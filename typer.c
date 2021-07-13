#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "typer.h"

#define HANDLE_BINOP(BINOP, op, ExprType) \
    case BINOP: {\
        ExprType *result = alloc##ExprType(); \
        result->value = ((ExprType *)binop->left)->value op \
                        ((ExprType *)binop->right)->value; \
        *exprPtr = (ExprHeader *)result; \
    } break;

#define HANDLE_INT_BINOP(BINOP, func) \
    case BINOP: {\
        IntExpr *result = allocIntExpr(); \
        func(&result->value, leftValue, rightValue); \
        *exprPtr = (ExprHeader *)result; \
    } break;

typedef ArrayType(Declaration *) DeclList;

static void checkDecl(
    Declaration *decl,
    Context *ctx,
    Scope *scope,
    DeclList *visitedDecls
);

static DataType *checkExpr(
    ExprHeader **exprPtr,
    Context *ctx,
    Scope *scope,
    DeclList *visitedDecls
);

static int
getTypeFamily(DataType *type) {
    if (type->kind == TYPE_COMPTIME_INT)
        return TYPE_INT;
    if (type->kind == TYPE_COMPTIME_FLOAT)
        return TYPE_FLOAT;
    return type->kind;
}

static DataType
checkBinop(
    ExprHeader **exprPtr,
    Context *ctx,
    Scope *scope,
    DeclList *visitedDecls
) {
    BinopExpr *binop = (BinopExpr *)*exprPtr;
    DataType type = {0};

    if (binop->type >= BINOP_FIRST_NON_ASS && binop->type <= BINOP_LAST_NON_ASS) {
        DataType *leftType = checkExpr(&binop->left,
                                             ctx,
                                             scope,
                                             visitedDecls);
        DataType *rightType = checkExpr(&binop->right,
                                              ctx,
                                              scope,
                                              visitedDecls);
        if (getTypeFamily(leftType) != getTypeFamily(rightType))
            error(ctx, binop->header.pos,
                  "infix operators used on values in different type families");
        if (getTypeFamily(leftType) == TYPE_INT) {
            if (binop->type >= BINOP_FIRST_LOG && binop->type <= BINOP_LAST_LOG)
                error(ctx, binop->header.pos, "integers used in logical operator");

            if (binop->left->type == EXPR_INT && binop->right->type == EXPR_INT) {
                BigInt *leftValue = &((IntExpr *)binop->left)->value;
                BigInt *rightValue = &((IntExpr *)binop->right)->value;

                switch (binop->type) {
                    HANDLE_INT_BINOP(BINOP_ADD, bigIntAdd)
                    HANDLE_INT_BINOP(BINOP_SUB, bigIntSub)
                    HANDLE_INT_BINOP(BINOP_MUL, bigIntMul)
                    HANDLE_INT_BINOP(BINOP_BITAND, bigIntAnd)
                    HANDLE_INT_BINOP(BINOP_BITOR, bigIntOr)
                    HANDLE_INT_BINOP(BINOP_BITXOR, bigIntXor)
                    HANDLE_INT_BINOP(BINOP_RSHIFT, bigIntShr)
                    case BINOP_LSHIFT: {
                        IntExpr *result = allocIntExpr();
                        if (!bigIntShl(&result->value, leftValue, rightValue))
                            error(ctx, binop->header.pos, "left-shift overflows memory");
                        *exprPtr = (ExprHeader *)result;
                    } break;
                    case BINOP_DIV: {
                        IntExpr *result = allocIntExpr();
                        if (!bigIntDiv(&result->value, NULL, leftValue, rightValue))
                            error(ctx, binop->header.pos, "division by zero");
                        *exprPtr = (ExprHeader *)result;
                    } break;
                    case BINOP_MOD: {
                        IntExpr *result = allocIntExpr();
                        if (!bigIntDiv(NULL, &result->value, leftValue, rightValue))
                            error(ctx, binop->header.pos, "division by zero");
                        *exprPtr = (ExprHeader *)result;
                    } break;
                    case BINOP_EQ: {
                        BoolExpr *result = allocBoolExpr();
                        result->value = bigIntCmp(leftValue, rightValue) == 0;
                        *exprPtr = (ExprHeader *)result;
                    } break;
                    case BINOP_LT: {
                        BoolExpr *result = allocBoolExpr();
                        result->value = bigIntCmp(leftValue, rightValue) < 0;
                        *exprPtr = (ExprHeader *)result;
                    } break;
                    case BINOP_LTE: {
                        BoolExpr *result = allocBoolExpr();
                        result->value = bigIntCmp(leftValue, rightValue) <= 0;
                        *exprPtr = (ExprHeader *)result;
                    } break;
                    case BINOP_GT: {
                        BoolExpr *result = allocBoolExpr();
                        result->value = bigIntCmp(leftValue, rightValue) > 0;
                        *exprPtr = (ExprHeader *)result;
                    } break;
                    case BINOP_GTE: {
                        BoolExpr *result = allocBoolExpr();
                        result->value = bigIntCmp(leftValue, rightValue) >= 0;
                        *exprPtr = (ExprHeader *)result;
                    } break;
                }
            }

            if (leftType->kind == TYPE_COMPTIME_INT && rightType->kind == TYPE_COMPTIME_INT) {
                type.kind = TYPE_COMPTIME_INT;
            } else {
                type.kind = TYPE_INT;

                if (leftType->kind == TYPE_COMPTIME_INT)
                    SWAP(DataType *, leftType, rightType);

                if (rightType->kind == TYPE_COMPTIME_INT) {
                    rightType->width = leftType->width;
                    rightType->isSigned = leftType->isSigned;
                }

                if (leftType->isSigned != rightType->isSigned)
                    error(ctx, binop->header.pos,
                          "infix operators used on integers of different signs");
                type.isSigned = leftType->isSigned;

                if (leftType->width != rightType->width)
                    error(ctx, binop->header.pos,
                          "infix operators used on integers of different widths");
                type.width = leftType->width;
            }
        } else if (getTypeFamily(leftType) == TYPE_FLOAT) {
            if (leftType->kind == TYPE_COMPTIME_FLOAT &&
                rightType->kind == TYPE_COMPTIME_FLOAT)
            {
                type.kind = TYPE_COMPTIME_FLOAT;
            } else {
                type.kind = TYPE_FLOAT;
            }

            if (binop->type >= BINOP_FIRST_BINARY &&
                binop->type <= BINOP_LAST_BINARY)
            {
                error(ctx, binop->header.pos,
                      "floating-point numbers used in bitwise operator");
            }
            if (binop->type >= BINOP_FIRST_LOG &&
                binop->type <= BINOP_LAST_LOG)
            {
                error(ctx, binop->header.pos,
                      "floating-point numbers used in logical operator");
            }
            if (binop->type == BINOP_MOD)
                error(ctx, binop->header.pos,
                      "floating-point numbers used in modulo operator");

            if (leftType->kind == TYPE_COMPTIME_FLOAT)
                SWAP(DataType *, leftType, rightType);
            if (rightType->kind == TYPE_COMPTIME_FLOAT)
                rightType->width = leftType->width;
            if (leftType->width != rightType->width)
                error(ctx, binop->header.pos,
                      "infix operator used on floating-point numbers of different widths");

            if (binop->left->type == EXPR_FLOAT && binop->right->type == EXPR_FLOAT) {
                switch (binop->type) {
                    HANDLE_BINOP(BINOP_ADD, +, FloatExpr)
                    HANDLE_BINOP(BINOP_SUB, -, FloatExpr)
                    HANDLE_BINOP(BINOP_MUL, *, FloatExpr)
                    HANDLE_BINOP(BINOP_DIV, /, FloatExpr)
                    HANDLE_BINOP(BINOP_EQ, ==, BoolExpr)
                    HANDLE_BINOP(BINOP_NEQ, !=, BoolExpr)
                    HANDLE_BINOP(BINOP_LT, <, BoolExpr)
                    HANDLE_BINOP(BINOP_LTE, <=, BoolExpr)
                    HANDLE_BINOP(BINOP_GT, >, BoolExpr)
                    HANDLE_BINOP(BINOP_GTE, >=, BoolExpr)
                }
            }

            type.width = leftType->width;
        } else if (leftType->kind == TYPE_BOOL) {
            type.kind = TYPE_BOOL;
            if (binop->type >= BINOP_FIRST_ARITH &&
                binop->type <= BINOP_LAST_ARITH)
            {
                error(ctx, binop->header.pos, "booleans used in arithmetic operator");
            }
            if (binop->type >= BINOP_FIRST_BINARY &&
                binop->type <= BINOP_LAST_BINARY)
            {
                error(ctx, binop->header.pos, "booleans used in bitwise operator");
            }

            if (binop->left->type == EXPR_BOOL &&
                binop->right->type == EXPR_BOOL)
            {
                switch (binop->type) {
                    HANDLE_BINOP(BINOP_AND, &&, BoolExpr)
                    HANDLE_BINOP(BINOP_OR, ||, BoolExpr)
                    HANDLE_BINOP(BINOP_EQ, ==, BoolExpr)
                    HANDLE_BINOP(BINOP_NEQ, !=, BoolExpr)
                    HANDLE_BINOP(BINOP_LT, <, BoolExpr)
                    HANDLE_BINOP(BINOP_LTE, <=, BoolExpr)
                    HANDLE_BINOP(BINOP_GT, >, BoolExpr)
                    HANDLE_BINOP(BINOP_GTE, >=, BoolExpr)
                }
            }
        } else {
            error(ctx, binop->header.pos, "invalid type for arithmetic infix operator");
        }

        if (binop->type >= BINOP_FIRST_CMP &&
            binop->type <= BINOP_LAST_CMP)
        {
            type.kind = TYPE_BOOL;
        }
    }

    return type;
}

#include <stdio.h>
static DataType
checkUnop(
    ExprHeader **exprPtr,
    Context *ctx,
    Scope *scope,
    DeclList *visitedDecls
) {
    UnopExpr *unop = (UnopExpr *)*exprPtr;
    DataType *childType = checkExpr(&unop->child, ctx, scope, visitedDecls);
    DataType type = {0};

    switch (unop->type) {
    case UNOP_NOT: {
        int family = getTypeFamily(childType);
        if (!(family == TYPE_BOOL || family == TYPE_INT))
            error(ctx, unop->header.pos, "bitwise negation of value not of integer or boolean type");

        if (unop->child->type == EXPR_INT) {
            IntExpr *result = allocIntExpr();
            bigIntNot(&result->value, &((IntExpr *)unop->child)->value);
            *exprPtr = (ExprHeader *)result;
        } else {
            BoolExpr *result = allocBoolExpr();
            result->value = !((BoolExpr *)unop->child)->value;
            *exprPtr = (ExprHeader *)result;
        }

        type = *childType;
    } break;
    case UNOP_NEG: {
        int family = getTypeFamily(childType);
        if (!(family == TYPE_INT || family == TYPE_FLOAT))
            error(ctx, unop->header.pos, "negation of value of non-numeric type");
        if (childType->kind == TYPE_INT && !childType->isSigned)
            error(ctx, unop->header.pos, "negation of unsigned value");

        if (unop->child->type == EXPR_INT) {
            IntExpr *result = allocIntExpr();
            bigIntNeg(&result->value, &((IntExpr *)unop->child)->value);
            *exprPtr = (ExprHeader *)result;
        }

        type = *childType;
    } break;

    /* TODO */
    default: error(ctx, unop->header.pos, "");
    }

    return type;
}

static DataType *
checkExpr(
    ExprHeader **exprPtr,
    Context *ctx,
    Scope *scope,
    DeclList *visitedDecls
) {
    ExprHeader *expr = *exprPtr;
    DataType type = {0};
    switch (expr->type) {
    case EXPR_INT:
        type.kind = TYPE_COMPTIME_INT;
        break;
    case EXPR_FLOAT:
        type.kind = TYPE_COMPTIME_FLOAT;
        break;
    case EXPR_BOOL:
        type.kind = TYPE_BOOL;
        break;
    case EXPR_STR: /*TODO*/ break;

    case EXPR_UNOP:
        type = checkUnop(exprPtr, ctx, scope, visitedDecls);
        break;
    case EXPR_BINOP:
        type = checkBinop(exprPtr, ctx, scope, visitedDecls);
        break;

    case EXPR_IDENT: {
        Declaration *decl = scopeGet(scope, ((IdentExpr *)expr)->value);
        unsigned int i;
        if (!decl)
            error(ctx, expr->pos, "undeclared identifier");
        for (i = 0; i < visitedDecls->len; i++)
            if (visitedDecls->data[i] == decl)
                error(ctx, decl->pos, "recursive declaration");

        if (decl->type.kind == TYPE_INFERRED) {
            if (scope != &ctx->topScope)
                error(ctx, expr->pos, "undeclared identifier");
            arrayAdd(visitedDecls, decl);
            checkDecl(decl, ctx, scope, visitedDecls);
            arrayRemove(visitedDecls, visitedDecls->len - 1);
        }

        if (decl->isConst)
            *exprPtr = decl->value;

        type = decl->type;
    } break;
    }

    (*exprPtr)->dataType = type;
    return &(*exprPtr)->dataType;
}

static void
checkDecl(
    Declaration *decl,
    Context *ctx,
    Scope *scope,
    DeclList *visitedDecls
) {
    DataType *valueType = checkExpr(&decl->value, ctx, scope, visitedDecls);
    int typeInferred = 0;

    if (decl->type.kind == TYPE_INFERRED) {
        decl->type = *valueType;
        typeInferred = 1;
    }

    if (getTypeFamily(&decl->type) != getTypeFamily(valueType))
        error(ctx, decl->pos, "assigned value does not match type");

    if (decl->isConst &&
        (decl->value->type < EXPR_FIRST_LIT || decl->value->type > EXPR_LAST_LIT))
    {
        error(ctx, decl->pos, "constant declarations must have a constant value");
    }

    if ((decl->type.kind == TYPE_COMPTIME_INT || decl->type.kind == TYPE_COMPTIME_FLOAT) &&
        !decl->isConst)
    {
        if (typeInferred)
            error(ctx, decl->pos,
                  "variables assigned with a constant integer or float must be declared with a type");
        else
            error(ctx, decl->pos, "a variable cannot be declared with a compile-time type");
    }

    if (decl->type.kind == TYPE_INT && valueType->kind == TYPE_INT &&
        decl->type.width != valueType->width)
    {
        error(ctx, decl->pos, "width of assigned value does not match type");
    }

    if (decl->type.kind == TYPE_INT && decl->value->type == EXPR_INT) {
        BigInt *value = &((IntExpr *)decl->value)->value;
        if (decl->type.isSigned) {
            BigInt maxValue = bigIntFromI64((1ll << (decl->type.width - 1)) - 1);
            if (bigIntCmp(value, &maxValue) > 0)
                error(ctx, decl->pos, "assigned integer is too large");
        } else {
            BigInt maxValue = bigIntFromU64(decl->type.width == 64 ?
                                            ~0ull :
                                            (1ull << decl->type.width) - 1);
            if (bigIntCmp(value, &maxValue) > 0)
                error(ctx, decl->pos, "assigned integer is too large");
        }
    }
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
