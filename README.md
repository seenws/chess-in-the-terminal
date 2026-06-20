# chess-in-the-terminal

CITT (**Chess In The Terminal**) is a lightweight chess engine written in C99, built from scratch. It runs in any terminal: moves are entered in standard algebraic notation and the board is rendered as ASCII after every move.

> **Status: on hiatus.** Development is paused until I can get better hardware. Training the NNUE evaluation at a useful search depth against Stockfish-labelled positions isn't feasible on my current machine — the data-generation pass is the bottleneck. The engine is complete and fully playable in the meantime; this is a pause on the NNUE work, not the project.

---

## How it works

The board is a 64-byte mailbox (`uint8_t board[64]`) alongside a set of [bitboards](https://www.chessprogramming.org/Bitboards): per `[color][piece_type]` plus per-color and total occupancy. The mailbox answers "what's on this square?"; the bitboards answer "where are all my knights?" and drive bulk attack generation. Squares use [little-endian rank-file](https://www.chessprogramming.org/Square_Mapping_Considerations#Little-Endian_Rank-File_Mapping) indexing (a1=0, h8=63). Each square byte encodes a piece as:

```
bit: 7 6 5 4 3 2 1 0
     0 0 0 0 C T T T

e.g. black rook  = 0b00001100
     white queen = 0b00000101
```

Sliding-piece attacks use [fancy magic bitboards](https://www.chessprogramming.org/Magic_Bitboards); leaper attacks (pawn, knight, king) are precomputed lookups. All tables are built once at startup by `attacks_init()`.

`make_move` and `unmake_move` keep the mailbox, the bitboards, and the incremental eval accumulators (material, PSQT, phase, bishop counts, king squares, Zobrist, pawn Zobrist) in sync. Direct mutation of `board[]` (FEN load and similar) must be followed by `compute_eval_state()` to rebuild everything else.

### Search

Iterative-deepening [Negamax](https://en.wikipedia.org/wiki/Negamax) with alpha-beta, plus:

- **Quiescence search** with stand-pat and delta pruning.
- **Transposition table** (16 MB default), Zobrist-keyed, age-aware, depth-preferred replacement; packs the best move for re-visits.
- **Aspiration windows** at depth ≥ 5.
- **Principal-variation search** (null-window scout, full re-search only on improvement).
- **Null-move pruning** (R=2) at depth ≥ 3, guarded against zugzwang.
- **Late-move reductions** for quiet, non-check moves after the first few.
- **Move ordering**: TT move, MVV/LVA captures, queen/rook promotions, killers (two per ply), history table.

### Evaluation

A tapered evaluation interpolates middlegame and endgame piece-square tables by remaining non-pawn material (`phase`). Material, PSQT, phase, bishop counts, king squares, and a pawn-only Zobrist hash are maintained incrementally inside make/unmake. Non-incremental terms — pawn structure, rook on open/semi-open files, king pawn-shield, bishop pair — are computed at the leaf and cached in a 4096-entry pawn-hash table.

---

## Building

Requires GCC and Make.

```bash
git clone https://github.com/seenws/chess-in-the-terminal
cd chess-in-the-terminal
make
```

This produces `build/bin/citt`. Every build target lands under `build/bin/`; nothing is written to the project root. `make clean` removes the whole `build/` tree.

---

## Playing

```bash
usage: ./build/bin/citt [-w] [-b] [-s] [-n PLIES]
  -v, --version       print version information
  -w, --ai-white      engine plays white
  -b, --ai-black      engine plays black
  -s, --selfplay      engine plays both sides (alias for -w -b)
  -n, --max-plies N   stop after N plies (0 = unlimited; debug aid for self-play)
default: human vs human
```

The board is printed after every move and you are prompted as the side to move. Enter moves in standard algebraic notation:

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

Trailing `+`, `#`, `!`, `?` are accepted and ignored. Invalid notation prints `Invalid notation.`; an illegal move prints `Illegal move.` and re-prompts. `Ctrl-D` exits. When a side has no legal reply the game prints `Checkmate.` or `Stalemate.` and exits.

The engine does not yet enforce the 50-move or threefold-repetition draws, so pair `--selfplay` with `-n` to bound debug runs, e.g. `./build/bin/citt -s -n 80`.

---

## Tests

```bash
make test     # assertion-based movegen suite (tests/tmovegen.c); prints OK on success
make debug    # build/bin/citt-debug: -g -O0 -DDEBUG, live DBG_PRINTF/DBG_ASSERT, AI depth 3
```

---

## Perft

The full move pipeline is validated by a [perft](https://www.chessprogramming.org/Perft) driver against published leaf counts from the starting position.

```bash
make perft                            # depth 5 (~4.9M nodes) with OK/FAIL vs reference
./build/bin/citt-perft 4              # depths 1..4
./build/bin/citt-perft 5 --divide     # per-root-move subtotals (the tool for narrowing a mismatch)
```

Movegen sustains ~8–16 Mnps depending on position density.

---

## Bench

A search benchmark drives iterative deepening over a fixed suite of nine positions, for regression-checking search/eval changes.

```bash
make bench
./build/bin/citt-bench 10                                        # depth 10 on the full suite
./build/bin/citt-bench -f "8/8/8/3k4/8/3K4/3P4/8 w - - 0 1" 12   # one position
```

Each row reports nodes, time, kn/s, best move, and score; the TT and ordering tables are wiped between rows. Full search runs at ~1–4 Mnps, with an effective branching factor around 2.3–2.5.

---

## UCI

A separate `citt-uci` binary speaks the [UCI](https://www.shredderchess.com/chess-features/uci-universal-chess-interface.html) protocol on stdin/stdout, for testing against external engines and GUIs. The main `citt` binary keeps its SAN loop unchanged.

```bash
make uci
```

Supported: `uci`, `isready`, `ucinewgame`, `position [startpos | fen ...] [moves ...]`, `go [depth | nodes | movetime | wtime/btime/winc/binc/movestogo | infinite]`, `stop`, `quit`, `setoption name Hash value N`, `setoption name EvalFile value <net>`. Single-threaded; `stop` and clock-based time controls are honored at the abort poll (every 4096 nodes). Time budget is `our_time / moves_to_go + 0.75 * inc`, hard-capped at 25% of remaining time.

---

## Strength

Estimated against Stockfish 16 at fixed `UCI_Elo` anchors (NNUE eval, 60 games per anchor, `tc=8+0.08`, 2026-06-20):

| Opponent | citt W–L–D  | Score |
|----------|:-----------:|:-----:|
| sf-2100  | 41 – 12 – 7 | 74.2% |
| sf-2300  | 28 – 23 – 9 | 54.2% |
| sf-2500  | 9 – 38 – 13 | 25.8% |

The ~50% crossover sits just above the 2300 anchor, placing CITT around **2300–2330** on Stockfish's `UCI_Elo` scale (an internal yardstick, not a FIDE/CCRL rating).

---

## License

MIT
