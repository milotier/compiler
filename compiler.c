/* Program file structure:
 * - compiler.c: the file which contains main, it reads command-line arguments and
 *   initializes the context.
 * - common.h: the header file that declares useful macros and data structures,
 *   like arrays and tables, and defines the AST and token structures. It also
 *   defines the context struct, which contains the state that is used when
 *   compiling a module. Finally, it defines functions for reporting errors and
 *   warnings.
 * - common.c: the implementation of data structures and functions defined in
 *   common.h
 * - lexer.h: the header file for the lexer.
 * - lexer.c: the implementation file of the lexer.
 * - parser.h: the header file for the parser.
 * - parser.c: the implementation file of the parser.
 */
#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common.h"
#include "parser.h"

/* function implementations */
static void
usage(void) {
    die("usage: %s [file] [--help]", argv0);
}

int
main(int argc, char *argv[]) {
    struct stat fileStat;
    int i, status, srcFile;
    unsigned int totalRead;
    Context ctx = {0};

    /* To make sure unicode code points in character literals are printed
     * correctly */
    setlocale(LC_ALL, "en_US.UTF-8");

    argv0 = argv[0];

    if (argc == 1)
        usage();
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0)
            usage();
        else
            ctx.srcPath = argv[i];
    }

    if (!ctx.srcPath)
        die("%s: no input file", argv0);

    srcFile = open(ctx.srcPath, O_RDONLY);
    if (srcFile < 0)
        die("%s: failed to open %s:", argv0, ctx.srcPath);
    status = fstat(srcFile, &fileStat);
    if (status < 0)
        die("%s: failed to get file status of %s:", argv0, ctx.srcPath);

    ctx.srcSize = (unsigned int)fileStat.st_size;
    ctx.srcCode = xMalloc(ctx.srcSize);
    totalRead = 0;
    while (totalRead < ctx.srcSize) {
        ssize_t bytesRead = read(srcFile, ctx.srcCode + totalRead, ctx.srcSize - totalRead);
        if (!bytesRead)
            break;
        if (bytesRead < 0)
            die("%s: failed to read file %s:", argv0, ctx.srcPath);
        totalRead += (unsigned int)bytesRead;
    }

    parse(&ctx);
    for (i = 0; (unsigned int)i < ctx.topScope.value.tbl.cap; i++) {
        if (ctx.topScope.value.tbl.entries[i].sym.str) {
            printDecl((Declaration *)ctx.topScope.value.tbl.entries[i].val);
            putchar('\n');
        }
    }
}
