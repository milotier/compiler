/* Requires stdlib.h and string.h */
#ifndef COMMON_H
#define COMMON_H

/* macros */
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define LEN(a) (sizeof(a) / sizeof((a)[0]))

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

/* variables */
extern char *argv0;
extern char *srcPath;
extern char *srcCode;
extern unsigned int srcSize;

/* function declarations */
NORETURN void Die(char *, ...);
NORETURN void Error(unsigned int, char *, ...);
void Warn(unsigned int, char *, ...);
void *xmalloc(size_t);
void *xrealloc(void *, size_t);
symbol AddSymbol(char *, unsigned int);
void StringAddS(string *, char *);
void StringAddC(string *, char);
void StringFree(string *);
void TableAdd(table *, symbol, void *);
void *TableGet(table *, symbol);
void TableRemove(table *, symbol, void (*)(void *));
void TableClear(table *, void (*)(void *));
void TableFree(table *, void (*)(void *));

/* These macros need to be defined here because they use xrealloc */
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

#endif /* COMMON_H */
