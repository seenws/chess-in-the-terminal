CC          = gcc
BASE_CFLAGS = -Wall -Wextra -std=c99 -D_POSIX_C_SOURCE=199309L -Iheaders

# Release: optimized, NDEBUG strips assert(), no DBG_PRINTF call sites.
RELEASE_CFLAGS = $(BASE_CFLAGS) -O2 -DNDEBUG

# Debug: no opt, debug symbols, DEBUG enables DBG_PRINTF / DBG_ASSERT.
DEBUG_CFLAGS   = $(BASE_CFLAGS) -g -O0 -DDEBUG

SRC = src/main.c src/board.c src/parser.c src/movegen.c src/game.c \
      src/zobrist.c src/search.c

REL_DIR = build/release
DBG_DIR = build/debug

REL_OBJ = $(SRC:src/%.c=$(REL_DIR)/%.o)
DBG_OBJ = $(SRC:src/%.c=$(DBG_DIR)/%.o)

REL_BIN = citt
DBG_BIN = citt-debug

TEST_BIN = tmovegen

.PHONY: all release debug clean test

all: release

release: $(REL_BIN)
debug:   $(DBG_BIN)

$(REL_BIN): $(REL_OBJ)
	$(CC) $(RELEASE_CFLAGS) -o $@ $^

$(DBG_BIN): $(DBG_OBJ)
	$(CC) $(DEBUG_CFLAGS) -o $@ $^

$(REL_DIR)/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(RELEASE_CFLAGS) -c -o $@ $<

$(DBG_DIR)/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(DEBUG_CFLAGS) -c -o $@ $<

# Tests are built straight from source with -g -O0 for gdb. board.c is linked
# for board_init/board_print; movegen.c is #included by the test file, so we
# must NOT also link src/movegen.o (would duplicate append_pseudolegal_moves).
$(TEST_BIN): tests/tmovegen.c src/board.c headers/board.h headers/game.h headers/movegen.h
	$(CC) $(BASE_CFLAGS) -g -O0 -o $@ tests/tmovegen.c src/board.c

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -rf build $(REL_BIN) $(DBG_BIN) $(TEST_BIN)
