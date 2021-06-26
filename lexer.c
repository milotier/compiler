#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "lexer.h"

/* TODO: think about allowing unicode in identifiers? */

/* variables */
static array_type(token) cachedTokens;
unsigned int srcIndex;

/* function implementations */
static int
GetChar(void)
{
	int c;
	if (srcIndex == srcSize)
		return EOF;

	c = srcCode[srcIndex];
	if ((unsigned int)c <= 32 && c != '\n' && c != '\r' && c != ' ' && c != '\t')
		Error(srcIndex, "found illegal character 0x%hhx", (char)c);
	srcIndex++;
	return c;
}

static void
UngetChar(void)
{
	srcIndex--;
}

void
PrintToken(token tok)
{
	switch (tok.type) {
	case TOK_EOF: printf("EOF"); break;
	case TOK_IDENT: printf("%s", tok.val.s.str); break;
	case TOK_STR: printf("\"%s\"", tok.val.s.str); break;
	case TOK_CHAR: printf("'%lc'", (wchar_t)tok.val.i); break;
	case TOK_INT: printf("%llu", tok.val.i); break;
	case TOK_FLOAT: printf("%f", tok.val.f); break;
	case TOK_BOOL: printf("%s", tok.val.i ? "true" : "false"); break;
	case TOK_ARROW: printf("->"); break;
	case TOK_LSHIFT: printf("<<"); break;
	case TOK_RSHIFT: printf(">>"); break;
	case TOK_AND: printf("&&"); break;
	case TOK_OR: printf("||"); break;
	case TOK_EQ: printf("=="); break;
	case TOK_NEQ: printf("!="); break;
	case TOK_GTE: printf(">="); break;
	case TOK_LTE: printf("<="); break;
	case TOK_ADD_ASS: printf("+="); break;
	case TOK_SUB_ASS: printf("-="); break;
	case TOK_MUL_ASS: printf("*="); break;
	case TOK_DIV_ASS: printf("/="); break;
	case TOK_MOD_ASS: printf("*="); break;
	case TOK_BITAND_ASS: printf("&="); break;
	case TOK_BITOR_ASS: printf("|="); break;
	case TOK_BITXOR_ASS: printf("^="); break;
	case TOK_LSHIFT_ASS: printf("<<="); break;
	case TOK_RSHIFT_ASS: printf(">>="); break;
	default: putchar(tok.type);
	}
}

static char
CheckForAssign(char type, char assignType)
{
	int c = GetChar();
	if (c == '=')
		return assignType;
	UngetChar();
	return type;
}

static token
ReadNumber(int c)
{
	string buf = {0};
	int isFloat = 0;
	int radix = 10;
	token tok;

	if (c == '0') {
		int next = GetChar();
		if (next == 'x') {
			radix = 16;
			c = GetChar();
		} else if (next == 'o') {
			radix = 8;
			c = GetChar();
		} else if (next == 'b') {
			radix = 2;
			c = GetChar();
		} else {
			UngetChar();
		}
	}

	for (;;) {
		if (c == '.') {
			if (radix != 10)
				break;
			isFloat = 1;
		} else if (radix == 2 && (c < '0' || c > '1')) {
			break;
		} else if (radix == 8 && (c < '0' || c > '7')) {
			break;
		} else if (radix == 10 && !isdigit(c)) {
			break;
		} else if (radix == 16 && !isxdigit(c)) {
			break;
		}
		StringAddC(&buf, (char)c);
		c = GetChar();
	}
	UngetChar();

	if (isFloat) {
		tok.type = TOK_FLOAT;
		tok.val.f = strtod(buf.data, NULL);
	} else {
		tok.type = TOK_INT;
		tok.val.i = strtoull(buf.data, NULL, radix);
	}

	StringFree(&buf);

	return tok;
}

static unsigned char
HexToInt(char c)
{
	if (isdigit(c))
		return (unsigned char)(c - '0');
	return (unsigned char)(c - 'a' + 10);
}

static char
GetEscape(void)
{
	unsigned int backslashPos = srcIndex - 1;
	int c = GetChar();

	switch (c) {
	case '\0': Error(backslashPos, "unexpected EOF after '\\' escape");
	case 'a': c = '\a'; break;
	case 'b': c = '\b'; break;
	case 'e': c = '\x1b'; break;
	case 'f': c = '\f'; break;
	case 'n': c = '\n'; break;
	case 'r': c = '\r'; break;
	case 't': c = '\t'; break;
	case 'v': c = '\v'; break;
	case '\\': c = '\\'; break;
	case '\'': c = '\''; break;
	case '"': c = '"'; break;
	case 'x': {
		unsigned char val = 0;

		c = GetChar();
		if (!isxdigit(c))
			Error(backslashPos, "expected two hex digits after \\x escape");
		val = (unsigned char)(val * 16) + HexToInt((char)c);
		c = GetChar();
		if (!isxdigit(c))
			Error(backslashPos, "expected two hex digits after \\x escape");
		val = (unsigned char)(val * 16) + HexToInt((char)c);

		c = (char)val;
		break;
	}
	default:
		Error(backslashPos, "invalid escape sequence \\%c", c);
	}

	return (char)c;
}

static symbol
ReadString(void)
{
	string buf = {0};
	symbol sym;

	for (;;) {
		int c = GetChar();
		if (c == '\\')
			c = GetEscape();
		if (c == EOF || c == '"')
			break;
		StringAddC(&buf, (char)c);
	}

	sym = AddSymbol(buf.data, buf.len);
	StringFree(&buf);
	return sym;
}

static unsigned long long
GetCodePoint(void)
{
	int c = GetChar();
	unsigned int uc = (unsigned int)c;
	if (c == EOF)
		Error(srcIndex, "unexpected EOF in character literal");
	if (uc <= 0x7f) {
		puts("1");
		return (unsigned long long)c;
	} else if ((uc & 0xe0) == 0xc0) {
		unsigned long long buf[2] = {uc & 0x1f};
		c = GetChar();
		uc = (unsigned int)c;
		if (c == EOF)
			Error(srcIndex, "unexpected EOF in character literal");
		if ((uc & 0xc0) != 0x80)
			Error(srcIndex, "invalid byte in character literal");
		buf[1] = uc & 0x3f;
		return buf[1] | (buf[0] << 6);
	} else if ((c & 0xf0) == 0xe0) {
		unsigned long long buf[3] = {uc & 0xf};
		unsigned int i;
		for (i = 1; i < LEN(buf); i++) {
			c = GetChar();
			uc = (unsigned int)c;
			if (c == EOF)
				Error(srcIndex, "unexpected EOF in character literal");
			if ((uc & 0xc0) != 0x80)
				Error(srcIndex, "invalid byte in character literal");
			buf[i] = uc & 0x3f;
		}
		return buf[2] | (buf[1] << 6) | (buf[0] << 12);
	} else if ((uc & 0xf8) == 0xf0) {
		unsigned long long buf[4] = {uc & 0x7};
		unsigned int i;
		for (i = 1; i < LEN(buf); i++) {
			c = GetChar();
			uc = (unsigned int)c;
			if (c == EOF)
				Error(srcIndex, "unexpected EOF in character literal");
			if ((uc & 0xc0) != 0x80)
				Error(srcIndex, "invalid byte in character literal");
			buf[i] = uc & 0x3f;
		}
		return buf[3] | (buf[2] << 6) | (buf[1] << 12) | (buf[0] << 18);
	} else {
		Error(srcIndex, "invalid byte in character literal");
	}
}

static token
NextLiteralToken(void)
{
	int c = GetChar();
	token tok;

	/* TODO: make this... thing cleaner */
	while (c == '\n' || c == '\r' || c == ' ' || c == '\t' || c == '/') {
		if (c == '/') {
			c = GetChar();
			if (c == '*') {
				unsigned int nesting = 1;
				c = GetChar();
				for (;;) {
					c = GetChar();
					if (c == '*') {
						c = GetChar();
						if (c == '/') {
							nesting--;
							if (!nesting)
								break;
						}
					} else if (c == '/') {
						c = GetChar();
						if (c == '*')
							nesting++;
					}
				}
			} else if (c == '/') {
				while (c != '\n')
					c = GetChar();
			}
		}
		c = GetChar();
	}
	tok.pos = srcIndex;

	switch (c) {
	case EOF: tok.type = TOK_EOF; return tok;
	case '=': tok.type = CheckForAssign('=', TOK_EQ); return tok;
	case '!': tok.type = CheckForAssign('!', TOK_NEQ); return tok;
	case '.': tok.type = '.'; return tok;
	case '>':
		c = GetChar();
		if (c == '>') {
			tok.type = CheckForAssign(TOK_RSHIFT, TOK_RSHIFT_ASS);
			return tok;
		}
		UngetChar();
		tok.type = CheckForAssign('>', TOK_GTE); return tok;
	case '<':
		c = GetChar();
		if (c == '<') {
			tok.type = CheckForAssign(TOK_LSHIFT, TOK_LSHIFT_ASS);
			return tok;
		}
		UngetChar();
		tok.type = CheckForAssign('<', TOK_LTE);
		return tok;
	case '+': tok.type = CheckForAssign('+', TOK_ADD_ASS); return tok;
	case '-':
		c = GetChar();
		if (c == '>') {
			tok.type = TOK_ARROW;
			return tok;
		}
		UngetChar();
		tok.type = CheckForAssign('-', TOK_SUB_ASS);
		return tok;
	case '*': tok.type = CheckForAssign('*', TOK_MUL_ASS); return tok;
	case '/': tok.type = CheckForAssign('/', TOK_DIV_ASS); return tok;
	case '%': tok.type = CheckForAssign('%', TOK_MOD_ASS); return tok;
	case '^': tok.type = CheckForAssign('^', TOK_BITXOR_ASS); return tok;
	case '&':
		c = GetChar();
		if (c == '&') {
			tok.type = TOK_AND;
			return tok;
		}
		UngetChar();
		tok.type = CheckForAssign('&', TOK_BITAND_ASS);
		return tok;
	case '|':
		c = GetChar();
		if (c == '|') {
			tok.type = TOK_OR;
			return tok;
		}
		UngetChar();
		tok.type = CheckForAssign('|', TOK_BITOR_ASS);
		return tok;
	case '"':
		tok.type = TOK_STR;
		tok.val.s = ReadString();
		return tok;
	case '\'':
		tok.type = TOK_CHAR;
		tok.val.i = GetCodePoint();
		return tok;
	}

	if (isdigit(c))
		return ReadNumber(c);
	if (isalpha(c) || c == '_') {
		string buf = {0};

		for (;;) {
			StringAddC(&buf, (char)c);
			c = GetChar();
			if (!isalnum(c) && c != '_') {
				UngetChar();
				break;
			}
		}

                if (strcmp(buf.data, "true") == 0) {
			tok.type = TOK_BOOL;
			tok.val.i = 1;
                } else if (strcmp(buf.data, "false") == 0) {
			tok.type = TOK_BOOL;
			tok.val.i = 0;
                } else if (strcmp(buf.data, "while") == 0) {
			tok.type = TOK_WHILE;
                } else if (strcmp(buf.data, "for") == 0) {
			tok.type = TOK_FOR;
                } else if (strcmp(buf.data, "break") == 0) {
			tok.type = TOK_BREAK;
                } else if (strcmp(buf.data, "continue") == 0) {
			tok.type = TOK_CONTINUE;
                } else if (strcmp(buf.data, "return") == 0) {
			tok.type = TOK_RETURN;
                } else {
			tok.type = TOK_IDENT;
			tok.val.s = AddSymbol(buf.data, buf.len);
                }

		StringFree(&buf);
		return tok;
	}

	if (c != '.' && c != ':' && c != ';' && c != ',' && c != '(' && c != ')'
	 && c != '[' && c != ']' && c != '{' && c != '}') {
		if (isprint(c))
			Error(tok.pos, "found illegal character '%c'", c);
		else
			Error(tok.pos, "found illegal character 0x%hhx", c);
	}

	/* It it got here, it is a single-character punctuation token */
	tok.type = (char)c;
	return tok;
}

token
NextToken(void)
{
	if (cachedTokens.len) {
		token tok = cachedTokens.data[0];
		ArrayRemove(&cachedTokens, 0);
		return tok;
	}
	return NextLiteralToken();
}

token
PeekToken(unsigned int i)
{
	while (cachedTokens.len < i)
		ArrayAdd(&cachedTokens, NextLiteralToken());
	return cachedTokens.data[i - 1];
}
