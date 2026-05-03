CC     = gcc
CFLAGS = -Wall -Wextra -std=c99 -Iheaders

SRC = src/main.c src/board.c src/parser.c src/movegen.c src/game.c
OBJ = $(SRC:.c=.o)

TARGET = citt

TEST_SRC = tests/tmovegen.c
TEST_OBJ = $(TEST_SRC:.c=.o)
TEST_BIN = tmovegen

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(TEST_BIN): $(TEST_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(OBJ) $(TEST_OBJ) $(TARGET) $(TEST_BIN)

.PHONY: clean test
