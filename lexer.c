#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "lexer.h"

/* TODO: think about allowing unicode in identifiers? */

/* function implementations */
static int
GetChar(context *ctx)
{
	int c;
	if (ctx->srcIndex == ctx->srcSize)
		return EOF;

	c = ctx->srcCode[ctx->srcIndex];
	if ((unsigned int)c <= 32 && c != '\n' && c != '\r' && c != ' ' && c != '\t')
		Error(ctx, ctx->srcIndex, "found illegal character 0x%hhx", (char)c);
	ctx->srcIndex++;
	return c;
}

static void
UngetChar(context *ctx)
{
	ctx->srcIndex--;
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
CheckForAssign(char type, char assignType, context *ctx)
{
	int c = GetChar(ctx);
	if (c == '=')
		return assignType;
	UngetChar(ctx);
	return type;
}

static token
ReadNumber(int c, context *ctx)
{
	string buf = {0};
	int isFloat = 0;
	int radix = 10;
	token tok;

	if (c == '0') {
		int next = GetChar(ctx);
		if (next == 'x') {
			radix = 16;
			c = GetChar(ctx);
		} else if (next == 'o') {
			radix = 8;
			c = GetChar(ctx);
		} else if (next == 'b') {
			radix = 2;
			c = GetChar(ctx);
		} else {
			UngetChar(ctx);
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
		c = GetChar(ctx);
	}
	UngetChar(ctx);

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
GetEscape(context *ctx)
{
	unsigned int backslashPos = ctx->srcIndex - 1;
	int c = GetChar(ctx);

	switch (c) {
	case '\0': Error(ctx, backslashPos, "unexpected EOF after '\\' escape");
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

		c = GetChar(ctx);
		if (!isxdigit(c))
			Error(ctx, backslashPos, "expected two hex digits after \\x escape");
		val = (unsigned char)(val * 16) + HexToInt((char)c);
		c = GetChar(ctx);
		if (!isxdigit(c))
			Error(ctx, backslashPos, "expected two hex digits after \\x escape");
		val = (unsigned char)(val * 16) + HexToInt((char)c);

		c = (char)val;
		break;
	}
	default:
		Error(ctx, backslashPos, "invalid escape sequence \\%c", c);
	}

	return (char)c;
}

static symbol
ReadString(context *ctx)
{
	string buf = {0};
	symbol sym;

	for (;;) {
		int c = GetChar(ctx);
		if (c == '\\')
			c = GetEscape(ctx);
		if (c == EOF || c == '"')
			break;
		StringAddC(&buf, (char)c);
	}

	sym = AddSymbol(buf.data, buf.len);
	StringFree(&buf);
	return sym;
}

/* TODO: clean this up */
static unsigned long long
GetCodePoint(context *ctx)
{
	int c = GetChar(ctx);
	unsigned int uc = (unsigned int)c;
	if (c == EOF)
		Error(ctx, ctx->srcIndex, "unexpected EOF in character literal");
	if (uc <= 0x7f) {
		puts("1");
		return (unsigned long long)c;
	} else if ((uc & 0xe0) == 0xc0) {
		unsigned long long buf[2] = {uc & 0x1f};
		c = GetChar(ctx);
		uc = (unsigned int)c;
		if (c == EOF)
			Error(ctx, ctx->srcIndex, "unexpected EOF in character literal");
		if ((uc & 0xc0) != 0x80)
			Error(ctx, ctx->srcIndex, "invalid byte in character literal");
		buf[1] = uc & 0x3f;
		return buf[1] | (buf[0] << 6);
	} else if ((c & 0xf0) == 0xe0) {
		unsigned long long buf[3] = {uc & 0xf};
		unsigned int i;
		for (i = 1; i < LEN(buf); i++) {
			c = GetChar(ctx);
			uc = (unsigned int)c;
			if (c == EOF)
				Error(ctx, ctx->srcIndex, "unexpected EOF in character literal");
			if ((uc & 0xc0) != 0x80)
				Error(ctx, ctx->srcIndex, "invalid byte in character literal");
			buf[i] = uc & 0x3f;
		}
		return buf[2] | (buf[1] << 6) | (buf[0] << 12);
	} else if ((uc & 0xf8) == 0xf0) {
		unsigned long long buf[4] = {uc & 0x7};
		unsigned int i;
		for (i = 1; i < LEN(buf); i++) {
			c = GetChar(ctx);
			uc = (unsigned int)c;
			if (c == EOF)
				Error(ctx, ctx->srcIndex, "unexpected EOF in character literal");
			if ((uc & 0xc0) != 0x80)
				Error(ctx, ctx->srcIndex, "invalid byte in character literal");
			buf[i] = uc & 0x3f;
		}
		return buf[3] | (buf[2] << 6) | (buf[1] << 12) | (buf[0] << 18);
	} else {
		Error(ctx, ctx->srcIndex, "invalid byte in character literal");
	}
}

static token
NextLiteralToken(context *ctx)
{
	int c = GetChar(ctx);
	token tok;

	/* TODO: make this... thing cleaner */
	while (c == '\n' || c == '\r' || c == ' ' || c == '\t' || c == '/') {
		if (c == '/') {
			c = GetChar(ctx);
			if (c == '*') {
				unsigned int nesting = 1;
				c = GetChar(ctx);
				for (;;) {
					c = GetChar(ctx);
					if (c == '*') {
						c = GetChar(ctx);
						if (c == '/') {
							nesting--;
							if (!nesting)
								break;
						}
					} else if (c == '/') {
						c = GetChar(ctx);
						if (c == '*')
							nesting++;
					}
				}
			} else if (c == '/') {
				while (c != '\n')
					c = GetChar(ctx);
			}
		}
		c = GetChar(ctx);
	}
	tok.pos = ctx->srcIndex;

	switch (c) {
	case EOF: tok.type = TOK_EOF; return tok;
	case '=': tok.type = CheckForAssign('=', TOK_EQ, ctx); return tok;
	case '!': tok.type = CheckForAssign('!', TOK_NEQ, ctx); return tok;
	case '.': tok.type = '.'; return tok;
	case '>':
		c = GetChar(ctx);
		if (c == '>') {
			tok.type = CheckForAssign(TOK_RSHIFT, TOK_RSHIFT_ASS, ctx);
			return tok;
		}
		UngetChar(ctx);
		tok.type = CheckForAssign('>', TOK_GTE, ctx);
		return tok;
	case '<':
		c = GetChar(ctx);
		if (c == '<') {
			tok.type = CheckForAssign(TOK_LSHIFT, TOK_LSHIFT_ASS, ctx);
			return tok;
		}
		UngetChar(ctx);
		tok.type = CheckForAssign('<', TOK_LTE, ctx);
		return tok;
	case '+': tok.type = CheckForAssign('+', TOK_ADD_ASS, ctx); return tok;
	case '-':
		c = GetChar(ctx);
		if (c == '>') {
			tok.type = TOK_ARROW;
			return tok;
		}
		UngetChar(ctx);
		tok.type = CheckForAssign('-', TOK_SUB_ASS, ctx);
		return tok;
	case '*': tok.type = CheckForAssign('*', TOK_MUL_ASS, ctx); return tok;
	case '/': tok.type = CheckForAssign('/', TOK_DIV_ASS, ctx); return tok;
	case '%': tok.type = CheckForAssign('%', TOK_MOD_ASS, ctx); return tok;
	case '^': tok.type = CheckForAssign('^', TOK_BITXOR_ASS, ctx); return tok;
	case '&':
		c = GetChar(ctx);
		if (c == '&') {
			tok.type = TOK_AND;
			return tok;
		}
		UngetChar(ctx);
		tok.type = CheckForAssign('&', TOK_BITAND_ASS, ctx);
		return tok;
	case '|':
		c = GetChar(ctx);
		if (c == '|') {
			tok.type = TOK_OR;
			return tok;
		}
		UngetChar(ctx);
		tok.type = CheckForAssign('|', TOK_BITOR_ASS, ctx);
		return tok;
	case '"':
		tok.type = TOK_STR;
		tok.val.s = ReadString(ctx);
		return tok;
	case '\'':
		tok.type = TOK_CHAR;
		tok.val.i = GetCodePoint(ctx);
		return tok;
	}

	if (isdigit(c))
		return ReadNumber(c, ctx);
	if (isalpha(c) || c == '_') {
		string buf = {0};

		for (;;) {
			StringAddC(&buf, (char)c);
			c = GetChar(ctx);
			if (!isalnum(c) && c != '_') {
				UngetChar(ctx);
				break;
			}
		}

                if (strcmp(buf.data, "true") == 0) {
			tok.type = TOK_BOOL;
			tok.val.i = 1;
                } else if (strcmp(buf.data, "false") == 0) {
			tok.type = TOK_BOOL;
			tok.val.i = 0;
                } else if (strcmp(buf.data, "if") == 0) {
			tok.type = TOK_IF;
                } else if (strcmp(buf.data, "else") == 0) {
			tok.type = TOK_ELSE;
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
			Error(ctx, tok.pos, "found illegal character '%c'", c);
		else
			Error(ctx, tok.pos, "found illegal character 0x%hhx", c);
	}

	/* It it got here, it is a single-character punctuation token */
	tok.type = (char)c;
	return tok;
}

token
NextToken(context *ctx)
{
	if (ctx->cachedTokens.len) {
		token tok = ctx->cachedTokens.data[0];
		ArrayRemove(&ctx->cachedTokens, 0);
		return tok;
	}
	return NextLiteralToken(ctx);
}

token
PeekToken(unsigned int i, context *ctx)
{
	while (ctx->cachedTokens.len < i)
		ArrayAdd(&ctx->cachedTokens, NextLiteralToken(ctx));
	return ctx->cachedTokens.data[i - 1];
}
