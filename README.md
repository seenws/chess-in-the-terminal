# chess-in-the-terminal

CITT (**Chess In The Terminal**) is a lightweight chess game written in C99, built from scratch with an emphasis on doing things properly. It runs in any terminal: pieces are entered in standard algebraic notation, the board is rendered as ASCII after every move.

---

## How it works

The board is represented as a 64-byte mailbox (`uint8_t board[64]`) running alongside a set of [bitboards](https://www.chessprogramming.org/Bitboards): per `[color][piece_type]` plus per-color and total occupancy. The mailbox answers "what's on this square?" in one load; the bitboards answer "where are all my knights?" in one load and let attack generation work in bulk. Squares are indexed [little-endian rank-file](https://www.chessprogramming.org/Square_Mapping_Considerations#Little-Endian_Rank-File_Mapping) (a1=0, h8=63). Each square byte encodes a piece as:

```
bit: 7 6 5 4 3 2 1 0
     0 0 0 0 C T T T

e.g. black rook  = 0b00001100
     white queen = 0b00000101
```

Sliding-piece attacks (bishop, rook, queen) use [fancy magic bitboards](https://www.chessprogramming.org/Magic_Bitboards): per-square magic multipliers index into per-square attack tables keyed by the relevant occupancy. Leaper attacks (pawn, knight, king) are plain precomputed lookups. All tables are built once at startup by `attacks_init()`.

Moves are a struct encoding origin, destination, and a bitmask of flags for captures, castling, en passant, and promotion. Each turn the engine generates the side-to-move's legal moves (pseudolegal moves filtered by a make/unmake king-safety check), parses the user's SAN input, matches it against that list, and applies the matching move. Castling generation enforces all three legality conditions (not in check, no pass-through-check, no into-check) at generation time.

`make_move` and `unmake_move` keep the mailbox, the bitboards, and the incremental eval accumulators (material, PSQT, phase, bishop counts, king squares, Zobrist, pawn Zobrist) in sync. Direct mutation of `board[]` — FEN load and similar — must be followed by `compute_eval_state()` to rebuild everything else.

### Search

The engine runs an iterative-deepening [Negamax](https://en.wikipedia.org/wiki/Negamax) search with alpha-beta pruning. On top of the bare alpha-beta, it adds:

- **Quiescence search** with stand-pat and delta pruning, to play out forcing captures past the nominal depth and avoid horizon-effect blunders.
- **Transposition table** (16 MB by default), Zobrist-keyed, age-aware, with depth-preferred replacement. TT entries pack the best move so it can be tried first on re-visits.
- **Aspiration windows** at depth ≥ 5: re-search with a doubled window on fail-high or fail-low.
- **Principal-variation search** (null-window scout on non-first moves; full re-search only when the scout suggests an improvement).
- **Null-move pruning** (R=2) at depth ≥ 3, guarded against zugzwang by requiring non-pawn material.
- **Late-move reductions** for non-capture, non-promotion, non-check moves after the first few tried.
- **Move ordering**: TT move first, then MVV/LVA-scored captures, then queen/rook promotions, then killer moves (two slots per ply), then a history table indexed by (color, piece, to-square).

### Evaluation

A tapered evaluation interpolates middlegame and endgame piece-square tables by remaining non-pawn material (`phase`). Material, PSQT, phase, bishop counts, king squares, and a pawn-only Zobrist hash are maintained incrementally inside `make_move`/`unmake_move`, so leaf eval is O(non-incremental terms) rather than O(64).

Non-incremental terms — pawn structure (doubled / isolated / passed), rook on open and semi-open files, king pawn-shield, bishop pair — are computed at the leaf and cached in a 4096-entry pawn-hash table keyed by the pawn Zobrist, so most consecutive evaluations hit the cache.

---

## Building

Requires GCC and Make.

```bash
git clone https://github.com/seenws/chess-in-the-terminal
cd chess-in-the-terminal
make
```

This produces the binary at `build/bin/citt`. Every build target — release, debug, perft, bench, uci, and the test binaries — lands under `build/bin/`; nothing is written to the project root. `make clean` removes the whole `build/` tree.

---

## Playing
```bash
usage: ./build/bin/citt [-w] [-b] [-s] [-n PLIES]
  -w, --ai-white      engine plays white
  -b, --ai-black      engine plays black
  -s, --selfplay      engine plays both sides (alias for -w -b)
  -n, --max-plies N   stop after N plies (0 = unlimited; debug aid for self-play)
default: human vs human
```

`--selfplay` runs the engine against itself, useful for eyeballing search behavior or smoke-testing changes to evaluation or move generation. Because the engine does not (yet) enforce the 50-move or threefold-repetition draws, pair it with `-n` to bound debug runs, e.g. `./build/bin/citt -s -n 80`.

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

This builds `build/bin/citt-debug` with `-g -O0 -DDEBUG`; run it as you would the release binary, e.g. `./build/bin/citt-debug [-w] [-b]`. In debug builds `DBG_PRINTF` and `DBG_ASSERT` are live (Zobrist invariants are checked every turn, the search prints per-iteration stats), and `AI_DEFAULT_DEPTH` drops to 3 so the prints stay readable.

---

## Perft

The full move pipeline (pseudolegal generation, legality filter, castling, en passant, promotion, make_move bookkeeping) is validated by a [perft](https://www.chessprogramming.org/Perft) driver against the published leaf counts from the starting position.

```bash
make perft
```

This builds `build/bin/citt-perft` at release optimization and runs it at the default depth of 5 (~4.9M nodes). Output is a per-depth table of node counts, wall time, kn/s, and OK/FAIL against the reference. Pass an explicit depth or `--divide` to the binary directly:

```bash
./build/bin/citt-perft 4              # depths 1..4
./build/bin/citt-perft 5 --divide     # per-root-move subtotals at depth 5
```

Perft divide is the standard tool for narrowing a perft mismatch against a reference engine: follow the line whose subtotal disagrees.

Performance on author machine (i3 8350k @ 4.0 GHz, 32 GB 3200 MHz DDR4):

```bash
perft from starting position, depths 1..6
depth  nodes              time(s)    knps         status
1      20                 0.000      6136.8       OK
2      400                0.000      13862.9      OK
3      8902               0.001      9885.4       OK
4      197281             0.019      10328.5      OK
5      4865609            0.463      10500.8      OK
6      119060324          14.037     8482.2       OK
```

Movegen sustains ~8–16 Mnps depending on position density.

---

## Bench

A search benchmark drives iterative deepening over a fixed suite of nine FEN positions, useful for regression-checking changes to search or evaluation.

```bash
make bench
```

This builds `build/bin/citt-bench` at release optimization and runs each position to depth 8 (overridable). Pass `-f "FEN"` to bench a single custom position instead of the suite.

```bash
./build/bin/citt-bench 10                                 # depth 10 on the full suite
./build/bin/citt-bench -f "8/8/8/3k4/8/3K4/3P4/8 w - - 0 1" 12   # one position, depth 12
```

Each row reports node count, wall time, kn/s, the engine's chosen best move, and its score. The TT and per-search ordering tables are wiped between rows so each position is reproducible.

Representative output at depth 8:
```bash
bench suite: 9 positions, depth 8
position        nodes             time           knps          best       score
startpos        nodes=    112573  time= 0.091s   1233.0 knps  best=b1c3  score=10
kiwipete        nodes=    867758  time= 0.714s   1215.2 knps  best=d5e6  score=4
perft3          nodes=     49204  time= 0.021s   2345.9 knps  best=b4c4  score=10
perft4          nodes=    154063  time= 0.100s   1546.5 knps  best=g1g2  score=-743
perft5          nodes=    268401  time= 0.117s   2291.6 knps  best=d7c8q score=539
perft6          nodes=    641875  time= 0.465s   1381.5 knps  best=c3d5  score=34
wac001          nodes=    210149  time= 0.081s   2581.5 knps  best=g3g6  score=28997
sicilian        nodes=    383521  time= 0.374s   1026.5 knps  best=d7d5  score=-30
endgame-kpk     nodes=      3688  time= 0.001s   4350.4 knps  best=d3e3  score=145
---
total           nodes=   2691232  time= 1.964s   1370.5 knps
```

Full search (movegen + eval + move ordering + qsearch) runs at ~1–4 Mnps depending on position. The effective branching factor settles around 2.3–2.5 as iterative deepening warms the TT and killer/history tables.

---

## UCI

A separate `citt-uci` binary speaks the [Universal Chess Interface](https://www.shredderchess.com/chess-features/uci-universal-chess-interface.html) protocol on stdin/stdout. It exists for testing engine changes against external opponents (Stockfish, other engines) and for plugging the engine into any UCI-compatible GUI or tournament harness. The main `citt` binary keeps its SAN-driven user-vs-AI loop unchanged.

```bash
make uci
```

Supported commands: `uci`, `isready`, `ucinewgame`, `position [startpos | fen ...] [moves ...]`, `go [depth | nodes | movetime | wtime/btime/winc/binc/movestogo | infinite]`, `stop`, `quit`, `setoption name Hash value N`. Mate scores are reported as `score mate N` (UCI convention); the move list per `info` line is a PV extracted from the transposition table.

Single-threaded: `stop` is honored at the next abort poll inside the search (every 4096 nodes), and clock-based time controls drive search termination via an internal deadline polled the same way. The deadline formula is `our_time / moves_to_go + 0.75 * inc`, hard-capped at 25% of remaining time so the engine can't blow the clock on one move.

---

## Strength

Calibrated against Stockfish 16 at fixed `UCI_Elo` levels, 20 games per rung at 5+0.05 time control, on 2026-05-21:

| Opponent | citt W–L–D | Score |
|----------|:----------:|:-----:|
| sf-1400  | 17 – 2 – 1 | 87.5% |
| sf-1600  | 15 – 4 – 1 | 77.5% |
| sf-1800  | 14 – 5 – 1 | 72.5% |
| sf-2000  |  9 – 7 – 4 | 55.0% |
| sf-2200  |  8 – 10 – 2| 45.0% |
---

## License

MIT