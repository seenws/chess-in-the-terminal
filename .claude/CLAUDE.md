# CLAUDE.md

This project prioritizes correctness, maintainability, readability,
portability, predictable performance, and design clarity. Improve how clearly
the code expresses the underlying system — not merely how it looks.

Good code is correct, simple, explicit, locally understandable, globally
coherent, easy to change safely, hard to misuse, easy to test, honest about
failure, and respectful of the reader. Prefer boring, transparent code over
clever, dense code. Every abstraction, helper, branch, and data structure must
earn its place. Preserve behavior exactly unless explicitly asked to change it,
and prefer small, reviewable changes over broad rewrites.

The detailed standards that used to live here are branched into skills so they
load on demand instead of bloating always-on context. Invoke the relevant
skill rather than working from memory:

- **`c-style`** — mechanical C conventions: GNU formatting, function-body
  structure, declarations/initialization, control flow, error handling,
  memory-allocation safety (esp. `realloc`), pointer ownership, and the
  pre-finish safety checklist. Use whenever writing or editing C.
- **`architect`** — design review: module boundaries, dependency direction,
  ownership/lifetime, API minimality, invariants, state management,
  testability, performance predictability, and portability/UB risk. Use
  before/after non-trivial edits, refactors, new modules, public-API changes,
  or performance-sensitive changes.
- **`comment-pragmatist`** — comment discipline: remove low-value comments,
  preserve essential rationale (invariants, UB, hardware quirks, algorithmic
  reasoning). Use after edits that add or change comments.

Task-specific verification and review skills:

- **`movegen-verify`** — run perft + movegen/SEE tests after movegen,
  `make_move`, or legality changes.
- **`perf-guard`** — bench node-count/knps regression check after search,
  ordering, or eval changes.
- **`portability-reviewer`** — audit for UB, integer/overflow, and
  GNU-ism-vs-C99 portability risk.
- **`invariant-auditor`** — verify the bitboard/accumulator/Zobrist invariants
  and make/unmake symmetry after state-mutation edits.

See the repository-root `CLAUDE.md` for project architecture, build/test
commands, and the engine's critical invariants.
