#ifndef CITT_HEADERS_GAME_H_
#define CITT_HEADERS_GAME_H_

#include <stdint.h>

#include "board.h"

struct move;

#define EP_NONE 0xFF

enum castle_rights {
    CASTLE_WK  = 1 << 0,
    CASTLE_WQ  = 1 << 1,
    CASTLE_BK  = 1 << 2,
    CASTLE_BQ  = 1 << 3,
    CASTLE_ALL = CASTLE_WK | CASTLE_WQ | CASTLE_BK | CASTLE_BQ,
};

/* Invariant: the bitboards (pieces/occ/occ_all) and the incremental
   accumulators (hash, material, psqt, phase, bishops, king_sq,
   pawn_hash) all describe the same position as board[]. make_move and
   unmake_move maintain this. A caller that mutates board[] directly
   must call compute_eval_state to resync the rest.  */
struct game {
    uint8_t    board[64];

    /* Piece bitboards indexed by [color][piece_type]; type 0 unused.  */
    uint64_t   pieces[2][7];
    uint64_t   occ[2];
    uint64_t   occ_all;

    enum color turn;
    uint8_t    castling;
    uint8_t    ep_target;
    uint8_t    halfmove;
    uint16_t   fullmove;
    uint64_t   hash;
    int16_t    material[2];
    int16_t    psqt_mg[2];
    int16_t    psqt_eg[2];
    int16_t    phase;
    int8_t     bishops[2];
    uint8_t    king_sq[2];
    uint64_t   pawn_hash;
};

/* Session-level CLI/UI configuration; held outside `struct game` so a
   position snapshot stays self-contained.  */
struct ui_config {
    uint8_t ai_white;
    uint8_t ai_black;
};

struct castle_rights_clear {
    uint8_t sq;
    uint8_t mask;
};

/* Pre-move snapshot for unmake_move. `captured` is board[m->to] before
   the move (EMPTY for non-captures and en passant).  */
struct undo_state {
    uint64_t hash;
    int16_t  material[2];
    int16_t  psqt_mg[2];
    int16_t  psqt_eg[2];
    int16_t  phase;
    int8_t   bishops[2];
    uint8_t  king_sq[2];
    uint64_t pawn_hash;
    uint8_t  captured;
    uint8_t  ep_target;
    uint8_t  castling;
    uint8_t  halfmove;
};

void game_init   (struct game *g);
int  game_step   (struct game *g, const struct ui_config *cfg);

/* Applies `m` to `g` and writes the inverse into `*undo`. Caller owns
   `undo` (typically a stack local); it must live until the matching
   unmake_move. Pseudolegal moves are accepted; legality is the
   caller's responsibility.  */
void make_move   (struct game *g, const struct move *m, struct undo_state *undo);

/* Reverses the most recent make_move. `m` and `undo` must be the pair
   handed to that make_move; using a different `m` or a stale `undo`
   silently corrupts `g`.  */
void unmake_move (struct game *g, const struct move *m, const struct undo_state *undo);

/* Rebuilds bitboards and incremental accumulators from board[]. Call
   after any direct mutation of board[] (e.g. FEN load) before searching
   or evaluating.  */
void compute_eval_state(struct game *g);

#endif /* CITT_HEADERS_GAME_H_ */
