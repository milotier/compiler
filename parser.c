#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "lexer.h"
#include "parser.h"

static ExprHeader *parseExpr(Context *, Scope *);
static StmtHeader *parseStmt(Context *, Scope *);

/* function implementations */
#define EXPR_ALLOC_FUNC(lower, upper) \
static lower##Expr * \
alloc##lower##Expr(void) \
{ \
    lower##Expr *expr = xMalloc(sizeof(*expr)); \
    *expr = (lower##Expr) {0}; \
    expr->header.type = EXPR_##upper; \
    return expr; \
}

EXPR_ALLOC_FUNC(Int, INT);
EXPR_ALLOC_FUNC(Float, FLOAT);
EXPR_ALLOC_FUNC(Bool, BOOL);
EXPR_ALLOC_FUNC(Char, CHAR);
EXPR_ALLOC_FUNC(Str, STR);
EXPR_ALLOC_FUNC(Member, MEMBER);
EXPR_ALLOC_FUNC(Call, CALL);
EXPR_ALLOC_FUNC(Ident, IDENT);
EXPR_ALLOC_FUNC(Unop, UNOP);
EXPR_ALLOC_FUNC(Binop, BINOP);
EXPR_ALLOC_FUNC(Func, FUNC);

#define STMT_ALLOC_FUNC(lower, upper) \
static lower##Stmt * \
alloc##lower##Stmt(void) \
{ \
    lower##Stmt *stmt = xMalloc(sizeof(*stmt)); \
    *stmt = (lower##Stmt) {0}; \
    stmt->header.type = STMT_##upper; \
    return stmt; \
}

STMT_ALLOC_FUNC(Expr, EXPR);
STMT_ALLOC_FUNC(Return, RETURN);
STMT_ALLOC_FUNC(If, IF);
STMT_ALLOC_FUNC(While, WHILE);
STMT_ALLOC_FUNC(Block, BLOCK);
STMT_ALLOC_FUNC(Decl, DECL);

static DataType
parseType(Context *ctx) {
    DataType type = {0};
    Token tok = nextToken(ctx);

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
    default: error(ctx, tok.pos, "expected type");
    }

    return type;
}

static Declaration
parseDecl(Context *ctx, Scope *scope) {
    Declaration decl = {0};
    Token tok = nextToken(ctx);
    if (tok.type != TOK_IDENT)
        error(ctx, tok.pos, "expected identifier in Declaration");
    decl.name = tok.val.s;
    tok = nextToken(ctx);
    if (tok.type != ':')
        error(ctx, tok.pos, "expected colon in Declaration");
    decl.pos = tok.pos;

    tok = peekToken(1, ctx);
    if (tok.type != '=' && tok.type != ':')
        decl.type = parseType(ctx);
    tok = nextToken(ctx);
    if (tok.type == '=' || tok.type == ':') {
        if (tok.type == ':')
            decl.isConst = 1;
        decl.value = parseExpr(ctx, scope);
    }

    if (!(decl.value && decl.value->type == EXPR_FUNC)) {
        tok = nextToken(ctx);
        if (tok.type != ';')
            error(ctx, tok.pos, "expected semicolon to end Declaration");
    }

    return decl;
}

#define HANDLE_TOKEN(TOK_TYPE, ExprType, val) \
    case TOK_TYPE: { \
        ExprType *expr = alloc##ExprType(); \
        expr->header.pos = tok.pos; \
        expr->value = val; \
        return (ExprHeader *)expr; \
    } break;

static ExprHeader *
parseSingularExpr(Context *ctx, Scope *scope) {
    Token tok = nextToken(ctx);
    switch (tok.type) {
    case '(': {
        if ((peekToken(1, ctx).type == TOK_IDENT &&
             peekToken(2, ctx).type == ':') ||
            peekToken(1, ctx).type == ')') {
            FuncExpr *func = allocFuncExpr();
            func->header.pos = tok.pos;
            func->scope.parent = scope;

            while (tok.type != ')') {
                Declaration decl = {0};
                tok = nextToken(ctx);
                if (tok.type != TOK_IDENT)
                    error(ctx, tok.pos,
                          "expected parameter name");
                decl.name = tok.val.s;
                tok = nextToken(ctx);
                if (tok.type != ':')
                    error(ctx, tok.pos,
                          "expected colon in parameter Declaration");
                decl.pos = tok.pos;
                decl.type = parseType(ctx);

                arrayAdd(&func->params, decl);
                scopeAdd(&func->scope,
                     &func->params.data[func->params.len - 1]);

                tok = nextToken(ctx);
                if (tok.type == ',' && peekToken(1, ctx).type == ')') {
                    nextToken(ctx);
                    break;
                }
                if (tok.type != ',' && tok.type != ')')
                    error(ctx, tok.pos,
                          "expected comma or closing parenthesis");
           }

            tok = peekToken(1, ctx);
            if (tok.type == '{') {
                nextToken(ctx);
                func->returnType.kind = TYPE_VOID;
            } else {
                func->returnType = parseType(ctx);
                tok = nextToken(ctx);
                if (tok.type != '{')
                    error(ctx, tok.pos, "expected opening brace");
            }

            while (peekToken(1, ctx).type != '}') {
                StmtHeader *stmt = parseStmt(ctx, &func->scope);
                if (stmt)
                    arrayAdd(&func->statements, stmt);
            }
            nextToken(ctx);

            return (ExprHeader *)func;
        } else {
            ExprHeader *expr = parseExpr(ctx, scope);
            Token next = nextToken(ctx);
            if (next.type != ')')
                error(ctx, next.pos, "expected closing parenthesis");
            if (expr->isParenthesized)
                warn(ctx, tok.pos, "useless parentheses");
            expr->isParenthesized = 1;
            return expr;
        }
    } break;
    HANDLE_TOKEN(TOK_INT, IntExpr, tok.val.i)
    HANDLE_TOKEN(TOK_FLOAT, FloatExpr, tok.val.f)
    HANDLE_TOKEN(TOK_BOOL, BoolExpr, (unsigned int)tok.val.i)
    HANDLE_TOKEN(TOK_CHAR, CharExpr, (unsigned int)tok.val.i)
    HANDLE_TOKEN(TOK_STR, StrExpr, tok.val.s)
    HANDLE_TOKEN(TOK_IDENT, IdentExpr, tok.val.s)
    default:
        error(ctx, tok.pos, "unexpected Token found while parsing expression");
    }
}

static ExprHeader *
parsePostfixExpr(Context *ctx, Scope *scope) {
    ExprHeader *child = parseSingularExpr(ctx, scope);
    ExprHeader *expr = child;
    Token tok = peekToken(1, ctx);

    while (tok.type == '^' || tok.type == '.' || tok.type == '[' || tok.type == '(') {
        if (tok.type == '^') {
            char nextType = peekToken(2, ctx).type;
            /* Check wether the caret is being used as
             * a bitwise xor operator */
            if (nextType == '(' || (nextType >= FIRST_LIT_TOK &&
                        nextType <= LAST_LIT_TOK))
                break;

            nextToken(ctx);
            expr = (ExprHeader *)allocUnopExpr();
            expr->pos = tok.pos;
            ((UnopExpr *)expr)->type = UNOP_DEREF;
            ((UnopExpr *)expr)->child = child;
            child = expr;
        } else if (tok.type == '.') {
            nextToken(ctx);
            expr = (ExprHeader *)allocMemberExpr();
            expr->pos = tok.pos;
            ((MemberExpr *)expr)->child = child;
            tok = nextToken(ctx);
            if (tok.type != TOK_IDENT)
                error(ctx, tok.pos, "expected member name after '.'");
            ((MemberExpr *)expr)->member = tok.val.s;
        } else if (tok.type == '[') {
            nextToken(ctx);
            expr = (ExprHeader *)allocBinopExpr();
            expr->pos = tok.pos;
            ((BinopExpr *)expr)->type = BINOP_INDEX;
            ((BinopExpr *)expr)->left = child;
            ((BinopExpr *)expr)->right = parseExpr(ctx, scope);
            tok = nextToken(ctx);
            if (tok.type != ']')
                error(ctx, tok.pos, "expected closing bracket");
        } else {
            nextToken(ctx);
            expr = (ExprHeader *)allocCallExpr();
            expr->pos = child->pos;
            ((CallExpr *)expr)->func = child;
            while (tok.type != ')') {
                arrayAdd(&((CallExpr *)expr)->args, parseExpr(ctx, scope));
                tok = nextToken(ctx);
                if (tok.type == ',' && peekToken(1, ctx).type == ')') {
                    nextToken(ctx);
                    break;
                }
                if (tok.type != ',' && tok.type != ')')
                    error(ctx, tok.pos, "expected comma or closing parenthesis");
            }
        }

        tok = peekToken(1, ctx);
    }

    return expr;
}

static ExprHeader *
parsePrefixExpr(Context *ctx, Scope *scope) {
    Token tok = peekToken(1, ctx);

    if (tok.type == '&') {
        nextToken(ctx);
        UnopExpr *expr = allocUnopExpr();
        expr->header.pos = tok.pos;
        expr->type = UNOP_ADDR_OF;
        expr->child = parsePrefixExpr(ctx, scope);
        return (ExprHeader *)expr;
    }
    if (tok.type == '!') {
        nextToken(ctx);
        UnopExpr *expr = allocUnopExpr();
        expr->header.pos = tok.pos;
        expr->type = UNOP_NOT;
        expr->child = parsePrefixExpr(ctx, scope);
        return (ExprHeader *)expr;
    }

    return parsePostfixExpr(ctx, scope);
}

static unsigned int
BinopPrecedence(unsigned int type) {
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

static ExprHeader *
parseExpr(Context *ctx, Scope *scope) {
    ExprHeader *expr = parsePrefixExpr(ctx, scope);
    Token tok = peekToken(1, ctx);
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
        ExprHeader *left = expr;
        ExprHeader *right;
        BinopExpr *exprBinop;

        nextToken(ctx);
        right = parseExpr(ctx, scope);

        expr = (ExprHeader *)allocBinopExpr();
        expr->pos = tok.pos;
        exprBinop = (BinopExpr *)expr;
        exprBinop->type = (unsigned int)type;
        exprBinop->left = left;
        exprBinop->right = right;
        /* Switch around members of expr and right so that it aligns with
         * their precedences */
        if (right->type == EXPR_BINOP &&
            ((BinopExpr *)right)->type != BINOP_INDEX &&
            !right->isParenthesized) {
            BinopExpr *rightBinop = (BinopExpr *)right;
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

static StmtHeader *
parseStmt(Context *ctx, Scope *scope) {
    Token tok = peekToken(1, ctx);
    StmtHeader *stmt;

    if (tok.type == ';') {
        nextToken(ctx);
        return NULL;
    } else if (tok.type == TOK_BREAK) {
        nextToken(ctx);
        stmt = xMalloc(sizeof(*stmt));
        stmt->type = STMT_BREAK;
        stmt->pos = tok.pos;
        tok = nextToken(ctx);
        if (tok.type != ';')
            error(ctx, tok.pos, "expected semicolon to end break statement");
    } else if (tok.type == TOK_CONTINUE) {
        nextToken(ctx);
        stmt = xMalloc(sizeof(*stmt));
        stmt->type = STMT_CONTINUE;
        stmt->pos = tok.pos;
        tok = nextToken(ctx);
        if (tok.type != ';')
            error(ctx, tok.pos, "expected semicolon to end continue statement");
    } else if (tok.type == TOK_RETURN) {
        nextToken(ctx);
        stmt = (StmtHeader *)allocReturnStmt();
        stmt->pos = tok.pos;
        ((ReturnStmt *)stmt)->expr = parseExpr(ctx, scope);
        tok = nextToken(ctx);
        if (tok.type != ';')
            error(ctx, tok.pos, "expected semicolon to end return statement");
    } else if (tok.type == TOK_IF) {
        nextToken(ctx);
        stmt = (StmtHeader *)allocIfStmt();
        stmt->pos = tok.pos;
        ((IfStmt *)stmt)->condition = parseExpr(ctx, scope);
        ((IfStmt *)stmt)->ifBranch = parseStmt(ctx, scope);

        tok = peekToken(1, ctx);
        if (tok.type == TOK_ELSE) {
            nextToken(ctx);
            ((IfStmt *)stmt)->elseBranch = parseStmt(ctx, scope);
        }
    } else if (tok.type == TOK_WHILE) {
        nextToken(ctx);
        stmt = (StmtHeader *)allocWhileStmt();
        stmt->pos = tok.pos;
        ((WhileStmt *)stmt)->condition = parseExpr(ctx, scope);
        ((WhileStmt *)stmt)->statement = parseStmt(ctx, scope);
    } else if (tok.type == '{') {
        BlockStmt *blockStmt;
        nextToken(ctx);
        stmt = (StmtHeader *)allocBlockStmt();
        blockStmt = (BlockStmt *)stmt;
        stmt->pos = tok.pos;
        blockStmt->scope.parent = scope;
        while (peekToken(1, ctx).type != '}') {
            StmtHeader *childStmt = parseStmt(ctx, &blockStmt->scope);
            if (childStmt)
                arrayAdd(&((BlockStmt *)stmt)->statements,childStmt);
        }
        nextToken(ctx);
    } else if (tok.type == TOK_IDENT && peekToken(2, ctx).type == ':') {
        DeclStmt *declStmt;
        stmt = (StmtHeader *)allocDeclStmt();
        declStmt = (DeclStmt *)stmt;
        declStmt->decl = parseDecl(ctx, scope);
        stmt->pos = declStmt->decl.pos;

        if (scopeGetNoParent(scope, declStmt->decl.name))
            error(ctx, stmt->pos, "reDeclaration of %s", declStmt->decl.name.str);
        scopeAdd(scope, &declStmt->decl);
    } else {
        stmt = (StmtHeader *)allocExprStmt();
        ((ExprStmt *)stmt)->expr = parseExpr(ctx, scope);
        stmt->pos = ((ExprStmt *)stmt)->expr->pos;
        tok = nextToken(ctx);
        if (tok.type != ';')
            error(ctx, tok.pos, "expected semicolon to end expression statement");
    }

    return stmt;
}

void
parse(Context *ctx) {
    while (peekToken(1, ctx).type != TOK_EOF) {
        Declaration *decl = xMalloc(sizeof(*decl));
        *decl = parseDecl(ctx, &ctx->topScope);
        if (scopeGetNoParent(&ctx->topScope, decl->name))
            error(ctx, decl->pos, "redeclaration of %s", decl->name.str);
        scopeAdd(&ctx->topScope, decl);
    }
}
