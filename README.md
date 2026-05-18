# chess-in-the-terminal

CITT (**Chess In The Terminal**) is a lightweight chess game written in C99, built from scratch with an emphasis on doing things properly. It runs in any terminal: pieces are entered in standard algebraic notation, the board is rendered as ASCII after every move.

---

## How it works

The board is represented using [0x88 encoding](https://en.wikipedia.org/wiki/0x88), a 128-byte array that functions as two adjacent 8x8 boards. The left half holds the game state, the right half makes off-board detection branchless: if a square index has bit 3 of the upper nibble set (`sq & 0x88`), it's off the board.

```
bit: 7 6 5 4 3 2 1 0
     0 0 0 0 C T T T

e.g. black rook  = 0b00001011
     white queen = 0b00000101
```

Moves are represented as a struct encoding origin, destination, and a bitmask of flags for captures, castling, en passant, and promotion. Each turn the engine generates the side-to-move's legal moves (pseudolegal moves filtered by a copy-make king-safety check), parses the user's SAN input, matches it against that list, and applies the matching move. Castling generation enforces all three legality conditions (not in check, no pass-through-check, no into-check) at generation time.

Searching is done via a [Negamax](https://en.wikipedia.org/wiki/Negamax) algorithm with alpha-beta pruning and a transposition table.

---

## Building

Requires GCC and Make.

```bash
git clone https://github.com/seenws/chess-in-the-terminal
cd chess-in-the-terminal
make
```

This produces a `citt` binary in the project root.

---

## Playing
```bash
usage: ./citt [-w] [-b]
  -w, --ai-white    engine plays white
  -b, --ai-black    engine plays black
default: human vs human
```

The board is printed after every move and you are prompted for input as the side to move. Enter moves in standard algebraic notation; the parser accepts:

| Form     | Meaning                                  |
| -------- | ---------------------------------------- |
| `e4`     | pawn push                                |
| `Nf3`    | piece move (`N`, `B`, `R`, `Q`, `K`)     |
| `Rce8`   | file disambiguation                      |
| `R1e2`   | rank disambiguation                      |
| `Qh3f1`  | full origin square disambiguation        |
| `exd5`   | capture (the `x` is optional)            |
| `e8=Q`   | promotion (`Q`, `R`, `B`, `N`)           |
| `O-O`    | kingside castle                          |
| `O-O-O`  | queenside castle                         |

Trailing `+`, `#`, `!`, `?` annotations are accepted and ignored. Invalid notation prints `Invalid notation.`; a move that doesn't match any legal move (including moves that would leave the player's own king in check) prints `Illegal move.` and re-prompts. Send EOF (`Ctrl-D`) to exit. Once a side has no legal reply, the game prints `Checkmate.` or `Stalemate.` and the loop exits.

---

## Tests

The move generator has a small assertion-based test suite in `tests/tmovegen.c`. Run it with:

```bash
make test
```

This builds a separate `tmovegen` binary (with `-g -O0` for debugging) and executes it. On success the suite prints each test's move list and finishes with `OK`; any failure aborts via `assert`.

The move generator also has a debug mode including further information about movemaking.
```bash
make debug
```

This builds a `citt-debug` binary with `-g -O0 -DDEBUG`; run it as you would the release binary with `./citt-debug [-w] [-b]`. In debug builds `DBG_PRINTF` and `DBG_ASSERT` are live (Zobrist invariants are checked every turn, the search prints per-iteration stats), and `AI_DEFAULT_DEPTH` drops to 3 so the prints stay readable.

---

## Perft

The full move pipeline (pseudolegal generation, legality filter, castling, en passant, promotion, make_move bookkeeping) is validated by a [perft](https://www.chessprogramming.org/Perft) driver against the published leaf counts from the starting position.

```bash
make perft
```

This builds a separate `citt-perft` binary at release optimization and runs it at the default depth of 5 (~4.9M nodes). Output is a per-depth table of node counts, wall time, kn/s, and OK/FAIL against the reference. Pass an explicit depth or `--divide` to the binary directly:

```bash
./citt-perft 4              # depths 1..4
./citt-perft 5 --divide     # per-root-move subtotals at depth 5
```

Perft divide is the standard tool for narrowing a perft mismatch against a reference engine: follow the line whose subtotal disagrees.

Performance on author machine (i3 8350k @4.0GHz, 32gb 3200MHz DDR4):
```bash
perft divide: depth=5, starting position

  b1a3   198572
  b1c3   234656
  g1f3   233491
  g1h3   198502
  a2a3   181046
  a2a4   217832
  b2b3   215255
  b2b4   216145
  c2c3   222861
  c2c4   240082
  d2d3   328511
  d2d4   361790
  e2e3   402988
  e2e4   405385
  f2f3   178889
  f2f4   198473
  g2g3   217210
  g2g4   214048
  h2h3   181044
  h2h4   218829

total: 4865609  time: 1.003s
OK (matches reference for depth 5).
```
```bash
perft from starting position, depths 1..5
depth  nodes              time(s)    knps         status
1      20                 0.000      2828.9       OK
2      400                0.000      2692.2       OK
3      8902               0.002      5180.3       OK
4      197281             0.080      2475.6       OK
5      4865609            1.019      4775.7       OK
```

---

## License

MIT