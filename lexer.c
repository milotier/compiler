#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "lexer.h"

/* TODO: think about allowing unicode in identifiers? */

/* function implementations */
static int
getChar(Context *ctx) {
    int c;
    if (ctx->srcIndex == ctx->srcSize)
        return EOF;

    c = ctx->srcCode[ctx->srcIndex];
    if ((unsigned int)c <= 32 && c != '\n' && c != '\r' && c != ' ' && c != '\t')
        error(ctx, ctx->srcIndex, "found illegal character 0x%hhx", (char)c);
    ctx->srcIndex++;
    return c;
}

static void
ungetChar(Context *ctx) {
    ctx->srcIndex--;
}

static char
CheckForAssign(char type, char assignType, Context *ctx) {
    int c = getChar(ctx);
    if (c == '=')
        return assignType;
    ungetChar(ctx);
    return type;
}

static Token
readNumber(int c, Context *ctx) {
    String buf = {0};
    int isFloat = 0;
    int radix = 10;
    Token tok;

    if (c == '0') {
        int next = getChar(ctx);
        if (next == 'x') {
            radix = 16;
            c = getChar(ctx);
        } else if (next == 'o') {
            radix = 8;
            c = getChar(ctx);
        } else if (next == 'b') {
            radix = 2;
            c = getChar(ctx);
        } else {
            ungetChar(ctx);
        }
    }

    for (;;) {
        if (c == '.') {
            if (radix != 10)
                break;
            isFloat = 1;
        } else if (radix == 2 && (c < '0' || c > '1')) {
            break;
        } else if (radix == 8 && (c < '0' || c > '7')) {
            break;
        } else if (radix == 10 && !isdigit(c)) {
            break;
        } else if (radix == 16 && !isxdigit(c)) {
            break;
        }
        stringAddC(&buf, (char)c);
        c = getChar(ctx);
    }
    ungetChar(ctx);

    if (isFloat) {
        tok.type = TOK_FLOAT;
        tok.val.f = strtod(buf.data, NULL);
    } else {
        tok.type = TOK_INT;
        tok.val.i = strtoull(buf.data, NULL, radix);
    }

    stringFree(&buf);

    return tok;
}

static unsigned char
HexToInt(char c) {
    if (isdigit(c))
        return (unsigned char)(c - '0');
    return (unsigned char)(c - 'a' + 10);
}

static char
getEscape(Context *ctx) {
    unsigned int backslashPos = ctx->srcIndex - 1;
    int c = getChar(ctx);

    switch (c) {
    case '\0': error(ctx, backslashPos, "unexpected EOF after '\\' escape");
    case 'a': c = '\a'; break;
    case 'b': c = '\b'; break;
    case 'e': c = '\x1b'; break;
    case 'f': c = '\f'; break;
    case 'n': c = '\n'; break;
    case 'r': c = '\r'; break;
    case 't': c = '\t'; break;
    case 'v': c = '\v'; break;
    case '\\': c = '\\'; break;
    case '\'': c = '\''; break;
    case '"': c = '"'; break;
    case 'x': {
        unsigned char val = 0;

        c = getChar(ctx);
        if (!isxdigit(c))
            error(ctx, backslashPos, "expected two hex digits after \\x escape");
        val = (unsigned char)(val * 16) + HexToInt((char)c);
        c = getChar(ctx);
        if (!isxdigit(c))
            error(ctx, backslashPos, "expected two hex digits after \\x escape");
        val = (unsigned char)(val * 16) + HexToInt((char)c);

        c = (char)val;
        break;
    }
    default:
        error(ctx, backslashPos, "invalid escape sequence \\%c", c);
    }

    return (char)c;
}

static Symbol
readstring(Context *ctx) {
    String buf = {0};
    Symbol sym;

    for (;;) {
        int c = getChar(ctx);
        if (c == '\\')
            c = getEscape(ctx);
        if (c == EOF || c == '"')
            break;
        stringAddC(&buf, (char)c);
    }

    sym = addSymbol(buf.data, buf.len);
    stringFree(&buf);
    return sym;
}

/* TODO: clean this up */
static unsigned long long
getCodePoint(Context *ctx) {
    int c = getChar(ctx);
    unsigned int uc = (unsigned int)c;
    if (c == EOF)
        error(ctx, ctx->srcIndex, "unexpected EOF in character literal");
    if (uc <= 0x7f) {
        return (unsigned long long)c;
    } else if ((uc & 0xe0) == 0xc0) {
        unsigned long long buf[2] = {uc & 0x1f};
        c = getChar(ctx);
        uc = (unsigned int)c;
        if (c == EOF)
            error(ctx, ctx->srcIndex, "unexpected EOF in character literal");
        if ((uc & 0xc0) != 0x80)
            error(ctx, ctx->srcIndex, "invalid byte in character literal");
        buf[1] = uc & 0x3f;
        return buf[1] | (buf[0] << 6);
    } else if ((c & 0xf0) == 0xe0) {
        unsigned long long buf[3] = {uc & 0xf};
        unsigned int i;
        for (i = 1; i < LEN(buf); i++) {
            c = getChar(ctx);
            uc = (unsigned int)c;
            if (c == EOF)
                error(ctx, ctx->srcIndex, "unexpected EOF in character literal");
            if ((uc & 0xc0) != 0x80)
                error(ctx, ctx->srcIndex, "invalid byte in character literal");
            buf[i] = uc & 0x3f;
        }
        return buf[2] | (buf[1] << 6) | (buf[0] << 12);
    } else if ((uc & 0xf8) == 0xf0) {
        unsigned long long buf[4] = {uc & 0x7};
        unsigned int i;
        for (i = 1; i < LEN(buf); i++) {
            c = getChar(ctx);
            uc = (unsigned int)c;
            if (c == EOF)
                error(ctx, ctx->srcIndex, "unexpected EOF in character literal");
            if ((uc & 0xc0) != 0x80)
                error(ctx, ctx->srcIndex, "invalid byte in character literal");
            buf[i] = uc & 0x3f;
        }
        return buf[3] | (buf[2] << 6) | (buf[1] << 12) | (buf[0] << 18);
    } else {
        error(ctx, ctx->srcIndex, "invalid byte in character literal");
    }
}

static Token
nextLiteralToken(Context *ctx) {
    int c = getChar(ctx);
    Token tok;

    /* TODO: make this... thing cleaner */
    while (c == '\n' || c == '\r' || c == ' ' || c == '\t' || c == '/') {
        if (c == '/') {
            c = getChar(ctx);
            if (c == '*') {
                unsigned int nesting = 1;
                c = getChar(ctx);
                for (;;) {
                    c = getChar(ctx);
                    if (c == '*') {
                        c = getChar(ctx);
                        if (c == '/') {
                            nesting--;
                            if (!nesting)
                                break;
                        }
                    } else if (c == '/') {
                        c = getChar(ctx);
                        if (c == '*')
                            nesting++;
                    }
                }
            } else if (c == '/') {
                while (c != '\n')
                    c = getChar(ctx);
            }
        }
        c = getChar(ctx);
    }
    tok.pos = ctx->srcIndex;

    switch (c) {
    case EOF: tok.type = TOK_EOF; return tok;
    case '=': tok.type = CheckForAssign('=', TOK_EQ, ctx); return tok;
    case '!': tok.type = CheckForAssign('!', TOK_NEQ, ctx); return tok;
    case '.': tok.type = '.'; return tok;
    case '>':
        c = getChar(ctx);
        if (c == '>') {
            tok.type = CheckForAssign(TOK_RSHIFT, TOK_RSHIFT_ASS, ctx);
            return tok;
        }
        ungetChar(ctx);
        tok.type = CheckForAssign('>', TOK_GTE, ctx);
        return tok;
    case '<':
        c = getChar(ctx);
        if (c == '<') {
            tok.type = CheckForAssign(TOK_LSHIFT, TOK_LSHIFT_ASS, ctx);
            return tok;
        }
        ungetChar(ctx);
        tok.type = CheckForAssign('<', TOK_LTE, ctx);
        return tok;
    case '+': tok.type = CheckForAssign('+', TOK_ADD_ASS, ctx); return tok;
    case '-':
        c = getChar(ctx);
        if (c == '>') {
            tok.type = TOK_ARROW;
            return tok;
        }
        ungetChar(ctx);
        tok.type = CheckForAssign('-', TOK_SUB_ASS, ctx);
        return tok;
    case '*': tok.type = CheckForAssign('*', TOK_MUL_ASS, ctx); return tok;
    case '/': tok.type = CheckForAssign('/', TOK_DIV_ASS, ctx); return tok;
    case '%': tok.type = CheckForAssign('%', TOK_MOD_ASS, ctx); return tok;
    case '^': tok.type = CheckForAssign('^', TOK_BITXOR_ASS, ctx); return tok;
    case '&':
        c = getChar(ctx);
        if (c == '&') {
            tok.type = TOK_AND;
            return tok;
        }
        ungetChar(ctx);
        tok.type = CheckForAssign('&', TOK_BITAND_ASS, ctx);
        return tok;
    case '|':
        c = getChar(ctx);
        if (c == '|') {
            tok.type = TOK_OR;
            return tok;
        }
        ungetChar(ctx);
        tok.type = CheckForAssign('|', TOK_BITOR_ASS, ctx);
        return tok;
    case '"':
        tok.type = TOK_STR;
        tok.val.s = readstring(ctx);
        return tok;
    case '\'':
        tok.type = TOK_INT;
        tok.val.i = getCodePoint(ctx);
        return tok;
    }

    if (isdigit(c))
        return readNumber(c, ctx);
    if (isalpha(c) || c == '_') {
        String buf = {0};

        for (;;) {
            stringAddC(&buf, (char)c);
            c = getChar(ctx);
            if (!isalnum(c) && c != '_') {
                ungetChar(ctx);
                break;
            }
        }

        if (strcmp(buf.data, "true") == 0) {
            tok.type = TOK_BOOL;
            tok.val.i = 1;
        } else if (strcmp(buf.data, "false") == 0) {
            tok.type = TOK_BOOL;
            tok.val.i = 0;
        } else if (strcmp(buf.data, "u8") == 0) {
            tok.type = TOK_U8;
        } else if (strcmp(buf.data, "u16") == 0) {
            tok.type = TOK_U16;
        } else if (strcmp(buf.data, "u32") == 0) {
            tok.type = TOK_U32;
        } else if (strcmp(buf.data, "u64") == 0) {
            tok.type = TOK_U64;
        } else if (strcmp(buf.data, "i8") == 0) {
            tok.type = TOK_I8;
        } else if (strcmp(buf.data, "i16") == 0) {
            tok.type = TOK_I16;
        } else if (strcmp(buf.data, "i32") == 0) {
            tok.type = TOK_I32;
        } else if (strcmp(buf.data, "i64") == 0) {
            tok.type = TOK_I64;
        } else if (strcmp(buf.data, "f32") == 0) {
            tok.type = TOK_F32;
        } else if (strcmp(buf.data, "f64") == 0) {
            tok.type = TOK_F64;
        } else if (strcmp(buf.data, "bool") == 0) {
            tok.type = TOK_BOOL_TYPE;
        } else if (strcmp(buf.data, "if") == 0) {
            tok.type = TOK_IF;
        } else if (strcmp(buf.data, "else") == 0) {
            tok.type = TOK_ELSE;
        } else if (strcmp(buf.data, "while") == 0) {
            tok.type = TOK_WHILE;
        } else if (strcmp(buf.data, "for") == 0) {
            tok.type = TOK_FOR;
        } else if (strcmp(buf.data, "break") == 0) {
            tok.type = TOK_BREAK;
        } else if (strcmp(buf.data, "continue") == 0) {
            tok.type = TOK_CONTINUE;
        } else if (strcmp(buf.data, "return") == 0) {
            tok.type = TOK_RETURN;
        } else {
            tok.type = TOK_IDENT;
            tok.val.s = addSymbol(buf.data, buf.len);
        }

        stringFree(&buf);
        return tok;
    }

    if (c != '.' && c != ':' && c != ';' && c != ',' && c != '(' && c != ')'
        && c != '[' && c != ']' && c != '{' && c != '}') {
        if (isprint(c))
            error(ctx, tok.pos, "found illegal character '%c'", c);
        else
            error(ctx, tok.pos, "found illegal character 0x%hhx", c);
    }

    /* It it got here, it is a single-character punctuation Token */
    tok.type = (char)c;
    return tok;
}

Token
nextToken(Context *ctx) {
    if (ctx->cachedTokens.len) {
        Token tok = ctx->cachedTokens.data[0];
        arrayRemove(&ctx->cachedTokens, 0);
        return tok;
    }
    return nextLiteralToken(ctx);
}

Token
peekToken(unsigned int i, Context *ctx) {
    while (ctx->cachedTokens.len < i)
        arrayAdd(&ctx->cachedTokens, nextLiteralToken(ctx));
    return ctx->cachedTokens.data[i - 1];
}
