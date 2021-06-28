#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "typer.h"

static DataType *
getExprDataType(ExprHeader *expr, Context *ctx) {
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
            DataType *leftType = getExprDataType(binop->left, ctx);
            DataType *rightType = getExprDataType(binop->right, ctx);
            if (leftType->kind != rightType->kind)
                error(ctx, expr->pos,
                      "can only add values of the same type family");
            if (leftType->kind == TYPE_INT) {
                type.kind = TYPE_INT;

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
                          "can not add integers of different signs");
                type.isSigned = leftType->isSigned;

                if (leftType->width != rightType->width)
                    error(ctx, expr->pos,
                          "can not add integers of different widths");
                type.width = leftType->width;
            } else if (leftType->kind == TYPE_FLOAT) {
                type.kind = TYPE_FLOAT;

                if (binop->type >= BINOP_FIRST_BINARY &&
                    binop->type <= BINOP_LAST_BINARY)
                {
                    error(ctx, expr->pos,
                          "floating-point numbers can not be used in bitwise operators");
                }

                if (leftType->isFromLiteral)
                    leftType->width = rightType->width;
                else if (rightType->isFromLiteral)
                    rightType->width = leftType->width;
                if (leftType->isFromLiteral && rightType->isFromLiteral)
                    type.isFromLiteral = 1;
                if (leftType->width != rightType->width)
                    error(ctx, expr->pos,
                          "can not add floating-point numbers of different widths");
                type.width = leftType->width;
            } else {
                error(ctx, expr->pos,
                      "invalid type for arithmetic infix operator");
            }
        }
    } break;

    case EXPR_IDENT: /*TODO*/ break;
    }

    expr->dataType = type;
    return &expr->dataType;
}

void
checkTypes(Context *ctx) {
    unsigned int i;
    for (i = 0; (unsigned int)i < ctx->topScope.value.tbl.cap; i++) {
        if (ctx->topScope.value.tbl.entries[i].sym.str) {
            Declaration *decl = ctx->topScope.value.tbl.entries[i].val;
            DataType *valueType = getExprDataType(decl->value, ctx);

            if (decl->type.kind == TYPE_INFERRED)
                decl->type = *valueType;

            if (decl->type.kind != valueType->kind)
                error(ctx, decl->pos, "assigned value does not match type");
            if (decl->type.kind == TYPE_INT || decl->type.kind == TYPE_FLOAT) {
                if (!valueType->isFromLiteral && decl->type.width < valueType->width)
                    error(ctx, decl->pos,
                          "width of assigned value does not match type");
                if (!valueType->isFromLiteral && decl->type.isSigned != valueType->isSigned)
                    error(ctx, decl->pos,
                          "sign of assigned value does not match type");
            }
        }
    }
}
