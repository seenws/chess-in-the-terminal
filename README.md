# chess-in-the-terminal

A chess engine and terminal client written in C, built from scratch with an emphasis on doing things properly.

---

## How it works

The board is represented using [0x88 encoding](https://en.wikipedia.org/wiki/0x88), a 128-byte array that functions as two adjacent 8x8 boards. The left half holds the game state, the right half makes off-board detection branchless: if a square index has bit 3 of the upper nibble set (`sq & 0x88`), it's off the board.

```
bit: 7 6 5 4 3 2 1 0
     0 0 0 0 C T T T

e.g. black rook  = 0b00001011
     white queen = 0b00000101
```

Moves are represented as a struct encoding origin, destination, and a bitmask of flags for captures, castling, en passant, and promotion.

---

## Current state

- [x] 0x88 board representation
- [x] Piece encoding/decoding
- [x] Board initialisation
- [x] Terminal board rendering
- [ ] Game state (turn, castling rights, en passant)
- [ ] Move generation
- [ ] Algebraic notation parser
- [ ] Game loop

---

## Building

Requires GCC and Make.

```bash
git clone https://github.com/seenws/chess-in-the-terminal
cd chess-in-the-terminal
make
./citt
```

---

## Project structure

```
chess-in-the-terminal/
├── src/
│   ├── main.c       # Entry point and game loop
│   ├── board.c      # Board init, rendering, piece encoding
│   ├── movegen.c    # Move representation and generation
│   └── parser.c     # Input handling and notation parsing
├── headers/
│   ├── board.h
│   ├── movegen.h
│   └── parser.h
└── Makefile
```

---

## License

MIT
