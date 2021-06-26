#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "lexer.h"

/* variables */
static array_type(token) cachedTokens;
unsigned int srcIndex;

/* function implementations */
static int
GetChar(void)
{
	if (srcIndex == srcSize)
		return EOF;
	return srcCode[srcIndex++];
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
	case TOK_CHAR: printf("'%s'", tok.val.s.str); break;
	case TOK_INT: printf("%llu", tok.val.i); break;
	case TOK_FLOAT: printf("%f", tok.val.f); break;
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
ReadString(char end)
{
	string buf = {0};
	symbol sym;

	for (;;) {
		int c = GetChar();
		if (c == '\\')
			c = GetEscape();
		if (c == EOF || c == end)
			break;
		StringAddC(&buf, (char)c);
	}

	sym = AddSymbol(buf.data, buf.len);
	StringFree(&buf);
	return sym;
}

static token
NextLiteralToken(void)
{
	int c = GetChar();
	token tok;

	/* TODO: make this... thing cleaner */
	while (isspace(c) || c == '/') {
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
		tok.val.s = ReadString('"');
		return tok;
	case '\'':
		tok.type = TOK_CHAR;
		tok.val.s = ReadString('\'');
		if (!tok.val.s.len)
			Error(tok.pos, "empty character string found");
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

		tok.type = TOK_IDENT;
		tok.val.s = AddSymbol(buf.data, buf.len);
		StringFree(&buf);
		return tok;
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
