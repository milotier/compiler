#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

/* macros */
#define SYMBUFLEN 1024

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
char *argv0;
char *srcPath;
char *srcCode;
unsigned int srcSize;

/* function implementations */
void
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

void
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

void
Warn(unsigned int pos, char *fmt, ...)
{
	va_list args;

	fprintf(stderr, "%s:%d: warning: ", srcPath, PosToLine(pos));
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fputc('\n', stderr);
}

void *
xmalloc(size_t len)
{
	void *p = malloc(len);

	if (!p)
		Die("malloc:");

	return p;
}

void *
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
RAddSymbol(char *str, unsigned int len, int isNew)
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
				RAddSymbol(oldsyms[j].str, oldsyms[j].len, 0);

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

symbol
AddSymbol(char *str, unsigned int len)
{
	return RAddSymbol(str, len, 1);
}

void
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

void
StringAddC(string *str, char c)
{
	char data[2] = {c, '\0'};
	StringAddS(str, data);
}

void
StringFree(string *str)
{
	free(str->data);
	*str = (string) {0};
}

void
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

void *
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

void
TableRemove(table *tbl, symbol sym, void (*freeFunc)(void *))
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
			freeFunc(tbl->entries[i].val);
		}
		i = (i + 1) % tbl->cap;
	}
}

void
TableClear(table *tbl, void (*freeFunc)(void *))
{
	unsigned int i;

	if (freeFunc) {
		for (i = 0; i < tbl->cap; i++) {
			tbl->entries[i].sym = (symbol) {0};
			freeFunc(tbl->entries[i].val);
		}
	}

	tbl->len = 0;
}

void
TableFree(table *tbl, void (*freeFunc)(void *))
{
	TableClear(tbl, freeFunc);
	tbl->entries = NULL;
	tbl->cap = 0;
}
