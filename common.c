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

    Symbol *symbols;
    unsigned int len, cap;
} symbolPool;
char *argv0;

/* function implementations */
void
die(char *fmt, ...) {
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
posToCoords(Context *ctx, unsigned int pos, unsigned int *line, unsigned int *col) {
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
error(Context *ctx, unsigned int pos, char *fmt, ...) {
    va_list args;
    unsigned int line, col;

    posToCoords(ctx, pos, &line, &col);
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
warn(Context *ctx, unsigned int pos, char *fmt, ...) {
    va_list args;
    unsigned int line, col;

    posToCoords(ctx, pos, &line, &col);
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
xMalloc(size_t len) {
    void *p = malloc(len);

    if (!p)
        die("malloc:");

    return p;
}

void *
xRealloc(void *p, size_t len) {
    p = realloc(p, len);
    if (!p)
        die("realloc:");

    return p;
}

static unsigned long
hashString(char *str) {
    unsigned long hash = 5381;
    unsigned int c;

    while ((c = (unsigned char)*str++))
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

    return hash;
}

static Symbol
rAddSymbol(char *str, unsigned int len, int isNew) {
    unsigned long i;

    if (symbolPool.len * 4 / 3 >= symbolPool.cap) {
        unsigned int j, oldcap = symbolPool.cap;
        Symbol *oldsyms = symbolPool.symbols;

        symbolPool.cap = MAX(symbolPool.cap * 2, 16u);
        symbolPool.symbols = xMalloc(sizeof(*symbolPool.symbols) * symbolPool.cap);
        for (j = 0; j < symbolPool.cap; j++)
            symbolPool.symbols[j].str = NULL;
        for (j = 0; j < oldcap; j++)
            if (oldsyms[j].str)
                rAddSymbol(oldsyms[j].str, oldsyms[j].len, 0);

        free(oldsyms);
    }

    symbolPool.len++;
    i = hashString(str) % symbolPool.cap;

    for (;;) {
        if (!symbolPool.symbols[i].str) {
            char *dest;

            if (isNew) {
                if (!symbolPool.symBuf ||
                     symbolPool.symBufLen + len >= SYMBUFLEN) {
                    symbolPool.symBuf = xMalloc(SYMBUFLEN);
                    symbolPool.symBufLen = 0;
                }

                dest = symbolPool.symBuf + symbolPool.symBufLen;
                memcpy(dest, str, len);
                dest[len] = '\0';
                symbolPool.symBufLen += len + 1;
            } else {
                dest = str;
            }

            return symbolPool.symbols[i] = (Symbol) {dest, len};
        } else if (strcmp(str, symbolPool.symbols[i].str) == 0) {
            return symbolPool.symbols[i];
        }

        i = (i + 1) % symbolPool.cap;
    }
}

Symbol
addSymbol(char *str, unsigned int len) {
    return rAddSymbol(str, len, 1);
}

void
stringAddS(String *str, char *data) {
    size_t dataLen = strlen(data);
    if (str->len + dataLen >= str->cap) {
        str->cap = MAX(MAX(str->cap * 2, 8),
                   str->len + (unsigned int)dataLen + 1);
        str->data = xRealloc(str->data, str->cap);
    }

    memcpy(str->data + str->len, data, dataLen);
    str->len += (unsigned int)dataLen;
    str->data[str->len] = '\0';
}

void
stringAddC(String *str, char c) {
    char data[2] = {c, '\0'};
    stringAddS(str, data);
}

void
stringFree(String *str) {
    free(str->data);
    *str = (String) {0};
}

void
tableAdd(Table *tbl, Symbol sym, void *val) {
    uintptr_t i;

    if (tbl->len * 4 / 3 >= tbl->cap) {
        unsigned int j;
        Table new = {.cap = MAX(tbl->cap * 2, 16)};

        new.entries = xMalloc(sizeof(*new.entries) * new.cap);
        for (j = 0; j < new.cap; j++)
            new.entries[j].sym.str = NULL;
        for (j = 0; j < tbl->cap; j++)
            if (tbl->entries[j].sym.str)
                tableAdd(&new, tbl->entries[j].sym,
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
tableGet(Table *tbl, Symbol sym) {
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
tableRemove(Table *tbl, Symbol sym, void (*freeFunc)(void *)) {
    uintptr_t i;

    /* Prevent dividing by zero */
    if (!tbl->cap)
        return;
    i = (uintptr_t)sym.str % tbl->cap;

    for (;;) {
        if (!tbl->entries[i].sym.str)
            return;
        if (sym.str == tbl->entries[i].sym.str) {
            tbl->entries[i].sym = (Symbol) {0};
            freeFunc(tbl->entries[i].val);
        }
        i = (i + 1) % tbl->cap;
    }
}

void
tableClear(Table *tbl, void (*freeFunc)(void *)) {
    unsigned int i;

    if (freeFunc) {
        for (i = 0; i < tbl->cap; i++) {
            tbl->entries[i].sym = (Symbol) {0};
            freeFunc(tbl->entries[i].val);
        }
    }

    tbl->len = 0;
}

void
tableFree(Table *tbl, void (*freeFunc)(void *)) {
    tableClear(tbl, freeFunc);
    tbl->entries = NULL;
    tbl->cap = 0;
}

void
scopeAdd(Scope *s, Declaration *decl) {
    if (!s->parent)
        tableAdd(&s->value.tbl, decl->name, decl);
    else
        arrayAdd(&s->value.arr, decl);
}

Declaration *
scopeGetNoParent(Scope *s, Symbol sym) {
    unsigned int i;
    if (!s->parent)
        return tableGet(&s->value.tbl, sym);
    for (i = 0; i < s->value.arr.len; i++) {
        if (s->value.arr.data[i]->name.str == sym.str)
            return s->value.arr.data[i];
    }
    return NULL;
}

Declaration *
scopeGet(Scope *s, Symbol sym) {
    while (s) {
        Declaration *decl = scopeGetNoParent(s, sym);
        if (decl)
            return decl;
        s = s->parent;
    }
    return NULL;
}

void
printToken(Token tok) {
    switch (tok.type) {
    case TOK_EOF: printf("EOF"); break;
    case TOK_IDENT: printf("%s", tok.val.s.str); break;
    case TOK_STR: printf("\"%s\"", tok.val.s.str); break;
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
printType(DataType type) {
    switch (type.kind) {
    case TYPE_INT: printf("%c%hhu", type.isSigned ? 'i' : 'u', type.width); break;
    case TYPE_FLOAT: printf("f%hhu", type.width); break;
    case TYPE_BOOL: printf("bool"); break;
    }
}

void
printExpr(ExprHeader *expr) {
    switch (expr->type) {
    case EXPR_INT: printf("%llu", ((IntExpr *)expr)->value); break;
    case EXPR_FLOAT: printf("%f", ((FloatExpr *)expr)->value); break;
    case EXPR_BOOL: printf("%s", ((BoolExpr *)expr)->value ? "true" : "false"); break;
    case EXPR_STR: printf("\"%s\"", ((StrExpr *)expr)->value.str); break;
    case EXPR_IDENT: printf("%s", ((IdentExpr *)expr)->value.str); break;
    case EXPR_MEMBER: {
        MemberExpr *member = (MemberExpr *)expr;
        printf("(");
        printExpr(member->child);
        printf(").%s", member->member.str);
    } break;
    case EXPR_CALL: {
        CallExpr *call = (CallExpr *)expr;
        unsigned int i;
        printf("(");
        printExpr(call->func);
        printf(")(");
        for (i = 0; i < call->args.len; i++) {
            printExpr(call->args.data[i]);
            if (i != call->args.len - 1)
                printf(", ");
        }
        printf(")");
    } break;
    case EXPR_UNOP: {
        UnopExpr *unop = (UnopExpr *)expr;
        if (unop->type == UNOP_NOT) {
            printf("!(");
            printExpr(unop->child);
            printf(")");
        } else if (unop->type == UNOP_ADDR_OF) {
            printf("&(");
            printExpr(unop->child);
            printf(")");
        } else if (unop->type == UNOP_DEREF) {
            printf("(");
            printExpr(unop->child);
            printf(")^");
        }
    } break;
    case EXPR_BINOP: {
        BinopExpr *binop = (BinopExpr *)expr;
        char *operator;
        if (binop->type == BINOP_INDEX) {
            printf("(");
            printExpr(binop->left);
            printf(")");
            printf("[");
            printExpr(binop->right);
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
        printExpr(binop->left);
        printf(") %s (", operator);
        printExpr(binop->right);
        printf(")");
    } break;
    case EXPR_FUNC: {
        FuncExpr *func = (FuncExpr *)expr;
        unsigned int i;
        printf("(");
        for (i = 0; i < func->params.len; i++) {
            printf("%s: ", func->params.data[i].name.str);
            printType(func->params.data[i].type);
            if (i != func->params.len - 1)
                printf(", ");
        }
        printf(") ");
        if (func->returnType.kind != TYPE_VOID) {
            printType(func->returnType);
            printf(" ");
        }
        printf("{ ");
        for (i = 0; i < func->statements.len; i++) {
            printStmt(func->statements.data[i]);
            printf(" ");
        }
        printf("}");
    } break;
    }
}

void
printDecl(Declaration *decl) {
    printf("%s :", decl->name.str);
    if (decl->type.kind != TYPE_INFERRED) {
        printf(" ");
        printType(decl->type);
        printf(" ");
    }
    if (decl->value) {
        if (decl->isConst)
            printf(": ");
        else
            printf("= ");
        printExpr(decl->value);
    }
    printf(";");
}

void
printStmt(StmtHeader *stmt) {
    if (!stmt) {
        printf(";");
        return;
    }

    switch (stmt->type) {
    case STMT_EXPR:
        printExpr(((ExprStmt *)stmt)->expr);
        printf(";");
        break;
    case STMT_RETURN:
        printf("return ");
        printExpr(((ReturnStmt *)stmt)->expr);
        printf(";");
        break;
    case STMT_BREAK:
        printf("break;");
        break;
    case STMT_CONTINUE:
        printf("continue;");
        break;
    case STMT_IF: {
        IfStmt *ifStmt = (IfStmt *)stmt;
        printf("if ");
        printExpr(ifStmt->condition);
        printf(" ");
        printStmt(ifStmt->ifBranch);
        if (ifStmt->elseBranch) {
            printf(" else ");
            printStmt(ifStmt->elseBranch);
        }
    } break;
    case STMT_WHILE: {
        WhileStmt *whileStmt = (WhileStmt *)stmt;
        printf("while ");
        printExpr(whileStmt->condition);
        printf(" ");
        printStmt(whileStmt->statement);
    } break;
    case STMT_BLOCK: {
        BlockStmt *blockStmt = (BlockStmt *)stmt;
        unsigned int i;
        printf("{ ");
        for (i = 0; i < blockStmt->statements.len; i++) {
            printStmt(blockStmt->statements.data[i]);
            printf(" ");
        }
        printf("}");
    } break;
    case STMT_DECL:
        printDecl(&((DeclStmt *)stmt)->decl);
        break;
    }
}
