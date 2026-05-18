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

Moves are represented as a struct encoding origin, destination, and a bitmask of flags for captures, castling, en passant, and promotion. Each turn the engine generates the side-to-move's pseudolegal moves, parses the user's SAN input, matches it against that list, and applies the matching move.

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
| `O-O`    | kingside castle *(parsed; not generated)* |
| `O-O-O`  | queenside castle *(parsed; not generated)* |

Trailing `+`, `#`, `!`, `?` annotations are accepted and ignored. Invalid notation prints `Invalid notation.`; a move that doesn't match any pseudolegal move prints `Illegal move.` and re-prompts. Send EOF (`Ctrl-D`) to exit.

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

This builds the citt binary with debug mode enabled, play it as you would normally with ``./citt [-w] [-b]``.

---

## License

MIT
