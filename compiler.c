/*
 * vim: noexpandtab:softtabstop=8:shiftwidth=8
 */
#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common.h"
#include "lexer.h"

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
	token tok;

	argv0 = argv[0];

	if (argc == 1)
		Usage();
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0)
			Usage();
		else
			srcPath = argv[i];
	}

	if (!srcPath)
		Die("%s: no input file", argv0);

	srcFile = open(srcPath, O_RDONLY);
	if (srcFile < 0)
		Die("%s: failed to open %s:", argv0, srcPath);
	status = fstat(srcFile, &fileStat);
	if (status < 0)
		Die("%s: failed to get file status of %s:", argv0, srcPath);

	srcSize = (unsigned int)fileStat.st_size;
	srcCode = xmalloc(srcSize);
	totalRead = 0;
	while (totalRead < srcSize) {
		ssize_t bytesRead = read(srcFile, srcCode + totalRead, srcSize - totalRead);
		if (!bytesRead)
			break;
		if (bytesRead < 0)
			Die("%s: failed to read file %s:", argv0, srcPath);
		totalRead += (unsigned int)bytesRead;
	}

	while ((tok = NextToken()).type != TOK_EOF) {
		PrintToken(tok);
		putchar('\n');
	}
}
