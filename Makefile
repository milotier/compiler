SRC=compiler.c common.c lexer.c parser.c typer.c
OBJ=$(SRC:.c=.o)
CFLAGS=-Wall -Wextra -Wshadow -Wconversion -Wimplicit -std=c99 -pedantic -g
OUT=compiler

.PHONY: clean

$(OUT): $(OBJ)
	$(CC) $(OBJ) -o $(OUT)
clean:
	rm -f $(OBJ) $(OUT)
