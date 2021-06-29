/* Requires stdlib.h, string.h and lexer.h */
#ifndef COMMON_H
#define COMMON_H

/* This is used by arrayAdd, so it needs to be declared here */
void *xMalloc(size_t);

/* macros */
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define LEN(a) (sizeof(a) / sizeof((a)[0]))

#define I64_MAX ((1llu << 63) - 1)

#if defined(__GNUC__) || defined(__clang__)
# define NORETURN __attribute__((noreturn))
#else
# define NORETURN
#endif

#define ArrayType(T) struct { T *data; unsigned int len, cap; }
#define arrayAdd(a, v) ( \
    ((a)->len == (a)->cap) ? (void)( \
        (a)->cap = MAX((a)->cap * 2, 4), \
        (a)->data = xRealloc((a)->data, sizeof(*(a)->data) * (a)->cap) \
    ) : (void)0, \
    (a)->len++, \
    (a)->data[(a)->len - 1] = (v) \
)
#define arrayRemove(a, i) ( \
    (a)->len--, \
    memmove((a)->data + i, (a)->data + i + 1, sizeof(*(a)->data) * ((a)->len - i)) \
)


/* types */

/* An interned string, used for identifiers and literals */
typedef struct {
    char *str;
    unsigned int len;
} Symbol;
/* A dynamically growing string with a terminating null byte */
typedef struct {
    char *data;
    unsigned int len, cap;
} String;

typedef struct {
    struct {
        Symbol sym;
        void *val;
    } *entries;
    unsigned int len, cap;
} Table;

/* Types used in the AST */
enum {
    TYPE_INFERRED,
    TYPE_VOID,
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_BOOL,
};
typedef struct {
    unsigned char kind;
    unsigned char isSigned: 1;
    unsigned char isFromLiteral: 1;
    unsigned char width;
} DataType;

enum {
    EXPR_INT,
    EXPR_FLOAT,
    EXPR_BOOL,
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
    DataType dataType;
} ExprHeader;

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
} StmtHeader;

typedef struct {
    Symbol name;
    ExprHeader *value;
    DataType type;
    unsigned int pos;
    unsigned char isConst;
} Declaration;

typedef struct scope Scope;
struct scope {
    Scope *parent;
    /* The top-level Scope uses a Table, all others use an array */
    union {
        ArrayType(Declaration *) arr;
        Table tbl;
    } value;
};

typedef struct {
    ExprHeader header;
    unsigned long long value;
} IntExpr;

typedef struct {
    ExprHeader header;
    unsigned char value;
} BoolExpr;

typedef struct {
    ExprHeader header;
    double value;
} FloatExpr;

typedef struct {
    ExprHeader header;
    Symbol value;
} StrExpr, IdentExpr;

typedef struct {
    ExprHeader header;
    ExprHeader *child;
    Symbol member;
} MemberExpr;

typedef struct {
    ExprHeader header;
    ExprHeader *func;
    ArrayType(ExprHeader *) args;
} CallExpr;

typedef struct {
    ExprHeader header;
    Scope scope;
    ArrayType(Declaration) params;
    ArrayType(StmtHeader *) statements;
    DataType returnType;
} FuncExpr;

enum {
    UNOP_DEREF,
    UNOP_ADDR_OF,
    UNOP_NOT,
};
typedef struct {
    ExprHeader header;
    ExprHeader *child;
    unsigned int type;
} UnopExpr;

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
    BINOP_AND,
    BINOP_OR,
    BINOP_EQ,
    BINOP_NEQ,
    BINOP_LT,
    BINOP_LTE,
    BINOP_GT,
    BINOP_GTE,

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

    BINOP_INDEX,

    BINOP_FIRST_NON_ASS = BINOP_ADD,
    BINOP_LAST_NON_ASS = BINOP_GTE,
    BINOP_FIRST_ARITH = BINOP_ADD,
    BINOP_LAST_ARITH = BINOP_MOD,
    BINOP_FIRST_BINARY = BINOP_LSHIFT,
    BINOP_LAST_BINARY = BINOP_BITXOR,
    BINOP_FIRST_LOG = BINOP_AND,
    BINOP_LAST_LOG = BINOP_OR,
    BINOP_FIRST_CMP = BINOP_EQ,
    BINOP_LAST_CMP = BINOP_GTE,
};
typedef struct {
    ExprHeader header;
    ExprHeader *left, *right;
    unsigned int type;
} BinopExpr;

typedef struct {
    StmtHeader header;
    ExprHeader *expr;
} ExprStmt, ReturnStmt;

typedef struct {
    StmtHeader header;
    ExprHeader *condition;
    StmtHeader *ifBranch, *elseBranch;
} IfStmt;

typedef struct {
    StmtHeader header;
    ExprHeader *condition;
    StmtHeader *statement;
} WhileStmt;

typedef struct {
    StmtHeader header;
    ArrayType(StmtHeader *) statements;
    Scope scope;
} BlockStmt;

typedef struct {
    StmtHeader header;
    Declaration decl;
} DeclStmt;

enum {
    TOK_EOF,

    TOK_IDENT,
    FIRST_LIT_TOK = TOK_IDENT,
    TOK_STR,
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
    TOK_BOOL_TYPE,

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
        Symbol s;
    } val;
    unsigned int pos;
    char type;
} Token;

typedef struct {
    char *srcPath, *srcCode;
    unsigned int srcSize;

    /* AST */
    Scope topScope;

    /* Used only by the lexer */
    unsigned int srcIndex;
    ArrayType(Token) cachedTokens;
} Context;

/* variables */
extern char *argv0;

/* function Declarations */

NORETURN void die(char *, ...);
NORETURN void error(Context *ctx, unsigned int, char *, ...);
void warn(Context *ctx, unsigned int, char *, ...);
void *xRealloc(void *, size_t);
Symbol addSymbol(char *, unsigned int);
void stringAddS(String *, char *);
void stringAddC(String *, char);
void stringFree(String *);
void tableAdd(Table *, Symbol, void *);
void *tableGet(Table *, Symbol);
void tableRemove(Table *, Symbol, void (*)(void *));
void tableClear(Table *, void (*)(void *));
void tableFree(Table *, void (*)(void *));
void scopeAdd(Scope *, Declaration *);
Declaration *scopeGetNoParent(Scope *, Symbol);
Declaration *scopeGet(Scope *, Symbol);
/* Only used for debugging */
void printToken(Token);
void printType(DataType);
void printExpr(ExprHeader *);
void printDecl(Declaration *);
void printStmt(StmtHeader *);

#endif /* COMMON_H */
