#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "lexer.h"
#include "parser.h"

static expr_header *ParseExpr(context *, scope *);
static stmt_header *ParseStmt(context *, scope *);

/* function implementations */
#define EXPR_ALLOC_FUNC(lower, upper, fullUpper) \
static lower##_expr * \
Alloc##upper##Expr(void) \
{ \
	lower##_expr *expr = xmalloc(sizeof(*expr)); \
	*expr = (lower##_expr) {0}; \
	expr->header.type = EXPR_##fullUpper; \
	return expr; \
}

EXPR_ALLOC_FUNC(int, Int, INT);
EXPR_ALLOC_FUNC(float, Float, FLOAT);
EXPR_ALLOC_FUNC(bool, Bool, BOOL);
EXPR_ALLOC_FUNC(char, Char, CHAR);
EXPR_ALLOC_FUNC(str, Str, STR);
EXPR_ALLOC_FUNC(member, Member, MEMBER);
EXPR_ALLOC_FUNC(call, Call, CALL);
EXPR_ALLOC_FUNC(ident, Ident, IDENT);
EXPR_ALLOC_FUNC(unop, Unop, UNOP);
EXPR_ALLOC_FUNC(binop, Binop, BINOP);
EXPR_ALLOC_FUNC(func, Func, FUNC);

#define STMT_ALLOC_FUNC(lower, upper, fullUpper) \
static lower##_stmt * \
Alloc##upper##Stmt(void) \
{ \
	lower##_stmt *stmt = xmalloc(sizeof(*stmt)); \
	*stmt = (lower##_stmt) {0}; \
	stmt->header.type = STMT_##fullUpper; \
	return stmt; \
}

STMT_ALLOC_FUNC(expr, Expr, EXPR);
STMT_ALLOC_FUNC(return, Return, RETURN);
STMT_ALLOC_FUNC(if, If, IF);
STMT_ALLOC_FUNC(while, While, WHILE);
STMT_ALLOC_FUNC(block, Block, BLOCK);
STMT_ALLOC_FUNC(decl, Decl, DECL);

static data_type
ParseType(context *ctx)
{
	data_type type = {0};
	token tok = NextToken(ctx);

	switch (tok.type) {
	case TOK_U8: type.kind = TYPE_UINT; type.width = 8; break;
	case TOK_U16: type.kind = TYPE_UINT; type.width = 16; break;
	case TOK_U32: type.kind = TYPE_UINT; type.width = 32; break;
	case TOK_U64: type.kind = TYPE_UINT; type.width = 64; break;
	case TOK_I8: type.kind = TYPE_INT; type.width = 8; break;
	case TOK_I16: type.kind = TYPE_INT; type.width = 16; break;
	case TOK_I32: type.kind = TYPE_INT; type.width = 32; break;
	case TOK_I64: type.kind = TYPE_INT; type.width = 64; break;
	case TOK_F32: type.kind = TYPE_FLOAT; type.width = 32; break;
	case TOK_F64: type.kind = TYPE_FLOAT; type.width = 64; break;
	case TOK_CHAR_TYPE: type.kind = TYPE_CHAR; break;
	default: Error(ctx, tok.pos, "expected type");
	}

	return type;
}

static declaration
ParseDecl(context *ctx, scope *s)
{
	declaration decl = {0};
	token tok = NextToken(ctx);
	if (tok.type != TOK_IDENT)
		Error(ctx, tok.pos, "expected identifier in declaration");
	decl.name = tok.val.s;
	tok = NextToken(ctx);
	if (tok.type != ':')
		Error(ctx, tok.pos, "expected colon in declaration");
	decl.pos = tok.pos;

	tok = PeekToken(1, ctx);
	if (tok.type != '=' && tok.type != ':')
		decl.type = ParseType(ctx);
	tok = NextToken(ctx);
	if (tok.type == '=' || tok.type == ':') {
		if (tok.type == ':')
			decl.isConst = 1;
		decl.value = ParseExpr(ctx, s);
	}

	if (!(decl.value && decl.value->type == EXPR_FUNC)) {
		tok = NextToken(ctx);
		if (tok.type != ';')
			Error(ctx, tok.pos, "expected semicolon to end declaration");
	}

	return decl;
}

#define HANDLE_TOKEN(tokType, expr_type, Func, val) \
	case tokType: { \
		expr_type *expr = Func(); \
		expr->header.pos = tok.pos; \
		expr->value = val; \
		return (expr_header *)expr; \
	} break;

static expr_header *
ParseSingularExpr(context *ctx, scope *s)
{
	token tok = NextToken(ctx);
	switch (tok.type) {
	case '(': {
		if ((PeekToken(1, ctx).type == TOK_IDENT &&
		     PeekToken(2, ctx).type == ':') ||
		    PeekToken(1, ctx).type == ')') {
			func_expr *func = AllocFuncExpr();
			func->header.pos = tok.pos;
			func->funcScope.parent = s;

			while (tok.type != ')') {
				declaration decl = {0};
				tok = NextToken(ctx);
				if (tok.type != TOK_IDENT)
					Error(ctx, tok.pos,
					      "expected parameter name");
				decl.name = tok.val.s;
				tok = NextToken(ctx);
				if (tok.type != ':')
					Error(ctx, tok.pos,
					      "expected colon in parameter declaration");
				decl.pos = tok.pos;
				decl.type = ParseType(ctx);

				ArrayAdd(&func->params, decl);
				ScopeAdd(&func->funcScope,
					 &func->params.data[func->params.len - 1]);

				tok = NextToken(ctx);
				if (tok.type == ',' &&
				    PeekToken(1, ctx).type == ')') {
					NextToken(ctx);
					break;
				}
				if (tok.type != ',' && tok.type != ')')
					Error(ctx, tok.pos,
					      "expected comma or closing parenthesis");
			}

			tok = PeekToken(1, ctx);
			if (tok.type == '{') {
				NextToken(ctx);
				func->returnType.kind = TYPE_VOID;
			} else {
				func->returnType = ParseType(ctx);
				tok = NextToken(ctx);
				if (tok.type != '{')
					Error(ctx, tok.pos,
					      "expected opening brace");
			}

			while (PeekToken(1, ctx).type != '}') {
				stmt_header *stmt = ParseStmt(ctx, &func->funcScope);
				if (stmt)
					ArrayAdd(&func->statements, stmt);
			}
			NextToken(ctx);

			return (expr_header *)func;
		} else {
			expr_header *expr = ParseExpr(ctx, s);
			token next = NextToken(ctx);
			if (next.type != ')')
				Error(ctx, next.pos,
				      "expected closing parenthesis");
			if (expr->isParenthesized)
				Warn(ctx, tok.pos, "useless parentheses");
			expr->isParenthesized = 1;
			return expr;
		}
	} break;
	HANDLE_TOKEN(TOK_INT, int_expr, AllocIntExpr, tok.val.i)
	HANDLE_TOKEN(TOK_FLOAT, float_expr, AllocFloatExpr, tok.val.f)
	HANDLE_TOKEN(TOK_BOOL, bool_expr, AllocBoolExpr, (unsigned int)tok.val.i)
	HANDLE_TOKEN(TOK_CHAR, char_expr, AllocCharExpr, tok.val.i)
	HANDLE_TOKEN(TOK_STR, str_expr, AllocStrExpr, tok.val.s)
	HANDLE_TOKEN(TOK_IDENT, ident_expr, AllocIdentExpr, tok.val.s)
	default:
		Error(ctx, tok.pos, "unexpected token found while parsing expression");
	}
}

static expr_header *
ParsePostfixExpr(context *ctx, scope *s)
{
	expr_header *child = ParseSingularExpr(ctx, s);
	expr_header *expr = child;
	token tok = PeekToken(1, ctx);

	while (tok.type == '^' || tok.type == '.' ||
	       tok.type == '[' || tok.type == '(') {
		if (tok.type == '^') {
			char nextType = PeekToken(2, ctx).type;
			/* Check wether the caret is being used as
			 * a bitwise xor operator */
			if (nextType == '(' || (nextType >= FIRST_LIT_TOK &&
						nextType <= LAST_LIT_TOK))
				break;

			NextToken(ctx);
			expr = (expr_header *)AllocUnopExpr();
			expr->pos = tok.pos;
			((unop_expr *)expr)->type = UNOP_DEREF;
			((unop_expr *)expr)->child = child;
			child = expr;
		} else if (tok.type == '.') {
			NextToken(ctx);
			expr = (expr_header *)AllocMemberExpr();
			expr->pos = tok.pos;
			((member_expr *)expr)->child = child;
			tok = NextToken(ctx);
			if (tok.type != TOK_IDENT)
				Error(ctx, tok.pos, "expected member name after '.'");
			((member_expr *)expr)->member = tok.val.s;
		} else if (tok.type == '[') {
			NextToken(ctx);
			expr = (expr_header *)AllocBinopExpr();
			expr->pos = tok.pos;
			((binop_expr *)expr)->type = BINOP_INDEX;
			((binop_expr *)expr)->left = child;
			((binop_expr *)expr)->right = ParseExpr(ctx, s);
			tok = NextToken(ctx);
			if (tok.type != ']')
				Error(ctx, tok.pos, "expected closing bracket");
		} else {
			NextToken(ctx);
			expr = (expr_header *)AllocCallExpr();
			expr->pos = child->pos;
			((call_expr *)expr)->func = child;
			while (tok.type != ')') {
				ArrayAdd(&((call_expr *)expr)->args, ParseExpr(ctx, s));
				tok = NextToken(ctx);
				if (tok.type == ',' && PeekToken(1, ctx).type == ')') {
					NextToken(ctx);
					break;
				}
				if (tok.type != ',' && tok.type != ')')
					Error(ctx, tok.pos, "expected comma or closing parenthesis");
			}
		}

		tok = PeekToken(1, ctx);
	}

	return expr;
}

static expr_header *
ParsePrefixExpr(context *ctx, scope *s)
{
	token tok = PeekToken(1, ctx);

	if (tok.type == '&') {
		NextToken(ctx);
		unop_expr *expr = AllocUnopExpr();
		expr->header.pos = tok.pos;
		expr->type = UNOP_ADDR_OF;
		expr->child = ParsePrefixExpr(ctx, s);
		return (expr_header *)expr;
	}
	if (tok.type == '!') {
		NextToken(ctx);
		unop_expr *expr = AllocUnopExpr();
		expr->header.pos = tok.pos;
		expr->type = UNOP_NOT;
		expr->child = ParsePrefixExpr(ctx, s);
		return (expr_header *)expr;
	}

	return ParsePostfixExpr(ctx, s);
}

static unsigned int
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

static expr_header *
ParseExpr(context *ctx, scope *s)
{
	expr_header *expr = ParsePrefixExpr(ctx, s);
	token tok = PeekToken(1, ctx);
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
		binop_expr *exprBinop;

		NextToken(ctx);
		right = ParseExpr(ctx, s);

		expr = (expr_header *)AllocBinopExpr();
		expr->pos = tok.pos;
		exprBinop = (binop_expr *)expr;
		exprBinop->type = (unsigned int)type;
		exprBinop->left = left;
		exprBinop->right = right;
		/* Switch around members of expr and right so that it aligns with
		 * their precedences */
		if (right->type == EXPR_BINOP &&
		    ((binop_expr *)right)->type != BINOP_INDEX &&
		    !right->isParenthesized) {
			binop_expr *rightBinop = (binop_expr *)right;
			unsigned int rightType = rightBinop->type;
			if (BinopPrecedence((unsigned int)type) >
			    BinopPrecedence(rightType)) {
				expr->pos = right->pos;
				exprBinop->type = rightType;
				rightBinop->type = (unsigned int)type;
				exprBinop->right = rightBinop->right;
				rightBinop->right = rightBinop->left;
				rightBinop->left = left;
				exprBinop->left = right;
			}
		}
	}

	return expr;
}

static stmt_header *
ParseStmt(context *ctx, scope *s)
{
	token tok = PeekToken(1, ctx);
	stmt_header *stmt;

	if (tok.type == ';') {
		NextToken(ctx);
		return NULL;
	} else if (tok.type == TOK_BREAK) {
		NextToken(ctx);
		stmt = xmalloc(sizeof(*stmt));
		stmt->type = STMT_BREAK;
		stmt->pos = tok.pos;
		tok = NextToken(ctx);
		if (tok.type != ';')
			Error(ctx, tok.pos, "expected semicolon to end break statement");
	} else if (tok.type == TOK_CONTINUE) {
		NextToken(ctx);
		stmt = xmalloc(sizeof(*stmt));
		stmt->type = STMT_CONTINUE;
		stmt->pos = tok.pos;
		tok = NextToken(ctx);
		if (tok.type != ';')
			Error(ctx, tok.pos, "expected semicolon to end continue statement");
	} else if (tok.type == TOK_RETURN) {
		NextToken(ctx);
		stmt = (stmt_header *)AllocReturnStmt();
		stmt->pos = tok.pos;
		((return_stmt *)stmt)->expr = ParseExpr(ctx, s);
		tok = NextToken(ctx);
		if (tok.type != ';')
			Error(ctx, tok.pos, "expected semicolon to end return statement");
	} else if (tok.type == TOK_IF) {
		NextToken(ctx);
		stmt = (stmt_header *)AllocIfStmt();
		stmt->pos = tok.pos;
		((if_stmt *)stmt)->condition = ParseExpr(ctx, s);
		((if_stmt *)stmt)->if_branch = ParseStmt(ctx, s);

		tok = PeekToken(1, ctx);
		if (tok.type == TOK_ELSE) {
			NextToken(ctx);
			((if_stmt *)stmt)->else_branch = ParseStmt(ctx, s);
		}
	} else if (tok.type == TOK_WHILE) {
		NextToken(ctx);
		stmt = (stmt_header *)AllocWhileStmt();
		stmt->pos = tok.pos;
		((while_stmt *)stmt)->condition = ParseExpr(ctx, s);
		((while_stmt *)stmt)->statement = ParseStmt(ctx, s);
	} else if (tok.type == '{') {
		block_stmt *blockStmt;
		NextToken(ctx);
		stmt = (stmt_header *)AllocBlockStmt();
		blockStmt = (block_stmt *)stmt;
		stmt->pos = tok.pos;
		blockStmt->blockScope.parent = s;
		while (PeekToken(1, ctx).type != '}') {
			stmt_header *child_stmt = ParseStmt(ctx,
							    &blockStmt->blockScope);
			if (child_stmt)
				ArrayAdd(&((block_stmt *)stmt)->statements,
					 child_stmt);
		}
		NextToken(ctx);
	} else if (tok.type == TOK_IDENT && PeekToken(2, ctx).type == ':') {
		decl_stmt *declStmt;
		stmt = (stmt_header *)AllocDeclStmt();
		declStmt = (decl_stmt *)stmt;
		declStmt->decl = ParseDecl(ctx, s);
		stmt->pos = declStmt->decl.pos;

		if (ScopeGetNoParent(s, declStmt->decl.name))
			Error(ctx, stmt->pos,
			      "redeclaration of %s", declStmt->decl.name.str);
		ScopeAdd(s, &declStmt->decl);
	} else {
		stmt = (stmt_header *)AllocExprStmt();
		((expr_stmt *)stmt)->expr = ParseExpr(ctx, s);
		stmt->pos = ((expr_stmt *)stmt)->expr->pos;
		tok = NextToken(ctx);
		if (tok.type != ';')
			Error(ctx, tok.pos,
			      "expected semicolon to end expression statement");
	}

	return stmt;
}

void
Parse(context *ctx)
{
	while (PeekToken(1, ctx).type != TOK_EOF) {
		declaration *decl = xmalloc(sizeof(*decl));
		*decl = ParseDecl(ctx, &ctx->topScope);
		if (ScopeGetNoParent(&ctx->topScope, decl->name))
			Error(ctx, decl->pos, "redeclaration of %s", decl->name.str);
		ScopeAdd(&ctx->topScope, decl);
	}
}
