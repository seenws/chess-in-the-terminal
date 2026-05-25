#ifndef CITT_HEADERS_ACCUMULATOR_H_
#define CITT_HEADERS_ACCUMULATOR_H_

#include <stdbool.h>
#include <stdint.h>

#include "board.h"   /* enum color */
#include "nnue.h"    /* NNUE_L1, nnue_net, nnue_feature_index */

struct game;

struct accumulator {
    int16_t v[2][NNUE_L1];
    bool    computed[2];
};

void accumulator_refresh(struct accumulator *acc, const struct game *g, enum color perspective);

void accumulator_refresh_all(struct accumulator *acc, const struct game *g);

#endif /* CITT_HEADERS_ACCUMULATOR_H_ */
