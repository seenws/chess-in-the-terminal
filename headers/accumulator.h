#ifndef CITT_HEADERS_ACCUMULATOR_H_
#define CITT_HEADERS_ACCUMULATOR_H_

#include <stdbool.h>
#include <stdint.h>

#include "board.h"   /* enum color */
#include "nnue.h"    /* NNUE_L1, nnue_net, nnue_feature_index */

struct game;
struct move;

struct accumulator {
    int16_t v[2][NNUE_L1];
    bool    computed[2];
};

void accumulator_refresh(struct accumulator *acc, const struct game *g, enum color perspective);

void accumulator_refresh_all(struct accumulator *acc, const struct game *g);

/* Patches g->acc for one move instead of rebuilding it. Must be called with
   board[] and king_sq[] already in their post-move state (sign +1, from
   make_move) or post-unmake state (sign -1, from unmake_move). `mover` is the
   piece byte that sat on m->from before the move (a pawn for promotions),
   `placed` the piece byte on m->to after it, `captured` the piece on m->to
   before it (EMPTY for non-captures and en passant).  */
void accumulator_update(struct game *g, const struct move *m,
                        uint8_t mover, uint8_t placed, uint8_t captured, int sign);

#endif /* CITT_HEADERS_ACCUMULATOR_H_ */
