BASE_CFLAGS = -Wall -Wextra -std=c99 -D_POSIX_C_SOURCE=199309L -pthread -Iheaders -MMD -MP

# Release: optimized, NDEBUG strips assert(), no DBG_PRINTF call sites.
RELEASE_CFLAGS = $(BASE_CFLAGS) -O2 -DNDEBUG

# Debug: no opt, debug symbols, DEBUG enables DBG_PRINTF / DBG_ASSERT.
DEBUG_CFLAGS   = $(BASE_CFLAGS) -g -O0 -DDEBUG

SRC = src/main.c src/cli.c src/board.c src/parser.c src/movegen.c src/game.c \
      src/zobrist.c src/search.c src/thread.c src/attacks.c \
      src/nnue.c src/accumulator.c

PERFT_SRC = src/perft.c src/board.c src/parser.c src/movegen.c src/game.c \
            src/zobrist.c src/search.c src/thread.c src/attacks.c \
            src/nnue.c src/accumulator.c


BENCH_SRC = src/bench.c src/board.c src/parser.c src/movegen.c src/game.c \
            src/zobrist.c src/search.c src/thread.c src/attacks.c \
            src/nnue.c src/accumulator.c

UCI_SRC   = src/uci.c src/board.c src/parser.c src/movegen.c src/game.c \
            src/zobrist.c src/search.c src/thread.c src/attacks.c \
            src/nnue.c src/accumulator.c

DATAGEN_SRC = src/datagen.c src/board.c src/parser.c src/movegen.c src/game.c \
              src/zobrist.c src/search.c src/thread.c src/attacks.c \
              src/nnue.c src/accumulator.c

REL_DIR   = build/release
DBG_DIR   = build/debug
PERFT_DIR = build/perft
BENCH_DIR = build/bench
UCI_DIR   = build/uci
DATAGEN_DIR = build/datagen
BIN_DIR   = build/bin

REL_OBJ   = $(SRC:src/%.c=$(REL_DIR)/%.o)
DBG_OBJ   = $(SRC:src/%.c=$(DBG_DIR)/%.o)
PERFT_OBJ = $(PERFT_SRC:src/%.c=$(PERFT_DIR)/%.o)
BENCH_OBJ = $(BENCH_SRC:src/%.c=$(BENCH_DIR)/%.o)
UCI_OBJ   = $(UCI_SRC:src/%.c=$(UCI_DIR)/%.o)
DATAGEN_OBJ = $(DATAGEN_SRC:src/%.c=$(DATAGEN_DIR)/%.o)

# Companion .d files emitted by -MMD. Pulled in at the bottom of the file.
DEPS      = $(REL_OBJ:.o=.d) $(DBG_OBJ:.o=.d) $(PERFT_OBJ:.o=.d) \
            $(BENCH_OBJ:.o=.d) $(UCI_OBJ:.o=.d) $(DATAGEN_OBJ:.o=.d)

REL_BIN   = $(BIN_DIR)/citt
DBG_BIN   = $(BIN_DIR)/citt-debug
PERFT_BIN = $(BIN_DIR)/citt-perft
BENCH_BIN = $(BIN_DIR)/citt-bench
UCI_BIN   = $(BIN_DIR)/citt-uci
DATAGEN_BIN = $(BIN_DIR)/citt-datagen

TEST_BIN  = $(BIN_DIR)/tmovegen
TSEE_BIN  = $(BIN_DIR)/tsee
TNNUE_BIN = $(BIN_DIR)/tnnue

TSEE_SRC  = tests/tsee.c src/board.c src/parser.c src/movegen.c src/game.c \
            src/zobrist.c src/search.c src/thread.c src/attacks.c \
            src/nnue.c src/accumulator.c

TNNUE_SRC = tests/tnnue.c src/board.c src/parser.c src/movegen.c src/game.c \
            src/zobrist.c src/search.c src/thread.c src/attacks.c \
            src/nnue.c src/accumulator.c

LIB_DIR      = build/lib
CITTNNUE_LIB = $(LIB_DIR)/libcittnnue.so
LIB_SRC      = src/nnue_ctypes.c src/nnue.c src/accumulator.c

.PHONY: all release debug clean test test-see test-nnue perft bench uci datagen libcittnnue

all: release

release: $(REL_BIN)
debug:   $(DBG_BIN)

$(REL_BIN): $(REL_OBJ)
	@mkdir -p $(@D)
	$(CC) $(RELEASE_CFLAGS) -o $@ $^

$(DBG_BIN): $(DBG_OBJ)
	@mkdir -p $(@D)
	$(CC) $(DEBUG_CFLAGS) -o $@ $^

$(REL_DIR)/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(RELEASE_CFLAGS) -c -o $@ $<

$(DBG_DIR)/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(DEBUG_CFLAGS) -c -o $@ $<

$(PERFT_BIN): $(PERFT_OBJ)
	@mkdir -p $(@D)
	$(CC) $(RELEASE_CFLAGS) -o $@ $^

$(PERFT_DIR)/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(RELEASE_CFLAGS) -c -o $@ $<

perft: $(PERFT_BIN)
	./$(PERFT_BIN)

$(BENCH_BIN): $(BENCH_OBJ)
	@mkdir -p $(@D)
	$(CC) $(RELEASE_CFLAGS) -o $@ $^

$(BENCH_DIR)/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(RELEASE_CFLAGS) -c -o $@ $<

bench: $(BENCH_BIN)
	./$(BENCH_BIN)

$(DATAGEN_BIN): $(DATAGEN_OBJ)
	@mkdir -p $(@D)
	$(CC) $(RELEASE_CFLAGS) -o $@ $^

$(DATAGEN_DIR)/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(RELEASE_CFLAGS) -c -o $@ $<

# Builds the self-play data generator; run e.g. ./build/bin/citt-datagen --games 5000
datagen: $(DATAGEN_BIN)

# Shared library for the Python trainer (ctypes): exact feature indexing +
# the quantized evaluator. Built PIC, optimized, asserts off.
$(CITTNNUE_LIB): $(LIB_SRC) headers/nnue.h headers/accumulator.h headers/game.h headers/board.h
	@mkdir -p $(@D)
	$(CC) $(RELEASE_CFLAGS) -fPIC -shared -o $@ $(LIB_SRC)

libcittnnue: $(CITTNNUE_LIB)

$(UCI_BIN): $(UCI_OBJ)
	@mkdir -p $(@D)
	$(CC) $(RELEASE_CFLAGS) -o $@ $^

$(UCI_DIR)/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(RELEASE_CFLAGS) -c -o $@ $<

# Just builds; meant to be driven by an external GUI / test harness over pipes.
uci: $(UCI_BIN)

TEST_LINK = src/board.c src/attacks.c src/game.c src/zobrist.c src/search.c src/thread.c \
            src/nnue.c src/accumulator.c

$(TEST_BIN): tests/tmovegen.c $(TEST_LINK) headers/board.h headers/game.h headers/movegen.h headers/attacks.h headers/bits.h
	@mkdir -p $(@D)
	$(CC) $(BASE_CFLAGS) -g -O0 -o $@ tests/tmovegen.c $(TEST_LINK)

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TSEE_BIN): $(TSEE_SRC)
	@mkdir -p $(@D)
	$(CC) $(BASE_CFLAGS) -g -O0 -o $@ $(TSEE_SRC)

test-see: $(TSEE_BIN)
	./$(TSEE_BIN)

$(TNNUE_BIN): $(TNNUE_SRC)
	@mkdir -p $(@D)
	$(CC) $(BASE_CFLAGS) -g -O0 -o $@ $(TNNUE_SRC)

test-nnue: $(TNNUE_BIN)
	./$(TNNUE_BIN)

clean:
	rm -rf build

-include $(DEPS)
