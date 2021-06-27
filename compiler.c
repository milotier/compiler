/*
 * vim: noexpandtab:softtabstop=8:shiftwidth=8
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
#include "lexer.h"
#include "parser.h"


/* function implementations */
static void
Usage(void)
{
	Die("usage: %s [file] [--help]", argv0);
}

int
main(int argc, char *argv[])
{
	struct stat fileStat;
	int i, status, srcFile;
	unsigned int totalRead;
	context ctx = {0};

	/* To make sure unicode code points in character literals are printed
	 * correctly */
	setlocale(LC_ALL, "en_US.UTF-8");

	argv0 = argv[0];

	if (argc == 1)
		Usage();
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0)
			Usage();
		else
			ctx.srcPath = argv[i];
	}

	if (!ctx.srcPath)
		Die("%s: no input file", argv0);

	srcFile = open(ctx.srcPath, O_RDONLY);
	if (srcFile < 0)
		Die("%s: failed to open %s:", argv0, ctx.srcPath);
	status = fstat(srcFile, &fileStat);
	if (status < 0)
		Die("%s: failed to get file status of %s:", argv0, ctx.srcPath);

	ctx.srcSize = (unsigned int)fileStat.st_size;
	ctx.srcCode = xmalloc(ctx.srcSize);
	totalRead = 0;
	while (totalRead < ctx.srcSize) {
		ssize_t bytesRead = read(srcFile,
					 ctx.srcCode + totalRead,
					 ctx.srcSize - totalRead);
		if (!bytesRead)
			break;
		if (bytesRead < 0)
			Die("%s: failed to read file %s:", argv0, ctx.srcPath);
		totalRead += (unsigned int)bytesRead;
	}

	PrintExpr(ParseExpr(&ctx));
}
