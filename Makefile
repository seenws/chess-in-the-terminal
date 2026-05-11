CC     = gcc
CFLAGS = -Wall -Wextra -std=c99 -Iheaders

SRC = src/main.c src/board.c src/parser.c src/movegen.c src/game.c
OBJ = $(SRC:.c=.o)

TARGET = citt

TEST_BIN = tmovegen

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Tests are built straight from source with -g -O0 for gdb. board.c is linked
# for board_init/board_print; movegen.c is #included by the test file, so we
# must NOT also link src/movegen.o (would duplicate append_pseudolegal_moves).
$(TEST_BIN): tests/tmovegen.c src/board.c headers/board.h headers/game.h headers/movegen.h
	$(CC) $(CFLAGS) -g -O0 -o $@ tests/tmovegen.c src/board.c

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(OBJ) $(TARGET) $(TEST_BIN)

.PHONY: clean test
