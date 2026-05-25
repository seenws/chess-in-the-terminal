/* accumulator.c -- NNUE feature-transformer accumulator: rebuilds the
   per-perspective int16 vectors from the board (refresh path).  */

#include <stddef.h>

#include "accumulator.h"
#include "board.h"
#include "game.h"
#include "nnue.h"

void
accumulator_refresh(struct accumulator *acc, const struct game *g, enum color perspective)
{
    const struct nnue_network *net = nnue_net();
    const int ksq = g->king_sq[perspective];

    for (int j = 0; j < NNUE_L1; ++j)
        acc->v[perspective][j] = net->ft_b[j];

    for (int sq = 0; sq < 64; ++sq) {
        uint8_t p = g->board[sq];

        if (is_empty(p) || piece_type(p) == PIECE_KING)
            continue;

        int idx = nnue_feature_index(perspective, ksq, sq, p);
        const int16_t *col = net->ft_w + (size_t)idx * NNUE_L1;

        for (int j = 0; j < NNUE_L1; ++j)
            acc->v[perspective][j] += col[j];
    }

    acc->computed[perspective] = true;
}

void
accumulator_refresh_all(struct accumulator *acc, const struct game *g)
{
    accumulator_refresh(acc, g, COLOR_WHITE);
    accumulator_refresh(acc, g, COLOR_BLACK);
}
