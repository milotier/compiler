#ifndef LEXER_H
#define LEXER_H

/* types */
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

/* function declarations */

/* Only used for debugging */
void PrintToken(token);

token NextToken(void);
token PeekToken(unsigned int);

#endif /* LEXER_H */
