#ifndef LEXER_H
#define LEXER_H

/* types */
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

	TOK_IF,
	TOK_ELSE,
	TOK_WHILE,
	TOK_FOR,
	TOK_BREAK,
	TOK_CONTINUE,
	TOK_RETURN,
};
struct token {
	union {
		double f;
		unsigned long long i;
		symbol s;
	} val;
	unsigned int pos;
	char type;
};

/* function declarations */

/* Only used for debugging */
void PrintToken(token);

token NextToken(context *);
token PeekToken(unsigned int, context *);

#endif /* LEXER_H */
