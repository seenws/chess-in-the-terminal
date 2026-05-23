BASE_CFLAGS = -Wall -Wextra -std=c99 -D_POSIX_C_SOURCE=199309L -Iheaders -MMD -MP

# Release: optimized, NDEBUG strips assert(), no DBG_PRINTF call sites.
RELEASE_CFLAGS = $(BASE_CFLAGS) -O2 -DNDEBUG

# Debug: no opt, debug symbols, DEBUG enables DBG_PRINTF / DBG_ASSERT.
DEBUG_CFLAGS   = $(BASE_CFLAGS) -g -O0 -DDEBUG

SRC = src/main.c src/board.c src/parser.c src/movegen.c src/game.c \
      src/zobrist.c src/search.c

PERFT_SRC = src/perft.c src/board.c src/parser.c src/movegen.c src/game.c \
            src/zobrist.c src/search.c


BENCH_SRC = src/bench.c src/board.c src/parser.c src/movegen.c src/game.c \
            src/zobrist.c src/search.c

UCI_SRC   = src/uci.c src/board.c src/parser.c src/movegen.c src/game.c \
            src/zobrist.c src/search.c

REL_DIR   = build/release
DBG_DIR   = build/debug
PERFT_DIR = build/perft
BENCH_DIR = build/bench
UCI_DIR   = build/uci

REL_OBJ   = $(SRC:src/%.c=$(REL_DIR)/%.o)
DBG_OBJ   = $(SRC:src/%.c=$(DBG_DIR)/%.o)
PERFT_OBJ = $(PERFT_SRC:src/%.c=$(PERFT_DIR)/%.o)
BENCH_OBJ = $(BENCH_SRC:src/%.c=$(BENCH_DIR)/%.o)
UCI_OBJ   = $(UCI_SRC:src/%.c=$(UCI_DIR)/%.o)

# Companion .d files emitted by -MMD. Pulled in at the bottom of the file.
DEPS      = $(REL_OBJ:.o=.d) $(DBG_OBJ:.o=.d) $(PERFT_OBJ:.o=.d) \
            $(BENCH_OBJ:.o=.d) $(UCI_OBJ:.o=.d)

REL_BIN   = citt
DBG_BIN   = citt-debug
PERFT_BIN = citt-perft
BENCH_BIN = citt-bench
UCI_BIN   = citt-uci

TEST_BIN  = tmovegen
TSEE_BIN  = tsee

TSEE_SRC  = tests/tsee.c src/board.c src/parser.c src/movegen.c src/game.c \
            src/zobrist.c src/search.c

.PHONY: all release debug clean test test-see perft bench uci

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

$(PERFT_BIN): $(PERFT_OBJ)
	$(CC) $(RELEASE_CFLAGS) -o $@ $^

$(PERFT_DIR)/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(RELEASE_CFLAGS) -c -o $@ $<

perft: $(PERFT_BIN)
	./$(PERFT_BIN)

$(BENCH_BIN): $(BENCH_OBJ)
	$(CC) $(RELEASE_CFLAGS) -o $@ $^

$(BENCH_DIR)/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(RELEASE_CFLAGS) -c -o $@ $<

bench: $(BENCH_BIN)
	./$(BENCH_BIN)

$(UCI_BIN): $(UCI_OBJ)
	$(CC) $(RELEASE_CFLAGS) -o $@ $^

$(UCI_DIR)/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(RELEASE_CFLAGS) -c -o $@ $<

# Just builds; meant to be driven by an external GUI / test harness over pipes.
uci: $(UCI_BIN)

$(TEST_BIN): tests/tmovegen.c src/board.c headers/board.h headers/game.h headers/movegen.h
	$(CC) $(BASE_CFLAGS) -g -O0 -o $@ tests/tmovegen.c src/board.c

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TSEE_BIN): $(TSEE_SRC)
	$(CC) $(BASE_CFLAGS) -g -O0 -o $@ $(TSEE_SRC)

test-see: $(TSEE_BIN)
	./$(TSEE_BIN)

clean:
	rm -rf build $(REL_BIN) $(DBG_BIN) $(PERFT_BIN) $(BENCH_BIN) $(UCI_BIN) $(TEST_BIN) $(TSEE_BIN)

-include $(DEPS)
