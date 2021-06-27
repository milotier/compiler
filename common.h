/* Requires stdlib.h, string.h and lexer.h */
#ifndef COMMON_H
#define COMMON_H

/* This is used by ArrayAdd, so it needs to be declared here */
void *xmalloc(size_t);

/* macros */
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define LEN(a) (sizeof(a) / sizeof((a)[0]))

#if defined(__GNUC__) || defined(__clang__)
# define NORETURN __attribute__((noreturn))
#else
# define NORETURN
#endif

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
	memmove((a)->data + i, (a)->data + i + 1, sizeof(*(a)->data) * ((a)->len - i)) \
)


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

/* Types used in the AST */
enum {
	TYPE_INFERRED,
	TYPE_VOID,
	TYPE_ANY_INT,
	TYPE_INT,
	TYPE_UINT,
	TYPE_FLOAT,
	TYPE_CHAR,
};
typedef struct {
	unsigned char kind, width;
} data_type;

enum {
	EXPR_INT,
	EXPR_FLOAT,
	EXPR_BOOL,
	EXPR_CHAR,
	EXPR_STR,
	EXPR_IDENT,
	EXPR_MEMBER,
	EXPR_CALL,
	EXPR_UNOP,
	EXPR_BINOP,
	EXPR_FUNC,
};
typedef struct {
	unsigned int pos;
	unsigned short type, isParenthesized;
} expr_header;

enum {
	STMT_EXPR,
	STMT_RETURN,
	STMT_BREAK,
	STMT_CONTINUE,
	STMT_IF,
	STMT_WHILE,
	STMT_BLOCK,
	STMT_DECL,
};
typedef struct {
	unsigned int pos, type;
} stmt_header;

typedef struct {
	symbol name;
	expr_header *value;
	data_type type;
	unsigned int pos;
	unsigned char isConst;
} declaration;

typedef struct scope scope;
struct scope {
	scope *parent;
	/* The top-level scope uses a table, all others use an array */
	union {
		array_type(declaration *) arr;
		table tbl;
	} value;
};

typedef struct {
	expr_header header;
	unsigned long long value;
} int_expr;

typedef struct {
	expr_header header;
	unsigned int value;
} bool_expr;

typedef struct {
	expr_header header;
	double value;
} float_expr;
typedef struct {
	expr_header header;
	unsigned long long value;
} char_expr;

typedef struct {
	expr_header header;
	symbol value;
} str_expr, ident_expr;

typedef struct {
	expr_header header;
	expr_header *child;
	symbol member;
} member_expr;

typedef struct {
	expr_header header;
	expr_header *func;
	array_type(expr_header *) args;
} call_expr;

typedef struct {
	expr_header header;
	scope funcScope;
	array_type(declaration) params;
	array_type(stmt_header *) statements;
	data_type returnType;
} func_expr;

enum {
	UNOP_DEREF,
	UNOP_ADDR_OF,
	UNOP_NOT,
};
typedef struct {
	expr_header header;
	expr_header *child;
	unsigned int type;
} unop_expr;

enum {
	BINOP_ADD,
	BINOP_SUB,
	BINOP_MUL,
	BINOP_DIV,
	BINOP_MOD,
	BINOP_LSHIFT,
	BINOP_RSHIFT,
	BINOP_BITAND,
	BINOP_BITOR,
	BINOP_BITXOR,

	BINOP_ADD_ASS,
	BINOP_SUB_ASS,
	BINOP_MUL_ASS,
	BINOP_DIV_ASS,
	BINOP_MOD_ASS,
	BINOP_LSHIFT_ASS,
	BINOP_RSHIFT_ASS,
	BINOP_BITAND_ASS,
	BINOP_BITOR_ASS,
	BINOP_BITXOR_ASS,

	BINOP_ASSIGN,

	BINOP_AND,
	BINOP_OR,

	BINOP_EQ,
	BINOP_NEQ,
	BINOP_LT,
	BINOP_LTE,
	BINOP_GT,
	BINOP_GTE,

	BINOP_INDEX,
};
typedef struct {
	expr_header header;
	expr_header *left, *right;
	unsigned int type;
} binop_expr;

typedef struct {
	stmt_header header;
	expr_header *expr;
} expr_stmt, return_stmt;

typedef struct {
	stmt_header header;
	expr_header *condition;
	stmt_header *if_branch, *else_branch;
} if_stmt;

typedef struct {
	stmt_header header;
	expr_header *condition;
	stmt_header *statement;
} while_stmt;

typedef struct {
	stmt_header header;
	array_type(stmt_header *) statements;
	scope blockScope;
} block_stmt;

typedef struct {
	stmt_header header;
	declaration decl;
} decl_stmt;

enum {
	TOK_EOF,

	TOK_IDENT,
        FIRST_LIT_TOK = TOK_IDENT,
	TOK_STR,
	TOK_CHAR,
	TOK_INT,
	TOK_FLOAT,
	TOK_BOOL,
        LAST_LIT_TOK = TOK_BOOL,

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

        TOK_U8,
        TOK_U16,
        TOK_U32,
        TOK_U64,
        TOK_I8,
        TOK_I16,
        TOK_I32,
        TOK_I64,
        TOK_F32,
        TOK_F64,
        TOK_CHAR_TYPE,

	TOK_IF,
	TOK_ELSE,
	TOK_WHILE,
	TOK_FOR,
	TOK_BREAK,
	TOK_CONTINUE,
	TOK_RETURN,
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

typedef struct {
	char *srcPath, *srcCode;
	unsigned int srcSize;

	/* AST */
	scope topScope;

	/* Used only by the lexer */
	unsigned int srcIndex;
	array_type(token) cachedTokens;
} context;

/* variables */
extern char *argv0;

/* function declarations */

NORETURN void Die(char *, ...);
NORETURN void Error(context *ctx, unsigned int, char *, ...);
void Warn(context *ctx, unsigned int, char *, ...);
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
void ScopeAdd(scope *, declaration *);
declaration *ScopeGetNoParent(scope *, symbol);
declaration *ScopeGet(scope *, symbol);
/* Only used for debugging */
void PrintToken(token);
void PrintType(data_type);
void PrintExpr(expr_header *);
void PrintDecl(declaration *);
void PrintStmt(stmt_header *);

#endif /* COMMON_H */
