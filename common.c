#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#include "common.h"
#include "lexer.h"

/* macros */
#define SYMBUFLEN 1024

/* variables */

/* TODO: make this thread-safe when implementing multithreading */
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

static void
PosToCoords(context *ctx, unsigned int pos, unsigned int *line, unsigned int *col)
{
	unsigned int i;
	*line = 1;
	*col = 0;
	for (i = 0; i < pos; i++) {
		if (ctx->srcCode[i] == '\n') {
			++*line;
			*col = 0;
		} else {
			++*col;
		}
	}
}

void
Error(context *ctx, unsigned int pos, char *fmt, ...)
{
	va_list args;
	unsigned int line, col;

	PosToCoords(ctx, pos, &line, &col);
	if (isatty(STDOUT_FILENO))
		fprintf(stderr, "%s:%u:%u: \x1b[1;91merror\x1b[m: ",
			ctx->srcPath, line, col);
	else
		fprintf(stderr, "%s:%u:%u: error: ",
			ctx->srcPath, line, col);
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fputc('\n', stderr);
	exit(1);
}

void
Warn(context *ctx, unsigned int pos, char *fmt, ...)
{
	va_list args;
	unsigned int line, col;

	PosToCoords(ctx, pos, &line, &col);
	if (isatty(STDOUT_FILENO))
		fprintf(stderr, "%s:%u:%u: \x1b[1;95mwarning\x1b[m: ",
			ctx->srcPath, line, col);
	else
		fprintf(stderr, "%s:%u:%u: warning: ",
			ctx->srcPath, line, col);
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

void
ScopeAdd(scope *s, declaration *decl)
{
	if (!s->parent)
		TableAdd(&s->value.tbl, decl->name, decl);
	else
		ArrayAdd(&s->value.arr, decl);
}

declaration *
ScopeGetNoParent(scope *s, symbol sym)
{
	unsigned int i;
	if (!s->parent)
		return TableGet(&s->value.tbl, sym);
	for (i = 0; i < s->value.arr.len; i++) {
		if (s->value.arr.data[i]->name.str == sym.str)
			return s->value.arr.data[i];
	}
	return NULL;
}

declaration *
ScopeGet(scope *s, symbol sym)
{
	while (s) {
		declaration *decl = ScopeGetNoParent(s, sym);
		if (decl)
			return decl;
		s = s->parent;
	}
	return NULL;
}

void
PrintToken(token tok)
{
	switch (tok.type) {
	case TOK_EOF: printf("EOF"); break;
	case TOK_IDENT: printf("%s", tok.val.s.str); break;
	case TOK_STR: printf("\"%s\"", tok.val.s.str); break;
	case TOK_CHAR: printf("'%lc'", (wint_t)tok.val.i); break;
	case TOK_INT: printf("%llu", tok.val.i); break;
	case TOK_FLOAT: printf("%f", tok.val.f); break;
	case TOK_BOOL: printf("%s", tok.val.i ? "true" : "false"); break;
	case TOK_ARROW: printf("->"); break;
	case TOK_LSHIFT: printf("<<"); break;
	case TOK_RSHIFT: printf(">>"); break;
	case TOK_AND: printf("&&"); break;
	case TOK_OR: printf("||"); break;
	case TOK_EQ: printf("=="); break;
	case TOK_NEQ: printf("!="); break;
	case TOK_GTE: printf(">="); break;
	case TOK_LTE: printf("<="); break;
	case TOK_ADD_ASS: printf("+="); break;
	case TOK_SUB_ASS: printf("-="); break;
	case TOK_MUL_ASS: printf("*="); break;
	case TOK_DIV_ASS: printf("/="); break;
	case TOK_MOD_ASS: printf("*="); break;
	case TOK_BITAND_ASS: printf("&="); break;
	case TOK_BITOR_ASS: printf("|="); break;
	case TOK_BITXOR_ASS: printf("^="); break;
	case TOK_LSHIFT_ASS: printf("<<="); break;
	case TOK_RSHIFT_ASS: printf(">>="); break;
	default: putchar(tok.type);
	}
}

void
PrintType(data_type type)
{
	switch (type.kind) {
	case TYPE_UINT: printf("u%hhu", type.width); break;
	case TYPE_INT: printf("i%hhu", type.width); break;
	case TYPE_FLOAT: printf("f%hhu", type.width); break;
	case TYPE_CHAR: printf("char"); break;
	}
}

void
PrintExpr(expr_header *expr)
{
	switch (expr->type) {
	case EXPR_INT: printf("%llu", ((int_expr *)expr)->value); break;
	case EXPR_FLOAT: printf("%f", ((float_expr *)expr)->value); break;
	case EXPR_BOOL: printf("%s", ((bool_expr *)expr)->value ? "true" : "false"); break;
	case EXPR_CHAR: printf("'%lc'", (wint_t)((char_expr *)expr)->value); break;
	case EXPR_STR: printf("\"%s\"", ((str_expr *)expr)->value.str); break;
	case EXPR_IDENT: printf("%s", ((ident_expr *)expr)->value.str); break;
	case EXPR_MEMBER: {
		member_expr *member = (member_expr *)expr;
		printf("(");
		PrintExpr(member->child);
		printf(").%s", member->member.str);
	} break;
	case EXPR_CALL: {
		call_expr *call = (call_expr *)expr;
		unsigned int i;
		printf("(");
		PrintExpr(call->func);
		printf(")(");
		for (i = 0; i < call->args.len; i++) {
			PrintExpr(call->args.data[i]);
			if (i != call->args.len - 1)
				printf(", ");
		}
		printf(")");
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
	case EXPR_FUNC: {
		func_expr *func = (func_expr *)expr;
		unsigned int i;
		printf("(");
		for (i = 0; i < func->params.len; i++) {
			printf("%s: ", func->params.data[i].name.str);
			PrintType(func->params.data[i].type);
			if (i != func->params.len - 1)
				printf(", ");
		}
		printf(") ");
		if (func->returnType.kind != TYPE_VOID) {
			PrintType(func->returnType);
			printf(" ");
		}
		printf("{ ");
		for (i = 0; i < func->statements.len; i++) {
			PrintStmt(func->statements.data[i]);
			printf(" ");
		}
		printf("}");
	} break;
	}
}

void
PrintDecl(declaration *decl)
{
	printf("%s :", decl->name.str);
	if (decl->type.kind != TYPE_INFERRED) {
		printf(" ");
		PrintType(decl->type);
		printf(" ");
	}
	if (decl->value) {
		if (decl->isConst)
			printf(": ");
		else
			printf("= ");
		PrintExpr(decl->value);
	}
	printf(";");
}

void
PrintStmt(stmt_header *stmt)
{
	if (!stmt) {
		printf(";");
		return;
	}

	switch (stmt->type) {
	case STMT_EXPR:
		PrintExpr(((expr_stmt *)stmt)->expr);
		printf(";");
		break;
	case STMT_RETURN:
		printf("return ");
		PrintExpr(((return_stmt *)stmt)->expr);
		printf(";");
		break;
	case STMT_BREAK:
		printf("break;");
		break;
	case STMT_CONTINUE:
		printf("continue;");
		break;
	case STMT_IF: {
		if_stmt *ifStmt = (if_stmt *)stmt;
		printf("if ");
		PrintExpr(ifStmt->condition);
		printf(" ");
		PrintStmt(ifStmt->if_branch);
		if (ifStmt->else_branch) {
			printf(" else ");
			PrintStmt(ifStmt->else_branch);
		}
	} break;
	case STMT_WHILE: {
		while_stmt *whileStmt = (while_stmt *)stmt;
		printf("while ");
		PrintExpr(whileStmt->condition);
		printf(" ");
		PrintStmt(whileStmt->statement);
	} break;
	case STMT_BLOCK: {
		block_stmt *blockStmt = (block_stmt *)stmt;
		unsigned int i;
		printf("{ ");
		for (i = 0; i < blockStmt->statements.len; i++) {
			PrintStmt(blockStmt->statements.data[i]);
			printf(" ");
		}
		printf("}");
	} break;
	case STMT_DECL:
		PrintDecl(&((decl_stmt *)stmt)->decl);
		break;
	}
}
