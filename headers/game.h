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
void make_move   (struct game *g, const struct move *m, struct undo_state *undo);
void unmake_move (struct game *g, const struct move *m, const struct undo_state *undo);

void compute_eval_state(struct game *g);

#endif /* CITT_HEADERS_GAME_H_ */
