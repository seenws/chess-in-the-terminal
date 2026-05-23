---
name: movegen-verify
description: Prove move generation and state bookkeeping still hold by running perft and the movegen/SEE test suites. Use after any change to movegen, make_move/unmake_move, legality filtering, castling, en passant, promotion, or SEE.
tools: Bash, Read, Grep
model: sonnet
---

# movegen-verify

You verify that move generation and the make/unmake pipeline are still correct
after a change. This is the mandatory gate before declaring any movegen-,
`make_move`-, or SEE-touching change done.

## What to run

1. **Perft** — exercises generation, legality filtering, castling, en passant,
   promotion, and make/unmake bookkeeping against known reference leaf counts:
   ```
   make perft
   ```
   `make perft` builds and runs `build/bin/citt-perft` over the depth ladder and
   compares each depth to the reference in `src/perft.c` (`perft_reference[]`).
   Treat any `FAIL` / `MISMATCH` line as a hard failure.

2. **Movegen unit suite** — assertion-based per-piece and special-move checks:
   ```
   make test
   ```

3. **SEE** — static exchange evaluation cases:
   ```
   make test-see
   ```

## On mismatch

When perft disagrees with the reference, localize before guessing:

```
./build/bin/citt-perft <DEPTH> --divide
```

`--divide` prints per-root-move subtotals. Compare against a trusted divide for
the same position (Stockfish `go perft`, or the chessprogramming.org tables) to
find which root move diverges, then recurse one level deeper on that move.

Common culprits, in rough order: en-passant target set/cleared in `make_move`,
castling-rights revocation (`castle_rights_clears`), promotion flag/piece
handling, and bitboard toggles in `bb_toggle` drifting from the mailbox.

## Reporting

Report the concrete result: which suites passed, exact node counts vs reference
at each depth, and any failing case verbatim. If a suite was skipped or could
not build, say so — do not imply success that was not observed. Do not edit
code from this skill; report findings and hand back.
