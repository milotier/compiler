#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <wchar.h>

#include "common.h"
#include "lexer.h"

/* macros */
#define SYMBUFLEN 1024

#define CARRY_BIT 31u
#define DIGIT_MASK (~((1u) << (CARRY_BIT)))

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

BigInt
bigIntFromU64(unsigned long long value) {
    BigInt result = {0};

    while (value) {
        arrayAdd(&result.data, (unsigned int)(value & DIGIT_MASK));
        value >>= CARRY_BIT;
    }

    return result;
}

BigInt
bigIntFromI64(long long value) {
    BigInt result = {0};

    if (value < 0) {
        result.sign = 1;
        value = -value;
    }
    while (value) {
        arrayAdd(&result.data, (unsigned int)(value & DIGIT_MASK));
        value >>= CARRY_BIT;
    }

    return result;
}

int
bigIntToU64(unsigned long long *out, BigInt *x) {
    unsigned long long result = 0;
    unsigned int i;

    for (i = 0; i / CARRY_BIT < x->data.len; i++) {
        unsigned int bitMask = 1u << (i % CARRY_BIT);
        unsigned int bit = (x->data.data[i / CARRY_BIT] & bitMask) >> (i % CARRY_BIT);
        if (i >= 64 && bit)
            return 0;
        result |= (unsigned long long)bit << i;
    }

    *out = result;
    return 1;
}

BigInt
bigIntCopy(BigInt *x) {
    BigInt y;
    y.sign = x->sign;
    arrayCopy(&x->data, &y.data);
    return y;
}

void
bigIntFree(BigInt *x) {
    arrayFree(&x->data);
    x->sign = 0;
}

void
bigIntMove(BigInt *dest, BigInt src) {
    bigIntFree(dest);
    *dest = src;
}

int
bigIntCmp(BigInt *x, BigInt *y) {
    unsigned int i;

    if ((x->sign == 1 && y->sign == 0) || x->data.len < y->data.len)
        return -1;
    if ((x->sign == 0 && y->sign == 1) || x->data.len > y->data.len)
        return 1;

    for (i = x->data.len - 1; i < x->data.len; i--) {
        if (x->data.data[i] < y->data.data[i])
            return -1;
        if (x->data.data[i] > y->data.data[i])
            return 1;
    }

    return 0;
}

void
bigIntAnd(BigInt *out, BigInt *x, BigInt *y) {
    BigInt result = {0};
    unsigned int i;

    if (x->data.len < y->data.len)
        SWAP(BigInt *, x, y);

    arraySetLen(&result.data, x->data.len);

    for (i = 0; i < y->data.len; i++)
        result.data.data[i] = x->data.data[i] & y->data.data[i];
    memcpy(result.data.data + i, x->data.data + i, result.data.len - i);

    bigIntMove(out, result);
}

void
bigIntOr(BigInt *out, BigInt *x, BigInt *y) {
    BigInt result = {0};
    unsigned int i;

    if (x->data.len < y->data.len)
        SWAP(BigInt *, x, y);

    arraySetLen(&result.data, x->data.len);

    for (i = 0; i < y->data.len; i++)
        result.data.data[i] = x->data.data[i] | y->data.data[i];

    memcpy(result.data.data + i,
           x->data.data + i,
           sizeof(*result.data.data) * (result.data.len - i));

    bigIntMove(out, result);
}

void
bigIntXor(BigInt *out, BigInt *x, BigInt *y) {
    BigInt result = {0};
    unsigned int i;

    if (x->data.len < y->data.len)
        SWAP(BigInt *, x, y);

    arraySetLen(&result.data, x->data.len);

    for (i = 0; i < y->data.len; i++)
        result.data.data[i] = x->data.data[i] ^ y->data.data[i];
    memcpy(result.data.data + i, x->data.data + i, result.data.len - i);

    bigIntMove(out, result);
}

int
bigIntLshift(BigInt *out, BigInt *x, BigInt *y) {
    BigInt result = {0}, digits = {0}, bits = {0};
    BigInt carryBit = bigIntFromU64(CARRY_BIT);
    unsigned long long sDigits, sBits;
    unsigned int i, carry = 0;

    bigIntDiv(&digits, &bits, y, &carryBit);
    bigIntToU64(&sBits, &bits);
    if (!bigIntToU64(&sDigits, &digits) || sDigits > UINT_MAX)
        return 0;

    arraySetLen(&result.data, x->data.len + (unsigned int)sDigits);
    memset(result.data.data, 0, sizeof(*result.data.data) * result.data.len);

    for (i = (unsigned int)sDigits; i < result.data.len; i++) {
        unsigned int mask = ~((1u << (CARRY_BIT - sBits)) - 1);
        result.data.data[i] = (x->data.data[i - (unsigned int)sDigits] << sBits) + carry;
        carry = (x->data.data[i - (unsigned int)sDigits] & mask) >> (CARRY_BIT - sBits);
    }
    if (carry)
        arrayAdd(&result.data, carry);

    bigIntFree(&carryBit);
    bigIntFree(&digits);
    bigIntFree(&bits);
    bigIntMove(out, result);
    return 1;
}

void
bigIntRshift(BigInt *out, BigInt *x, BigInt *y) {
    BigInt result = {0}, digits = {0}, bits = {0};
    BigInt carryBit = bigIntFromU64(CARRY_BIT);
    unsigned long long sDigits, sBits;
    unsigned int i, carry = 0;

    bigIntDiv(&digits, &bits, y, &carryBit);
    bigIntToU64(&sBits, &bits);
    bigIntToU64(&sDigits, &digits);

    if (sDigits >= x->data.len) {
        bigIntMove(out, result);
        return;
    }

    arraySetLen(&result.data, x->data.len - (unsigned int)sDigits);
    memcpy(result.data.data,
           x->data.data + (unsigned int)sDigits,
           sizeof(*result.data.data) * result.data.len);

    for (i = result.data.len - 1; i < result.data.len; i--) {
        unsigned int mask = (1u << sBits) - 1;
        unsigned int digit = x->data.data[i + (unsigned int)sDigits];
        result.data.data[i] = (digit >> sBits) + carry;
        carry = (digit & mask) << (CARRY_BIT - sBits);
    }

    bigIntFree(&carryBit);
    bigIntFree(&digits);
    bigIntFree(&bits);
    bigIntMove(out, result);
}

void
bigIntAdd(BigInt *out, BigInt *x, BigInt *y) {
    BigInt result = {0};
    unsigned int i, carry;

    if (x->sign != y->sign) {
        if (bigIntCmp(x, y) < 0)
            SWAP(BigInt *, x, y);
        y->sign = !y->sign;
        bigIntSub(out, x, y);
        return;
    }

    result.sign = x->sign;

    if (x->data.len < y->data.len)
        SWAP(BigInt *, x, y);

    arraySetLen(&result.data, x->data.len);

    carry = 0;
    for (i = 0; i < y->data.len; i++) {
        result.data.data[i] = x->data.data[i] + y->data.data[i] + carry;
        carry = result.data.data[i] >> CARRY_BIT;
        result.data.data[i] &= DIGIT_MASK;
    }

    carry = 0;
    for (; i < x->data.len; i++) {
        result.data.data[i] = x->data.data[i] + carry;
        carry = result.data.data[i] >> CARRY_BIT;
        result.data.data[i] &= DIGIT_MASK;
    }

    if (carry)
        arrayAdd(&result.data, 1);

    bigIntMove(out, result);
}

void
bigIntSub(BigInt *out, BigInt *x, BigInt *y) {
    BigInt result = {0};
    unsigned int i, borrow;

    if (x->sign != y->sign) {
        if (bigIntCmp(x, y) < 0)
            SWAP(BigInt *, x, y);
        y->sign = 0;
        bigIntAdd(&result, x, y);
        result.sign = 0;
        bigIntMove(out, result);
    }
    if (bigIntCmp(x, y) < 0) {
        result.sign = !x->sign;
        SWAP(BigInt *, x, y);
    } else {
        result.sign = x->sign;
    }

    arraySetLen(&result.data, x->data.len);

    borrow = 0;
    for (i = 0; i < y->data.len; i++) {
        result.data.data[i] = x->data.data[i] - y->data.data[i] - borrow;
        borrow = result.data.data[i] >> CARRY_BIT;
        result.data.data[i] &= DIGIT_MASK;
    }

    borrow = 0;
    for (; i < x->data.len; i++) {
        result.data.data[i] = x->data.data[i] - borrow;
        borrow = result.data.data[i] >> CARRY_BIT;
        result.data.data[i] &= DIGIT_MASK;
    }

    while (result.data.len && !result.data.data[result.data.len - 1])
        result.data.len--;

    bigIntMove(out, result);
}

void
bigIntMul(BigInt *out, BigInt *x, BigInt *y) {
    BigInt result = {0};
    unsigned int i;

    if (x->sign != y->sign)
        result.sign = 1;

    arraySetLen(&result.data, x->data.len + y->data.len + 1);
    memset(result.data.data, 0, sizeof(*result.data.data) * result.data.len);

    for (i = 0; i < x->data.len; i++) {
        unsigned int j, carry = 0;
        for (j = 0; j < y->data.len; j++) {
            unsigned long long res = (unsigned long long)result.data.data[i + j] +
                                     ((unsigned long long)x->data.data[i] *
                                      (unsigned long long)y->data.data[j]) +
                                     (unsigned long long)carry;
            result.data.data[i + j] = (unsigned int)(res & DIGIT_MASK);
            carry = (unsigned int)(res >> (unsigned long long)CARRY_BIT);
        }
        result.data.data[i + y->data.len] = carry;
    }

    while (result.data.len && !result.data.data[result.data.len - 1])
        result.data.len--;

    bigIntMove(out, result);
}

static unsigned int
bigIntGetBit(BigInt *x, unsigned int bit) {
    unsigned int index = bit / CARRY_BIT;
    unsigned int mask = 1u << (bit % CARRY_BIT);
    return x->data.data[index] & mask;
}

static void
bigIntSetBit(BigInt *x, unsigned int totalBit, unsigned int value) {
    unsigned int index = totalBit / CARRY_BIT;
    unsigned int bit = totalBit % CARRY_BIT;

    if (x->data.len <= index) {
        unsigned int oldLen = x->data.len;
        arraySetLen(&x->data, index + 1);
        memset(x->data.data + oldLen,
               0,
               sizeof(*x->data.data) * (x->data.len - oldLen));
    }

    x->data.data[index] = (x->data.data[index] & ~(1u << bit)) | ((unsigned int)(value != 0) << bit);
    if (index == x->data.len - 1 && !x->data.data[index])
        x->data.len--;
}

/* Simple binary long division */
int
bigIntDiv(BigInt *q, BigInt *r, BigInt *num, BigInt *den) {
    BigInt quot = {0}, rem = {0};
    unsigned int numBits = CARRY_BIT * num->data.len;
    unsigned int i, j;

    if (!den->data.len)
        return 0;

    for (i = numBits - 1; i < numBits; i--) {
        unsigned int carry = 0;

        /* Left-shift the remainder by 1 */
        for (j = 0; j < rem.data.len; j++) {
            rem.data.data[j] = (rem.data.data[j] << 1) | carry;
            carry = rem.data.data[j] >> CARRY_BIT;
            rem.data.data[j] &= DIGIT_MASK;
        }
        if (carry)
            arrayAdd(&rem.data, 1);

        bigIntSetBit(&rem, 0, bigIntGetBit(num, i));

        if (bigIntCmp(&rem, den) >= 0) {
            bigIntSub(&rem, &rem, den);
            bigIntSetBit(&quot, i, 1);
        }
    }

    if (r)
        bigIntMove(r, rem);
    else
        bigIntFree(&rem);
    if (q)
        bigIntMove(q, quot);
    else
        bigIntFree(&quot);
    return 1;
}

void
bigIntPrint(BigInt *x) {
    BigInt value = bigIntCopy(x);
    BigInt ten = bigIntFromI64(10);
    String buf = {0};
    unsigned int i;

    if (!x->data.len) {
        putchar('0');
        return;
    }

    if (x->sign == 1) {
        putchar('-');
        x->sign = 0;
    }

    while (value.data.len) {
        BigInt rem = {0};
        bigIntDiv(&value, &rem, &value, &ten);

        stringAddC(&buf, '0' + (char)rem.data.data[0]);
        bigIntFree(&rem);
    }

    for (i = buf.len - 1; i < buf.len; i--)
        putchar(buf.data[i]);

    stringFree(&buf);
    bigIntFree(&value);
    bigIntFree(&ten);
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
    case TYPE_COMPTIME_INT: printf("comptime_int"); break;
    case TYPE_INT: printf("%c%hhu", type.isSigned ? 'i' : 'u', type.width); break;
    case TYPE_COMPTIME_FLOAT: printf("comptime_float"); break;
    case TYPE_FLOAT: printf("f%hhu", type.width); break;
    case TYPE_BOOL: printf("bool"); break;
    }
}

void
printExpr(ExprHeader *expr) {
    switch (expr->type) {
    case EXPR_INT: bigIntPrint(&((IntExpr *)expr)->value); break;
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
