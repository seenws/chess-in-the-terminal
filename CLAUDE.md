# CLAUDE.md

Guidance for Claude Code when working in this repository. This file is the
always-loaded essentials. Detailed standards live in skills (see
**Conventions & review** at the bottom) — invoke them rather than duplicating
their content here.

## Project Overview

CITT (**Chess In The Terminal**) is a C99 chess engine with a terminal UI.
Board state is a 64-square mailbox plus parallel bitboards (magic-number
sliders); search is iterative-deepening negamax with alpha-beta, a
transposition table, quiescence, and tapered evaluation. `README.md` covers the
user-facing surface; this file covers what's needed to change the engine safely.

## Build & Test Commands

All outputs land in `build/bin/`; nothing is written to the project root.
`make clean` removes the whole `build/` tree.

- `make` / `make release` — optimized binary `build/bin/citt` (O2, NDEBUG).
- `make debug` — `build/bin/citt-debug` (symbols, O0, DEBUG: DBG_PRINTF/
  DBG_ASSERT live, AI search depth 3).
- `make test` — movegen suite (`build/bin/tmovegen`, assertion-based).
- `make test-see` — SEE test (`build/bin/tsee`).
- `make perft` — perft validator vs reference leaf counts.
  `./build/bin/citt-perft [DEPTH] [--divide]` (`--divide` = per-root subtotals).
- `make bench` — 9-position search benchmark (depth 8).
  `./build/bin/citt-bench [DEPTH]` or `-f "FEN" [DEPTH]`.
- `make uci` — `build/bin/citt-uci`, speaks UCI on stdin/stdout.

## Architecture

**Board representation.** Hybrid: `uint8_t board[64]` mailbox + parallel
bitboards. Piece byte: bit 3 = color (0=white, 1=black), bits 0–2 = type
(1=pawn, 2=bishop, 3=knight, 4=rook, 5=queen, 6=king). Bitboards are
`pieces[color][type]` (7×2) plus aggregates `occ[color]` and `occ_all`.
Primitives (popcount, lsb/msb, file/rank, little-endian rank-file 0–63) in
`headers/bits.h`.

**Attack generation.** Precomputed leaper attacks (pawn/knight/king); fancy
magic bitboards for sliders (variable-sized per-square tables). `attacks_init()`
must run once before any query — idempotent, safe from multiple entry points.
API: `pawn_attacks`, `knight_attacks`, `king_attacks`, `bishop_attacks(sq,occ)`,
`rook_attacks(sq,occ)`, `queen_attacks`.

**Move generation & legality.** `append_pseudolegal_moves()` generates all
pseudo-legal moves; `append_legal_moves()` filters via copy-make / king-check /
unmake and restores the position. Move struct: `from`, `to`, `flags` (CAPTURE,
ENP, CASTLE_K/Q, PROMO bitmask), `promo`. Castling legality is enforced at
generation time.

**Move application.** `make_move(g, m, undo)` applies and records the inverse in
`undo`; `unmake_move(g, m, undo)` reverses it. Both keep mailbox, bitboards, and
incremental accumulators (hash, material, psqt_mg/eg, phase, bishop count, king
squares, pawn_hash) describing the same position. Pseudolegal input is OK;
legality is the caller's responsibility.

**Zobrist & transposition.** Key tables `z_piece[16][64]`, `z_castle[16]`,
`z_ep_file[8]`, `z_side`. Hash updated incrementally once per make_move;
`zobrist_compute()` rebuilds from scratch (FEN load, game_init). TT: 16-byte
entries, 16 MB default; probe/store keyed by `key & mask` with collision
rejection on stored `key`. TT move is packed (from/to/promo, flags discarded);
`move_unpack` returns a `MOVE_QUIET` move.

**Evaluation & search.** Tapered eval = material + PSQT (separate MG/EG arrays)
interpolated by `phase` (0..24). Incremental material/PSQT/phase/bishop-pair/
king-square; pawn-hash (4K entries, keyed by pawn Zobrist) caches pawn-structure
terms. Entry points: `search_root(g, depth, best)` (simple) and
`search_run(g, limits, best, info_cb, ctx)` (time controls, iterative deepening,
UCI info callback). Core: `negamax(g, depth, ply, alpha, beta, can_null)` with
null-move pruning, LMR, PVS, aspiration windows (≥depth 5). Ordering: TT move,
MVV/LVA + SEE captures, promotions, killers (2/ply), history
[color][type][to]. Quiescence: stand-pat, delta pruning, captures + checks.

**Game state & main loop.** `struct game` holds board, bitboards, turn, castling
(4-bit), ep_target (square or `EP_NONE` = 0xFF), halfmove/fullmove, hash, and
the accumulators above. `game_init()` sets the start position and calls
`zobrist_init()` + `attacks_init()`. `game_step()` runs one turn (print, SAN
prompt, apply). `ui_config` (ai_white/ai_black) is kept outside `struct game` so
positions stay self-contained.

## Critical invariants

These are the rules that silently corrupt state if broken. Honor them in any
change to make/unmake, movegen, search, or board mutation.

1. **Bitboard/accumulator invariant.** After FEN load or any direct `board[]`
   mutation, call `compute_eval_state()` to resync bitboards and accumulators
   before search/eval.
2. **Magic init.** `attacks_init()` must run before any attack query.
3. **Make/unmake pairing.** Every `make_move()` binds an `undo_state` that must
   go to the matching `unmake_move()`. Never mix or reuse snapshots.
4. **TT identity.** Indexed by `key & mask`; collisions rejected by comparing
   stored `key`. Packed TT moves lose flags (`move_unpack` → `MOVE_QUIET`).
5. **Phase.** `pst_lookup` mirrors tables for black; phase saturates at
   `PHASE_MAX = 24`; eval interpolates MG↔EG as phase drops.
6. **En passant.** `ep_target` is the square the capturing pawn lands on, or
   `EP_NONE`. Set on every double push; Zobrist XORs `z_ep_file[file]` whenever
   set, even when no capture is possible (two positions differing only in a
   non-capturable ep target won't share a TT slot — a known eval-correctness
   refinement, not a bug).

## Key files

- Board & pieces: `headers/board.h`, `src/board.c`
- Bitboards / attacks: `headers/bits.h`, `src/attacks.c`
- Move generation: `headers/movegen.h`, `src/movegen.c`
- Game state, make/unmake, accumulators: `headers/game.h`, `src/game.c`
- Zobrist: `headers/zobrist.h`, `src/zobrist.c`
- Search & eval: `headers/search.h`, `src/search.c`
- Parsing & play: `src/parser.c` (SAN/FEN), `src/main.c` (loop)
- UCI: `src/uci.c`
- Test harnesses: `src/perft.c`, `src/bench.c`, `tests/`

## Testing & validation

- **Movegen** is validated by perft against reference leaf counts (full
  pipeline: generation, legality, castling, ep, promotion, make/unmake). Run
  `make perft` before any change to movegen or `make_move`.
- **Search** regression: `make bench` (fixed 9-position suite, depth 8). Node
  counts should be stable to within noise for unrelated changes.
  `search_reset_state()` wipes per-search tables (killers, history, pawn cache);
  the TT is wiped separately via `tt_clear()`.
- **Debug build** (`make debug`) adds DBG_PRINTF stats, DBG_ASSERT invariant
  checks (bitboard consistency every turn in `src/game.c`, key drift in
  `src/search.c`), and `AI_DEFAULT_DEPTH = 3`.

## Conventions & review — where the detailed guidance lives

This project prioritizes correctness, maintainability, readability,
portability, predictable performance, and design clarity. Treat code as a
system of ideas: reveal intent through structure, naming, and data flow; avoid
cleverness, hidden coupling, and implicit state. Preserve behavior exactly
unless asked to change it; prefer small reviewable changes over rewrites.

Detailed standards are branched into skills — invoke the relevant one instead
of working from memory:

- **`c-style`** — mechanical C conventions: GNU formatting, function-body
  structure, declarations, control flow, error handling, **memory-allocation
  safety** (esp. `realloc`), pointer ownership, and the pre-finish safety
  checklist. Use whenever writing or editing C here.
- **`architect`** — design review: module boundaries, dependency direction,
  ownership/lifetime, API minimality, state management, testability,
  performance predictability, portability/UB. Use before/after non-trivial
  edits, refactors, new modules, public-API changes, or perf-sensitive changes.
- **`comment-pragmatist`** — comment review: removes low-value comments,
  preserves essential rationale (invariants, UB, hardware quirks, algorithmic
  reasoning). Use after edits that add or change comments.

When a change touches parsing, memory ownership, move generation, search, or
state mutation, be especially conservative and explain the behavioral risk.
