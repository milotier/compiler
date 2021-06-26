/*
 * vim: noexpandtab:softtabstop=8:shiftwidth=8
 */
#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define LEN(a) (sizeof(a) / sizeof((a)[0]))
#define SYMBUFLEN 1024

#define array_type(T) struct { T *data; unsigned int len, cap; }
#define ArrayAdd(a, v) ( \
	((a)->len == (a)->cap) ? ( \
		(a)->cap = MAX((a)->cap * 2, 4), \
		(a)->data = xrealloc((a)->data, sizeof(*(a)->data) * (a)->cap) \
	) : (void)0, \
	(a)->len++, \
	(a)->data[(a)->len - 1] = (v) \
)
#define ArrayRemove(a, i) ( \
	(a)->len--, \
	memmove((a)->data + i, (a)->data + i + 1, (a)->len - i) \
)

#if defined(__GNUC__) || defined(__clang__)
# define NORETURN __attribute__((noreturn))
#else
# define NORETURN
#endif

/* types */

/* An interned string, used for identifiers and literals */
typedef struct {
	char *str;
	unsigned int len;
} symbol;
/* A dynamically growing string with a terminating null byte */
typedef struct {
	char *data;
	unsigned int len, cap;
} string;

typedef struct {
	struct {
		symbol sym;
		void *val;
	} *entries;
	unsigned int len, cap;
} table;

enum {
	TOK_EOF,

	TOK_IDENT,
	TOK_STR,
	TOK_CHAR,
	TOK_INT,
	TOK_FLOAT,

	TOK_ARROW,

	TOK_LSHIFT,
	TOK_RSHIFT,
	TOK_AND,
	TOK_OR,
	TOK_EQ,
	TOK_NEQ,
	TOK_GTE,
	TOK_LTE,

	TOK_ADD_ASS,
	TOK_SUB_ASS,
	TOK_MUL_ASS,
	TOK_DIV_ASS,
	TOK_MOD_ASS,
	TOK_BITAND_ASS,
	TOK_BITOR_ASS,
	TOK_BITXOR_ASS,
	TOK_LSHIFT_ASS,
	TOK_RSHIFT_ASS,
};
typedef struct {
	union {
		double f;
		unsigned long long i;
		symbol s;
	} val;
	unsigned int pos;
	char type;
} token;

/* variables */
static struct {
	/* The current buffer in which new symbols are stored. This is overwritten,
	 * because the memory for symbols is never freed, since it is used for
	 * the entire duration of the program. They are stored with a null
	 * terminator, to stay compatible with standard library functions. */
	char *symBuf;
	unsigned int symBufLen;

	symbol *symbols;
	unsigned int len, cap;
} symbolPool;
static char *argv0;
static char *srcPath;
static char *srcCode;
static unsigned int srcSize, srcIndex;
static array_type(token) cachedTokens;

/* function implementations */
NORETURN static void
Die(char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	if (fmt[0] && fmt[strlen(fmt) - 1] == ':') {
		fputc(' ', stderr);
		perror(NULL);
	} else {
		fputc('\n', stderr);
	}

	exit(1);
}

static unsigned int
PosToLine(unsigned int pos)
{
	unsigned int i, line = 1;
	for (i = 0; i < pos; i++) {
		if (srcCode[i] == '\n')
			line++;
	}
	return line;
}

NORETURN static void
Error(unsigned int pos, char *fmt, ...)
{
	va_list args;

	fprintf(stderr, "%s:%d: Error: ", srcPath, PosToLine(pos));
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fputc('\n', stderr);
	exit(1);
}

static void
Warn(unsigned int pos, char *fmt, ...)
{
	va_list args;

	fprintf(stderr, "%s:%d: warning: ", srcPath, PosToLine(pos));
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fputc('\n', stderr);
}

static void *
xmalloc(size_t len)
{
	void *p = malloc(len);

	if (!p)
		Die("malloc:");

	return p;
}

static void *
xrealloc(void *p, size_t len)
{
	p = realloc(p, len);
	if (!p)
		Die("realloc:");

	return p;
}

static unsigned long
HashString(char *str)
{
    unsigned long hash = 5381;
    unsigned int c;

    while ((c = (unsigned char)*str++))
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

    return hash;
}

static symbol
RAddSym(char *str, unsigned int len, int isNew)
{
	unsigned long i;

	if (symbolPool.len * 4 / 3 >= symbolPool.cap) {
		unsigned int j, oldcap = symbolPool.cap;
		symbol *oldsyms = symbolPool.symbols;

		symbolPool.cap = MAX(symbolPool.cap * 2, 16u);
		symbolPool.symbols = xmalloc(sizeof(*symbolPool.symbols) * symbolPool.cap);
		for (j = 0; j < symbolPool.cap; j++)
			symbolPool.symbols[j].str = NULL;
		for (j = 0; j < oldcap; j++)
			if (oldsyms[j].str)
				RAddSym(oldsyms[j].str, oldsyms[j].len, 0);

		free(oldsyms);
	}

	symbolPool.len++;
	i = HashString(str) % symbolPool.cap;

	for (;;) {
		if (!symbolPool.symbols[i].str) {
			char *dest;

			if (isNew) {
				if (!symbolPool.symBuf ||
				     symbolPool.symBufLen + len >= SYMBUFLEN) {
					symbolPool.symBuf = xmalloc(SYMBUFLEN);
					symbolPool.symBufLen = 0;
				}

				dest = symbolPool.symBuf + symbolPool.symBufLen;
				memcpy(dest, str, len);
				dest[len] = '\0';
				symbolPool.symBufLen += len + 1;
			} else {
				dest = str;
			}

			return symbolPool.symbols[i] = (symbol) {dest, len};
		} else if (strcmp(str, symbolPool.symbols[i].str) == 0) {
			return symbolPool.symbols[i];
		}

		i = (i + 1) % symbolPool.cap;
	}
}

static symbol
AddSym(char *str, unsigned int len)
{
	return RAddSym(str, len, 1);
}

static void
StringAddS(string *str, char *data)
{
	size_t dataLen = strlen(data);
	if (str->len + dataLen >= str->cap) {
		str->cap = MAX(MAX(str->cap * 2, 8),
			       str->len + (unsigned int)dataLen + 1);
		str->data = xrealloc(str->data, str->cap);
	}

	memcpy(str->data + str->len, data, dataLen);
	str->len += (unsigned int)dataLen;
	str->data[str->len] = '\0';
}

static void
StringAddC(string *str, char c)
{
	char data[2] = {c, '\0'};
	StringAddS(str, data);
}

static void
StringFree(string *str)
{
	free(str->data);
	*str = (string) {0};
}

static void
TableAdd(table *tbl, symbol sym, void *val)
{
	uintptr_t i;

	if (tbl->len * 4 / 3 >= tbl->cap) {
		unsigned int j;
		table new = {.cap = MAX(tbl->cap * 2, 16)};

		new.entries = xmalloc(sizeof(*new.entries) * new.cap);
		for (j = 0; j < new.cap; j++)
			new.entries[j].sym.str = NULL;
		for (j = 0; j < tbl->cap; j++)
			if (tbl->entries[j].sym.str)
				TableAdd(&new, tbl->entries[j].sym,
				       tbl->entries[j].val);
		free(tbl->entries);
		*tbl = new;
	}

	tbl->len++;
	i = (uintptr_t)sym.str % tbl->cap;

	for (;;) {
		if (!tbl->entries[i].sym.str) {
			tbl->entries[i].val = val;
			tbl->entries[i].sym = sym;
			break;
		} else if (sym.str == tbl->entries[i].sym.str) {
			tbl->entries[i].val = val;
			break;
		}
		i = (i + 1) % tbl->cap;
	}
}

static void *
TableGet(table *tbl, symbol sym)
{
	uintptr_t i;

	/* Prevent dividing by zero */
	if (!tbl->cap)
		return NULL;
	i = (uintptr_t)sym.str % tbl->cap;

	for (;;) {
		if (!tbl->entries[i].sym.str)
			return NULL;
		if (sym.str == tbl->entries[i].sym.str)
			return tbl->entries[i].val;
		i = (i + 1) % tbl->cap;
	}
}

static void
TableRemove(table *tbl, symbol sym, void (*freefunc)(void *))
{
	uintptr_t i;

	/* Prevent dividing by zero */
	if (!tbl->cap)
		return;
	i = (uintptr_t)sym.str % tbl->cap;

	for (;;) {
		if (!tbl->entries[i].sym.str)
			return;
		if (sym.str == tbl->entries[i].sym.str) {
			tbl->entries[i].sym = (symbol) {0};
			freefunc(tbl->entries[i].val);
		}
		i = (i + 1) % tbl->cap;
	}
}


static void
TableClear(table *tbl, void (*freefunc)(void *))
{
	unsigned int i;

	if (freefunc) {
		for (i = 0; i < tbl->cap; i++) {
			tbl->entries[i].sym = (symbol) {0};
			freefunc(tbl->entries[i].val);
		}
	}

	tbl->len = 0;
}

static void
TableFree(table *tbl, void (*freefunc)(void *))
{
	TableClear(tbl, freefunc);
	tbl->entries = NULL;
	tbl->cap = 0;
}

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

/* Only used for debugging */
static void
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

	sym = AddSym(buf.data, buf.len);
	StringFree(&buf);
	return sym;
}

static token
NextLiteralToken(void)
{
	int c = GetChar();
	token tok;

	while (c == ' ' || c == '\n' || c == '\t')
		c = GetChar();
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
		tok.val.s = AddSym(buf.data, buf.len);
		StringFree(&buf);
		return tok;
	}

	/* It it got here, it is a single-character punctuation token */
	tok.type = (char)c;
	return tok;
}

static token
NextToken(void)
{
	if (cachedTokens.len) {
		token tok = cachedTokens.data[0];
		ArrayRemove(&cachedTokens, 0);
		return tok;
	}
	return NextLiteralToken();
}

static token
PeekToken(unsigned int i)
{
	while (cachedTokens.len < i)
		ArrayAdd(&cachedTokens, NextLiteralToken());
	return cachedTokens.data[i - 1];
}

static void
Usage(void)
{
	Die("usage: %s [file] [--help]", argv0);
}

int
main(int argc, char *argv[])
{
	struct stat fileStat;
	int i, status, srcFile;
	unsigned int totalRead;
	token tok;

	argv0 = argv[0];

	if (argc == 1)
		Usage();
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0)
			Usage();
		else
			srcPath = argv[i];
	}

	if (!srcPath)
		Die("%s: no input file", argv0);

	srcFile = open(srcPath, O_RDONLY);
	if (srcFile < 0)
		Die("%s: failed to open %s:", argv0, srcPath);
	status = fstat(srcFile, &fileStat);
	if (status < 0)
		Die("%s: failed to get file status of %s:", argv0, srcPath);

	srcSize = (unsigned int)fileStat.st_size;
	srcCode = xmalloc(srcSize);
	totalRead = 0;
	while (totalRead < srcSize) {
		ssize_t bytesRead = read(srcFile, srcCode + totalRead, srcSize - totalRead);
		if (!bytesRead)
			break;
		if (bytesRead < 0)
			Die("%s: failed to read file %s:", argv0, srcPath);
		totalRead += (unsigned int)bytesRead;
	}

	while ((tok = NextToken()).type != TOK_EOF) {
		PrintToken(tok);
		putchar('\n');
	}
}
