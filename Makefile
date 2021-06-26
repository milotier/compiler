SRC=compiler.c common.c lexer.c parser.c
OBJ=$(SRC:.c=.o)
CFLAGS=-Wall -Wextra -Wshadow -Wconversion -Wimplicit
OUT=compiler

.PHONY: clean

$(OUT): $(OBJ)
	$(CC) $(OBJ) -o $(OUT)
clean:
	rm -f $(OBJ) $(OUT)
