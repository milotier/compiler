#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "typer.h"

typedef ArrayType(Declaration *) DeclList;

static void checkDecl(
    Declaration *decl,
    Context *ctx,
    Scope *scope,
    DeclList *visitedDecls
);

static DataType *
getExprDataType(
    ExprHeader *expr,
    Context *ctx,
    Scope *scope,
    DeclList *visitedDecls
) {
    DataType type = {0};
    switch (expr->type) {
    case EXPR_INT:
        type.kind = TYPE_INT;
        type.isFromLiteral = 1;
        type.width = 32;
        type.isSigned = 1;
        break;
    case EXPR_FLOAT:
        type.kind = TYPE_FLOAT;
        type.isFromLiteral = 1;
        type.width = 32;
        break;
    case EXPR_BOOL:
        type.kind = TYPE_BOOL;
        type.isFromLiteral = 1;
        break;
    case EXPR_STR: /*TODO*/ break;

    case EXPR_BINOP: {
        BinopExpr *binop = (BinopExpr *)expr;
        if (binop->type >= BINOP_FIRST_NON_ASS &&
            binop->type <= BINOP_LAST_NON_ASS)
        {
            DataType *leftType = getExprDataType(binop->left, ctx, scope, visitedDecls);
            DataType *rightType = getExprDataType(binop->right, ctx, scope, visitedDecls);
            if (leftType->kind != rightType->kind)
                error(ctx, expr->pos,
                      "infix operators used on values in different type families");
            if (leftType->kind == TYPE_INT) {
                type.kind = TYPE_INT;

                if (binop->type >= BINOP_FIRST_LOG &&
                    binop->type <= BINOP_LAST_LOG)
                {
                    error(ctx, expr->pos,
                          "integers used in logical operator");
                }

                if (leftType->isFromLiteral) {
                    leftType->width = rightType->width;
                    leftType->isSigned = rightType->isSigned;
                } else if (rightType->isFromLiteral) {
                    rightType->width = leftType->width;
                    rightType->isSigned = leftType->isSigned;
                }
                if (leftType->isFromLiteral && rightType->isFromLiteral)
                    type.isFromLiteral = 1;

                if (leftType->isSigned != rightType->isSigned)
                    error(ctx, expr->pos,
                          "infix used operators on integers of different signs");
                type.isSigned = leftType->isSigned;

                if (leftType->width != rightType->width)
                    error(ctx, expr->pos,
                          "infix operators used on integers of different widths");
                type.width = leftType->width;
            } else if (leftType->kind == TYPE_FLOAT) {
                type.kind = TYPE_FLOAT;

                if (binop->type >= BINOP_FIRST_BINARY &&
                    binop->type <= BINOP_LAST_BINARY)
                {
                    error(ctx, expr->pos,
                          "floating-point numbers used in bitwise operator");
                }
                if (binop->type >= BINOP_FIRST_LOG &&
                    binop->type <= BINOP_LAST_LOG)
                {
                    error(ctx, expr->pos,
                          "floating-point numbers used in logical operator");
                }

                if (leftType->isFromLiteral)
                    leftType->width = rightType->width;
                else if (rightType->isFromLiteral)
                    rightType->width = leftType->width;
                if (leftType->isFromLiteral && rightType->isFromLiteral)
                    type.isFromLiteral = 1;
                if (leftType->width != rightType->width)
                    error(ctx, expr->pos,
                          "infix operator used on floating-point numbers of different widths");
                type.width = leftType->width;
            } else if (leftType->kind == TYPE_BOOL) {
                type.kind = TYPE_BOOL;
                if (binop->type >= BINOP_FIRST_ARITH &&
                     binop->type <= BINOP_LAST_ARITH)
                {
                    error(ctx, expr->pos, "booleans used in arithmetic operator");
                }
                if (binop->type >= BINOP_FIRST_BINARY &&
                     binop->type <= BINOP_LAST_BINARY)
                {
                    error(ctx, expr->pos, "booleans used in bitwise operator");
                }
            } else {
                error(ctx, expr->pos,
                      "invalid type for arithmetic infix operator");
            }

            if (binop->type >= BINOP_FIRST_CMP &&
                binop->type <= BINOP_LAST_CMP)
            {
                type.kind = TYPE_BOOL;
            }
        }
    } break;

    case EXPR_IDENT: {
        Declaration *decl = scopeGet(scope, ((IdentExpr *)expr)->value);
        unsigned int i;
        if (!decl)
            error(ctx, expr->pos, "undeclared identifier");
        for (i = 0; i < visitedDecls->len; i++)
            if (visitedDecls->data[i] == decl)
                error(ctx, decl->pos, "recursive declaration");
        arrayAdd(visitedDecls, decl);

        if (decl->type.kind == TYPE_INFERRED) {
            if (scope != &ctx->topScope)
                error(ctx, expr->pos, "undeclared identifier");
            checkDecl(decl, ctx, scope, visitedDecls);
        }

        type = decl->type;
    } break;
    }

    expr->dataType = type;
    return &expr->dataType;
}

static void
checkDecl(
    Declaration *decl,
    Context *ctx,
    Scope *scope,
    DeclList *visitedDecls
) {
    DataType *valueType = getExprDataType(decl->value, ctx, scope, visitedDecls);

    if (decl->type.kind == TYPE_INFERRED)
        decl->type = *valueType;

    if (decl->type.kind != valueType->kind)
        error(ctx, decl->pos, "assigned value does not match type");
    if (decl->type.kind == TYPE_INT || decl->type.kind == TYPE_FLOAT) {
        if (!valueType->isFromLiteral && decl->type.width < valueType->width)
            error(ctx, decl->pos, "width of assigned value does not match type");
        if (!valueType->isFromLiteral && decl->type.isSigned != valueType->isSigned)
            error(ctx, decl->pos, "sign of assigned value does not match type");
    }

    decl->type.isFromLiteral = 0;
}

void
checkTypes(Context *ctx) {
    unsigned int i;
    for (i = 0; i < ctx->topScope.value.tbl.cap; i++) {
        if (ctx->topScope.value.tbl.entries[i].sym.str) {
            Declaration *decl = ctx->topScope.value.tbl.entries[i].val;
            DeclList visitedDecls = {0};
            arrayAdd(&visitedDecls, decl);
            checkDecl(decl, ctx, &ctx->topScope, &visitedDecls);
        }
    }
}
