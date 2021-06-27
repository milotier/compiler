/* Requires common.h */
#ifndef PARSER_H
#define PARSER_H

/* types */
enum {
	EXPR_INT,
	EXPR_FLOAT,
	EXPR_BOOL,
	EXPR_CHAR,
	EXPR_STR,
	EXPR_IDENT,
	EXPR_MEMBER,
	EXPR_FUNC,
	EXPR_UNOP,
	EXPR_BINOP,
};
typedef struct {
	unsigned int pos;
	unsigned short type, isParenthesized;
} expr_header;

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

enum {
	TYPE_INFERRED,
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
} block_stmt;

typedef struct {
	stmt_header header;
	symbol name;
	expr_header *value;
	data_type type;
} decl_stmt;

/* function declarations */

expr_header *ParseExpr(context *ctx);
stmt_header *ParseStmt(context *ctx);
/* Only used for debugging */
void PrintExpr(expr_header *expr);

#endif /* PARSER_H */
