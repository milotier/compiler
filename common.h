/* Requires stdlib.h, string.h and lexer.h */
#ifndef COMMON_H
#define COMMON_H

/* This is used by arrayAdd, so it needs to be declared here */
void *xMalloc(size_t);

/* macros */
#define SWAP(T, x, y) do { T tmp = x; x = y; y = tmp; } while (0);
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define LEN(a) (sizeof(a) / sizeof((a)[0]))

#define U64_MAX (~0llu)

#define I32_MAX ((1l << 31) - 1)
#define I64_MAX ((1ll << 63) - 1)
#define I64_MIN (-(1ll << 63))

#if defined(__GNUC__) || defined(__clang__)
# define NORETURN __attribute__((noreturn))
#else
# define NORETURN
#endif

#define ArrayType(T) struct { T *data; unsigned int len, cap; }
#define arrayCopy(a, b) ( \
    (b)->cap = (a)->cap, \
    (b)->len = (a)->len, \
    (b)->data = xMalloc(sizeof(*(a)->data) * (a)->cap), \
    memcpy((b)->data, (a)->data, sizeof(*(a)->data) * (a)->len) \
)
#define arraySetLen(a, l) ( \
    ((l) > (a)->cap) ? (void)( \
        (a)->cap = MAX((a)->cap * 2, l), \
        (a)->data = xRealloc((a)->data, sizeof(*(a)->data) * (a)->cap) \
    ) : (void)0, \
    (a)->len = (l) \
)
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
#define arrayFree(a) ( \
    free((a)->data), \
    (a)->data = NULL, \
    (a)->len = 0, \
    (a)->cap = 0 \
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
    ArrayType(unsigned int) data;
    int sign;
} BigInt;

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
    TYPE_COMPTIME_INT,
    TYPE_INT,
    TYPE_COMPTIME_FLOAT,
    TYPE_FLOAT,
    TYPE_BOOL,
};
typedef struct {
    unsigned char kind;
    unsigned char isSigned: 1;
    unsigned char width;
} DataType;

enum {
    EXPR_INT,
    EXPR_FLOAT,
    EXPR_BOOL,
    EXPR_STR,
    EXPR_FUNC,

    EXPR_FIRST_LIT = EXPR_INT,
    EXPR_LAST_LIT = EXPR_FUNC,

    EXPR_IDENT,
    EXPR_MEMBER,
    EXPR_CALL,
    EXPR_UNOP,
    EXPR_BINOP,
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
    BigInt value;
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
        BigInt bi;
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

BigInt bigIntFromU64(unsigned long long);
BigInt bigIntFromI64(long long);
BigInt bigIntCopy(BigInt *);
void bigIntFree(BigInt *);
void bigIntMove(BigInt *, BigInt);
int bigIntCmp(BigInt *, BigInt *);
int bigIntToU64(unsigned long long *out, BigInt *x);
void bigIntAnd(BigInt *out, BigInt *x, BigInt *y);
void bigIntOr(BigInt *out, BigInt *x, BigInt *y);
void bigIntXor(BigInt *out, BigInt *x, BigInt *y);
int bigIntLshift(BigInt *out, BigInt *x, BigInt *y);
void bigIntRshift(BigInt *out, BigInt *x, BigInt *y);
void bigIntAdd(BigInt *, BigInt *, BigInt *);
void bigIntSub(BigInt *, BigInt *, BigInt *);
void bigIntMul(BigInt *, BigInt *, BigInt *);
int bigIntDiv(BigInt *, BigInt *, BigInt *, BigInt *);
void bigIntPrint(BigInt *);

/* Only used for debugging */
void printToken(Token);
void printType(DataType);
void printExpr(ExprHeader *);
void printDecl(Declaration *);
void printStmt(StmtHeader *);

/* Allocation functions */
#define EXPR_ALLOC_FUNC(lower, upper) \
static inline lower##Expr * \
alloc##lower##Expr(void) \
{ \
    lower##Expr *expr = xMalloc(sizeof(*expr)); \
    *expr = (lower##Expr) {0}; \
    expr->header.type = EXPR_##upper; \
    return expr; \
}

EXPR_ALLOC_FUNC(Int, INT)
EXPR_ALLOC_FUNC(Float, FLOAT)
EXPR_ALLOC_FUNC(Bool, BOOL)
EXPR_ALLOC_FUNC(Str, STR)
EXPR_ALLOC_FUNC(Member, MEMBER)
EXPR_ALLOC_FUNC(Call, CALL)
EXPR_ALLOC_FUNC(Ident, IDENT)
EXPR_ALLOC_FUNC(Unop, UNOP)
EXPR_ALLOC_FUNC(Binop, BINOP)
EXPR_ALLOC_FUNC(Func, FUNC)

#define STMT_ALLOC_FUNC(lower, upper) \
static inline lower##Stmt * \
alloc##lower##Stmt(void) \
{ \
    lower##Stmt *stmt = xMalloc(sizeof(*stmt)); \
    *stmt = (lower##Stmt) {0}; \
    stmt->header.type = STMT_##upper; \
    return stmt; \
}

STMT_ALLOC_FUNC(Expr, EXPR)
STMT_ALLOC_FUNC(Return, RETURN)
STMT_ALLOC_FUNC(If, IF)
STMT_ALLOC_FUNC(While, WHILE)
STMT_ALLOC_FUNC(Block, BLOCK)
STMT_ALLOC_FUNC(Decl, DECL)

#endif /* COMMON_H */
