#ifndef CITT_HEADERS_GAME_H_
#define CITT_HEADERS_GAME_H_

#include <stdint.h>

#include "board.h"

struct move;

/* No-en-passant sentinel. 0xFF fails on_board() so movegen needs no
   special case.  */
#define EP_NONE 0xFF

enum castle_rights {
    CASTLE_WK  = 1 << 0,
    CASTLE_WQ  = 1 << 1,
    CASTLE_BK  = 1 << 2,
    CASTLE_BQ  = 1 << 3,
    CASTLE_ALL = CASTLE_WK | CASTLE_WQ | CASTLE_BK | CASTLE_BQ,
};

/* Incremental accumulators (material, psqt, phase, bishops, king_sq,
   pawn_hash) are maintained by make_move/unmake_move; callers that mutate
   board[] directly must call compute_eval_state to resync.  */
struct game {
    uint8_t    board[128];
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

/* Session-level CLI/UI configuration. Lives outside `struct game` because
   it never changes during play and has no place in a position snapshot.  */
struct ui_config {
    uint8_t ai_white;
    uint8_t ai_black;
};

/* Row of the castle_rights_clears table in game.c: a square whose change
   of occupancy clears the given castling-rights mask.  */
struct castle_rights_clear {
    uint8_t sq;
    uint8_t mask;
};

/* Pre-move snapshot for unmake_move. Sized small so per-node copies in
   search stay cheap.  `captured` is board[m->to] before the move (EMPTY
   for non-captures and en passant).  */
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
void make_move   (struct game *g, const struct move *m, struct undo_state *undo);
void unmake_move (struct game *g, const struct move *m, const struct undo_state *undo);

void compute_eval_state(struct game *g);

#endif
