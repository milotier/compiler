#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "common.h"
#include "lexer.h"
#include "parser.h"

/* function implementations */
#define AllocFunc(lower, upper, fullUpper) \
static lower##_expr * \
Alloc##upper##Expr(void) \
{ \
	lower##_expr *expr = xmalloc(sizeof(*expr)); \
	*expr = (lower##_expr) {0}; \
	expr->header.type = EXPR_##fullUpper; \
	return expr; \
}

AllocFunc(int, Int, INT);
AllocFunc(float, Float, FLOAT);
AllocFunc(char, Char, CHAR);
AllocFunc(str, Str, STR);
AllocFunc(member, Member, MEMBER);
AllocFunc(ident, Ident, IDENT);
AllocFunc(unop, Unop, UNOP);
AllocFunc(binop, Binop, BINOP);

expr_header *
ParseSingularExpr(void)
{
	token tok = NextToken();
	switch (tok.type) {
	case '(': {
		expr_header *expr = ParseExpr();
		token next = NextToken();
		if (next.type != ')')
			Error(next.pos, "expected closing parenthesis");
		if (expr->isParenthesized)
			Warn(tok.pos, "useless parentheses");
		expr->isParenthesized = 1;
		return expr;
	} break;
	case TOK_INT: {
		int_expr *expr = AllocIntExpr();
		expr->header.pos = tok.pos;
		expr->value = tok.val.i;
		return (expr_header *)expr;
	} break;
	case TOK_FLOAT: {
		float_expr *expr = AllocFloatExpr();
		expr->header.pos = tok.pos;
		expr->value = tok.val.f;
		return (expr_header *)expr;
	} break;
	case TOK_CHAR: {
		char_expr *expr = AllocCharExpr();
		expr->header.pos = tok.pos;
		expr->value = tok.val.i;
		return (expr_header *)expr;
	} break;
	case TOK_STR: {
		str_expr *expr = AllocStrExpr();
		expr->header.pos = tok.pos;
		expr->value = tok.val.s;
		return (expr_header *)expr;
	} break;
	case TOK_IDENT: {
		ident_expr *expr = AllocIdentExpr();
		expr->header.pos = tok.pos;
		expr->value = tok.val.s;
		return (expr_header *)expr;
	} break;
	default:
	      Error(tok.pos, "unexpected token found while parsing expression");
	}
}

expr_header *
ParsePostfixExpr(void)
{
	expr_header *child = ParseSingularExpr();
	expr_header *expr = child;
	token tok = PeekToken(1);

	while (tok.type == '^' || tok.type == '.' || tok.type == '[') {
		NextToken();

		if (tok.type == '^') {
			expr = (expr_header *)AllocUnopExpr();
			((unop_expr *)expr)->type = UNOP_DEREF;
			((unop_expr *)expr)->child = child;
			child = expr;
		} else if (tok.type == '.') {
			expr = (expr_header *)AllocMemberExpr();
			((member_expr *)expr)->child = child;
			tok = NextToken();
			if (tok.type != TOK_IDENT)
				Error(tok.pos, "expected member name after '.'");
			((member_expr *)expr)->member = tok.val.s;
		} else {
			expr = (expr_header *)AllocBinopExpr();
			((binop_expr *)expr)->type = BINOP_INDEX;
			((binop_expr *)expr)->left = child;
			((binop_expr *)expr)->right = ParseExpr();
			tok = NextToken();
			if (tok.type != ']')
				Error(tok.pos, "expected closing bracket");
		}

		tok = PeekToken(1);
	}

	return expr;
}

expr_header *
ParsePrefixExpr(void)
{
	token tok = PeekToken(1);

	if (tok.type == '&') {
		NextToken();
		unop_expr *expr = AllocUnopExpr();
		expr->type = UNOP_ADDR_OF;
		expr->child = ParsePrefixExpr();
		return (expr_header *)expr;
	}
	if (tok.type == '!') {
		NextToken();
		unop_expr *expr = AllocUnopExpr();
		expr->type = UNOP_NOT;
		expr->child = ParsePrefixExpr();
		return (expr_header *)expr;
	}

	return ParsePostfixExpr();
}

unsigned int
BinopPrecedence(unsigned int type)
{
	switch (type) {
	case BINOP_ADD_ASS:
	case BINOP_SUB_ASS:
	case BINOP_MUL_ASS:
	case BINOP_DIV_ASS:
	case BINOP_MOD_ASS:
	case BINOP_LSHIFT_ASS:
	case BINOP_RSHIFT_ASS:
	case BINOP_BITAND_ASS:
	case BINOP_BITOR_ASS:
	case BINOP_BITXOR_ASS:
	case BINOP_ASSIGN:
		return 0;
	case BINOP_AND:
	case BINOP_OR:
		return 1;
	case BINOP_EQ:
	case BINOP_NEQ:
	case BINOP_LT:
	case BINOP_LTE:
	case BINOP_GT:
	case BINOP_GTE:
		return 2;
	case BINOP_ADD:
	case BINOP_SUB:
		return 3;
	case BINOP_MUL:
	case BINOP_DIV:
	case BINOP_MOD:
		return 4;
	case BINOP_BITAND:
	case BINOP_BITOR:
	case BINOP_BITXOR:
		return 5;
	case BINOP_LSHIFT:
	case BINOP_RSHIFT:
		return 6;
	}
	assert(0);
}

expr_header *
ParseExpr(void)
{
	expr_header *expr = ParsePrefixExpr();
	token tok = PeekToken(1);
	int type = -1;

	switch (tok.type) {
	case '+': type = BINOP_ADD; break;
	case '-': type = BINOP_SUB; break;
	case '*': type = BINOP_MUL; break;
	case '/': type = BINOP_DIV; break;
	case '%': type = BINOP_MOD; break;
	case '&': type = BINOP_BITAND; break;
	case '|': type = BINOP_BITOR; break;
	case '^': type = BINOP_BITXOR; break;
	case '=': type = BINOP_ASSIGN; break;
	case '>': type = BINOP_GT; break;
	case '<': type = BINOP_LT; break;

	case TOK_LSHIFT: type = BINOP_LSHIFT; break;
	case TOK_RSHIFT: type = BINOP_RSHIFT; break;

	case TOK_EQ: type = BINOP_EQ; break;
	case TOK_NEQ: type = BINOP_NEQ; break;
	case TOK_GTE: type = BINOP_GTE; break;
	case TOK_LTE: type = BINOP_LTE; break;

	case TOK_AND: type = BINOP_AND; break;
	case TOK_OR: type = BINOP_OR; break;

	case TOK_ADD_ASS: type = BINOP_ADD_ASS; break;
	case TOK_SUB_ASS: type = BINOP_SUB_ASS; break;
	case TOK_MUL_ASS: type = BINOP_MUL_ASS; break;
	case TOK_DIV_ASS: type = BINOP_DIV_ASS; break;
	case TOK_MOD_ASS: type = BINOP_MOD_ASS; break;
	case TOK_LSHIFT_ASS: type = BINOP_LSHIFT_ASS; break;
	case TOK_RSHIFT_ASS: type = BINOP_RSHIFT_ASS; break;
	case TOK_BITAND_ASS: type = BINOP_BITAND_ASS; break;
	case TOK_BITOR_ASS: type = BINOP_BITOR_ASS; break;
	case TOK_BITXOR_ASS: type = BINOP_BITXOR_ASS; break;
	}

	if (type >= 0) {
		expr_header *left = expr;
		expr_header *right;
		binop_expr *expr_binop;

		NextToken();
		right = ParseExpr();

		expr = (expr_header *)AllocBinopExpr();
		expr_binop = (binop_expr *)expr;
		expr_binop->type = (unsigned int)type;
		/* Switch around members of expr and right so that it aligns with
		 * their precedences */
		if (right->type == EXPR_BINOP &&
		    ((binop_expr *)right)->type != BINOP_INDEX &&
		    !right->isParenthesized) {
			binop_expr *right_binop = (binop_expr *)right;
			unsigned int right_type = right_binop->type;
			if (BinopPrecedence((unsigned int)type) >
			    BinopPrecedence(right_type)) {
				expr_binop->type = right_type;
				right_binop->type = (unsigned int)type;
				expr_binop->right = right_binop->right;
				right_binop->right = right_binop->left;
				right_binop->left = left;
				expr_binop->left = right;
			} else {
				expr_binop->left = left;
				expr_binop->right = right;
			}
		} else {
			expr_binop->left = left;
			expr_binop->right = right;
		}
	}

	return expr;
}

/* Only used for debugging */
void
PrintExpr(expr_header *expr)
{
	switch (expr->type) {
	case EXPR_INT: printf("%llu", ((int_expr *)expr)->value); break;
	case EXPR_FLOAT: printf("%f", ((float_expr *)expr)->value); break;
	case EXPR_CHAR: printf("'%lc'", (wint_t)((char_expr *)expr)->value); break;
	case EXPR_STR: printf("\"%s\"", ((str_expr *)expr)->value.str); break;
	case EXPR_IDENT: printf("%s", ((ident_expr *)expr)->value.str); break;
	case EXPR_MEMBER: {
		member_expr *member = (member_expr *)expr;
		printf("(");
		PrintExpr(member->child);
		printf(").%s", member->member.str);
	} break;
	case EXPR_UNOP: {
		unop_expr *unop = (unop_expr *)expr;
		if (unop->type == UNOP_NOT) {
			printf("!(");
			PrintExpr(unop->child);
			printf(")");
		} else if (unop->type == UNOP_ADDR_OF) {
			printf("&(");
			PrintExpr(unop->child);
			printf(")");
		} else if (unop->type == UNOP_DEREF) {
			printf("(");
			PrintExpr(unop->child);
			printf(")^");
		}
	} break;
	case EXPR_BINOP: {
		binop_expr *binop = (binop_expr *)expr;
		char *operator;
		if (binop->type == BINOP_INDEX) {
			printf("(");
			PrintExpr(binop->left);
			printf(")");
			printf("[");
			PrintExpr(binop->right);
			printf("]");
			break;
		}
		switch (binop->type) {
		case BINOP_ADD: operator = "+"; break;
		case BINOP_SUB: operator = "-"; break;
		case BINOP_MUL: operator = "*"; break;
		case BINOP_DIV: operator = "/"; break;
		case BINOP_MOD: operator = "%"; break;
		case BINOP_LSHIFT: operator = "<<"; break;
		case BINOP_RSHIFT: operator = ">>"; break;
		case BINOP_BITAND: operator = "&"; break;
		case BINOP_BITOR: operator = "|"; break;
		case BINOP_BITXOR: operator = "^"; break;
		case BINOP_ADD_ASS: operator = "+="; break;
		case BINOP_SUB_ASS: operator = "-="; break;
		case BINOP_MUL_ASS: operator = "*="; break;
		case BINOP_DIV_ASS: operator = "/="; break;
		case BINOP_MOD_ASS: operator = "%="; break;
		case BINOP_LSHIFT_ASS: operator = "<<="; break;
		case BINOP_RSHIFT_ASS: operator = ">>="; break;
		case BINOP_BITAND_ASS: operator = "&="; break;
		case BINOP_BITOR_ASS: operator = "|="; break;
		case BINOP_BITXOR_ASS: operator = "^="; break;
		case BINOP_ASSIGN: operator = "="; break;
		case BINOP_AND: operator = "&&"; break;
		case BINOP_OR: operator = "||"; break;
		case BINOP_EQ: operator = "=="; break;
		case BINOP_NEQ: operator = "!="; break;
		case BINOP_LT: operator = "<"; break;
		case BINOP_LTE: operator = "<="; break;
		case BINOP_GT: operator = ">"; break;
		case BINOP_GTE: operator = ">="; break;
		}
		printf("(");
		PrintExpr(binop->left);
		printf(") %s (", operator);
		PrintExpr(binop->right);
		printf(")");
	} break;
	}
}
