# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

CITT (**Chess In The Terminal**) is a C99 chess engine with a terminal UI. Board state is a mailbox plus parallel bitboards (magic-number sliders); search is iterative-deepening negamax with alpha-beta, TT, quiescence, and tapered evaluation. `README.md` covers the user-facing surface and a high-level walkthrough; this file covers the architectural detail Claude needs to make changes safely.

## Build & Test Commands

All build outputs land in `build/bin/`; nothing is written to the project root. `make clean` removes the whole `build/` tree.

**Compilation variants:**
- `make` or `make release` — optimized binary at `build/bin/citt` (O2, NDEBUG)
- `make debug` — debug binary at `build/bin/citt-debug` (symbols, O0, DEBUG enabled — DBG_PRINTF/DBG_ASSERT live, search depth 3)

**Testing:**
- `make test` — move generator test suite (`build/bin/tmovegen`, assertion-based)
- `make test-see` — SEE (Static Exchange Evaluation) test (`build/bin/tsee`)
- `make perft` — build & run perft validator vs reference leaf counts
  - `./build/bin/citt-perft [DEPTH] [--divide]` — depth override; `--divide` prints per-root-move subtotals for debugging movegen mismatches
- `make bench` — build & run search benchmark on 9-position suite
  - `./build/bin/citt-bench [DEPTH]` or `./build/bin/citt-bench -f "FEN" [DEPTH]` — depth override or single-FEN bench

**Interfaces:**
- `make uci` — build UCI binary at `build/bin/citt-uci`; speaks UCI protocol on stdin/stdout for testing vs other engines or external GUIs

## Architecture

**Board Representation:**
- Hybrid: 64-square mailbox (`uint8_t board[64]`) + parallel bitboards for fast piece/occupancy queries
- Piece encoding: bit 3 = color (0=white, 1=black), bits 0–2 = piece type (1=pawn, 2=bishop, 3=knight, 4=rook, 5=queen, 6=king)
- Bitboards per [color][piece_type] (7 rows, 2 colors); aggregates `occ[color]` and `occ_all`
- Bitboard helpers in `headers/bits.h`: popcount, lsb, msb, file/rank construction (0–63 little-endian rank-file)

**Attack Generation:**
- Precomputed leaper attacks (pawn, knight, king) indexed by square and color
- Magic bitboards for sliding pieces (bishop, rook): fancy magic with variable-sized lookup tables per square (bishop ~5KB, rook ~100KB total)
- `attacks_init()` must be called once before any query; idempotent and safe from multiple entry points
- API: `pawn_attacks()`, `knight_attacks()`, `king_attacks()`, `bishop_attacks(sq, occ)`, `rook_attacks(sq, occ)`, `queen_attacks()`

**Move Generation & Legality:**
- `append_pseudolegal_moves()` generates all pseudo-legal moves (captures, quiet, castling, en passant, promotion)
- `append_legal_moves()` filters via make/unmake: copy-makes, checks king legality, unmakes; restores position before returning
- Move struct: `from` (u8), `to` (u8), `flags` (bitmask: CAPTURE, ENP, CASTLE_K/Q, PROMO), `promo` (piece type for promotion)
- Castling legality (no king/rook movement, not in check, no check on intermediate square) enforced at generation time

**Move Application:**
- `make_move(game, move, undo_state)` applies move, records inverse in `undo_state`; pseudolegal OK, legality caller's responsibility
- `unmake_move(game, move, undo_state)` reverses via the undo snapshot
- Both maintain invariant: mailbox `board[]`, bitboards `pieces/occ/occ_all`, and incremental accumulators (hash, material, psqt_mg/eg, phase, bishop count, king squares, pawn_hash) all describe the same position
- Direct `board[]` mutations require `compute_eval_state()` to rebuild bitboards and accumulators

**Zobrist Hashing & Transposition:**
- Zobrist key tables: `z_piece[16][64]` (piece-by-square), `z_castle[16]` (castle rights), `z_ep_file[8]` (en-passant file), `z_side` (side to move)
- Called exactly once per position in make_move; `zobrist_compute()` rebuilds from scratch (FEN load, game_init)
- TT: 16-byte entries (key, score, move, depth, bound, age, pad); 16 MB default allocation
- TT probe/store: `tt_probe()` checks key + bound/depth; `tt_store()` replaces on shallower depth or stale age
- Move packing in TT: bits 0–6 = from, 7–13 = to, 14–15 = promo (flags discarded); `move_pack()` / `move_unpack()`

**Evaluation & Search:**
- Tapered eval: material + piece-square tables (MG/EG, separate arrays) interpolated by `phase` (remaining non-pawn material weight, 0..24)
- Incremental material, PSQT, phase, bishop pair, king squares tracked in game state; pawn-hash caches pawn-structure terms (doubled/isolated/passed, rook file openness, king shield) via 4K-entry table keyed by pawn Zobrist
- `search_root(game, max_depth, best_move)` — simple depth-N search entry point
- `search_run(game, limits, best_out, info_cb, ctx)` — rich entry point with time controls, iterative deepening, and callback for UCI-style info output
- `negamax(game, depth, ply, alpha, beta, can_null)` — core alpha-beta with null-move pruning (R=2, depth ≥3, non-zugzwang), LMR, PVS, aspiration windows (≥depth 5)
- Move ordering: TT move, MVV/LVA captures, queen/rook promotions, killers (2 per ply), history table [color][piece_type][to]
- Quiescence: stand-pat, delta pruning, captures + checks

**Game State & Main Loop:**
- `struct game`: board[64], bitboards (pieces[2][7], occ[2], occ_all), turn, castling (4-bit mask), ep_target (square index or `EP_NONE` = 0xFF), halfmove/fullmove counters, hash, material[2], psqt_mg/eg[2], phase, bishops[2], king_sq[2], pawn_hash
- `game_init()` — initialize to starting position, zero accumulators, call `zobrist_init()` and `attacks_init()`
- `game_step(game, ui_config)` — one turn: print board, prompt for input (SAN parser), find legal move, apply, return 0 if game over
- SAN parser (`parser.c`): handles rank/file disambiguation, captures, castling, promotion, annotations
- Entry point (`main.c`): `game_init()` → loop `game_step()` until game over; CLI flags control AI sides (human vs human, vs AI, self-play)
- UI side (config): `ui_config` struct holds ai_white/ai_black flags; separated from game state so positions remain self-contained

## Key Files & Entry Points

- **Board & pieces**: `headers/board.h`, `src/board.c` (mailbox init, print)
- **Bitboards**: `headers/bits.h` (primitives, rank/file), `src/attacks.c` (magic init, leaper/slider tables)
- **Move generation**: `headers/movegen.h`, `src/movegen.c` (pseudo/legal moves, king safety checks)
- **Game state**: `headers/game.h`, `src/game.c` (struct, make/unmake, eval accumulators)
- **Zobrist**: `headers/zobrist.h`, `src/zobrist.c` (hash tables, key computation)
- **Search & eval**: `headers/search.h`, `src/search.c` (negamax, qsearch, TT, eval, piece values, PST)
- **Parsing & play**: `src/parser.c` (SAN input), `src/main.c` (game loop)
- **UCI**: `src/uci.c` (protocol driver)
- **Perft/bench**: `src/perft.c`, `src/bench.c` (test harnesses)

## Testing & Validation

**Move generation**: Validated by perft against reference leaf counts. Perft exercises the full pipeline (pseudolegal generation, legality filtering, castling, en passant, promotion, make/unmake bookkeeping). Run before any change to movegen or `make_move`.

**Search**: `make bench` runs a fixed 9-position suite at depth 8. Use it as a regression check after changes to search, ordering, or eval — node counts should be stable to within noise for unrelated changes. `search_reset_state()` wipes per-search tables (killers, history, pawn cache) between positions; the TT is wiped separately via `tt_clear()`.

## Debug Build Features

With `make debug`, the binary includes:
- `DBG_PRINTF` calls (search iteration stats, Zobrist checks, move bookkeeping)
- `DBG_ASSERT` macro for invariant assertions (bitboard consistency, key drift)
- `AI_DEFAULT_DEPTH = 3` (depth 5 in release) for readable output
- Full symbol table and O0 for stepping

Check `ifdef DEBUG` blocks in `src/game.c` (bitboard consistency check every turn) and `src/search.c` for stats.

## Notes for Contributors

1. **Bitboard invariant**: After FEN load or direct `board[]` mutation, always call `compute_eval_state()` to sync bitboards and accumulators before search/eval.
2. **Magic init required**: `attacks_init()` must run before any attack query. Call it early in `main()` or `game_init()` as a precondition.
3. **Make/unmake pairing**: Every `make_move()` call binds an `undo_state` snapshot that must be passed to the matching `unmake_move()`. Do not mix or reuse snapshots.
4. **TT identity**: The TT array is indexed by `key & mask`; collisions are rejected by comparing the stored `key` to the probe key. The TT move is packed (from/to/promo only) and loses flags; `move_unpack` returns a `MOVE_QUIET` move regardless of the original.
5. **Phase interpolation**: `pst_lookup(color, piece_type, square)` mirrors the table for black. Phase saturates at `PHASE_MAX = 24` (both sides at full non-pawn material); eval interpolates between MG and EG as phase drops.
6. **En passant**: `ep_target` holds the *square* the capturing pawn would land on, or `EP_NONE` (0xFF). It is set on every double pawn push, and Zobrist XORs `z_ep_file[file_of(ep_target)]` whenever it is set — even when no capture is actually possible. This means two positions that differ only in a non-capturable ep target will not share a TT slot; refining this is a known eval-correctness improvement, not a bug.
