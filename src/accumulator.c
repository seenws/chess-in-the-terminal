/* accumulator.c -- NNUE feature-transformer accumulator. Two paths keep the
   per-perspective int16 vectors in agreement with board[]:
     - refresh: rebuild a perspective from scratch (FEN load, and either side
       of a king move, where every feature index changes at once).
     - update:  apply one move's feature deltas in place; the hot path that
       make/unmake drive so eval never rebuilds at the leaves.  */

#include <stddef.h>
#include <string.h>

#include "accumulator.h"
#include "board.h"
#include "game.h"
#include "movegen.h"
#include "nnue.h"

/* AVX2 column add/subtract over the int16 accumulator, dispatched at runtime
   via nnue_avx2_active(); same guard rules as the propagate kernels in
   nnue.c (GNU C on x86, function target attribute, scalar fallback).  */
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
  #include <immintrin.h>
  #define NNUE_X86_DISPATCH 1

__attribute__((target("avx2")))
static void
acc_add_col_avx2(int16_t *dst, const int16_t *col, int mul)
{
    if (mul > 0)
        for (int j = 0; j < NNUE_L1; j += 16) {
            __m256i d = _mm256_loadu_si256((const __m256i *)(dst + j));
            __m256i c = _mm256_loadu_si256((const __m256i *)(col + j));
            _mm256_storeu_si256((__m256i *)(dst + j), _mm256_add_epi16(d, c));
        }
    else
        for (int j = 0; j < NNUE_L1; j += 16) {
            __m256i d = _mm256_loadu_si256((const __m256i *)(dst + j));
            __m256i c = _mm256_loadu_si256((const __m256i *)(col + j));
            _mm256_storeu_si256((__m256i *)(dst + j), _mm256_sub_epi16(d, c));
        }
}
#endif

/* dst[0..NNUE_L1) += col (mul > 0) or -= col (mul < 0). Integer adds, so the
   SIMD and scalar paths are bit-identical.  */
static void
acc_add_col(int16_t *dst, const int16_t *col, int mul)
{
#if NNUE_X86_DISPATCH
    if (nnue_avx2_active()) {
        acc_add_col_avx2(dst, col, mul);
        return;
    }
#endif
    if (mul > 0)
        for (int j = 0; j < NNUE_L1; ++j)
            dst[j] += col[j];
    else
        for (int j = 0; j < NNUE_L1; ++j)
            dst[j] -= col[j];
}

void
accumulator_refresh(struct accumulator *acc, const struct game *g, enum color perspective)
{
    const struct nnue_network *net = nnue_net();
    const int ksq = g->king_sq[perspective];

    memcpy(acc->v[perspective], net->ft_b, sizeof acc->v[perspective]);

    for (int sq = 0; sq < 64; ++sq) {
        uint8_t p = g->board[sq];

        if (is_empty(p) || piece_type(p) == PIECE_KING)
            continue;

        int idx = nnue_feature_index(perspective, ksq, sq, p);
        const int16_t *col = net->ft_w + (size_t)idx * NNUE_L1;

        acc_add_col(acc->v[perspective], col, +1);
    }

    acc->computed[perspective] = true;
}

void
accumulator_refresh_all(struct accumulator *acc, const struct game *g)
{
    accumulator_refresh(acc, g, COLOR_WHITE);
    accumulator_refresh(acc, g, COLOR_BLACK);
}

/* One non-king piece that changed squares. `from` is the square it left
   (-1 if it was only placed), `to` the square it landed on (-1 if it was
   only removed). Promotions split into two entries (pawn leaves, promoted
   piece arrives); captures and the castling rook add their own.  */
struct dirty {
    uint8_t piece;
    int     from;
    int     to;
};

/* acc->v[p] += mul * (feature column for `piece` on `sq`), mul in {+1,-1}.  */
static void
acc_feature(struct accumulator *acc, const struct nnue_network *net,
            enum color p, int ksq, int sq, uint8_t piece, int mul)
{
    int            idx = nnue_feature_index(p, ksq, sq, piece);
    const int16_t *col = net->ft_w + (size_t)idx * NNUE_L1;

    acc_add_col(acc->v[p], col, mul);
}

void
accumulator_update(struct game *g, const struct move *m,
                   uint8_t mover, uint8_t placed, uint8_t captured, int sign)
{
    const struct nnue_network *net   = nnue_net();
    const enum color           color = piece_color(mover);
    const enum color           opp   = (color == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
    const int                  king_moved = piece_type(mover) == PIECE_KING;

    struct dirty d[4];
    int          n = 0;

    if (m->flags & MOVE_PROMO) {
        d[n++] = (struct dirty){ mover,  m->from, -1     };   /* pawn leaves   */
        d[n++] = (struct dirty){ placed, -1,      m->to  };   /* promo arrives */
    } else {
        d[n++] = (struct dirty){ mover,  m->from, m->to  };   /* piece relocates */
    }

    if (!is_empty(captured))
        d[n++] = (struct dirty){ captured, m->to, -1 };       /* normal capture */

    if (m->flags & MOVE_ENP) {
        int cap_sq = (color == COLOR_WHITE) ? m->to - 8 : m->to + 8;
        d[n++] = (struct dirty){ encode_piece(opp, PIECE_PAWN), cap_sq, -1 };
    }

    if (m->flags & (MOVE_CASTLE_K | MOVE_CASTLE_Q)) {
        int ks = m->flags & MOVE_CASTLE_K;
        int rf = ks ? m->to + 1 : m->to - 2;
        int rt = ks ? m->to - 1 : m->to + 1;
        d[n++] = (struct dirty){ encode_piece(color, PIECE_ROOK), rf, rt };
    }

    for (int pi = 0; pi < 2; ++pi) {
        enum color p = (enum color)pi;

        /* The mover's own king square indexes every feature of its own
           perspective, so a king move (incl. castling) invalidates that whole
           vector -- rebuild it from the board, which already reflects the new
           position. The other perspective's king is unchanged, so its
           non-king deltas below still hold.  */
        if (king_moved && p == color) {
            accumulator_refresh(&g->acc, g, p);
            continue;
        }

        int ksq = g->king_sq[p];
        for (int i = 0; i < n; ++i) {
            if (piece_type(d[i].piece) == PIECE_KING)
                continue;                       /* kings are never features */
            if (d[i].from >= 0)
                acc_feature(&g->acc, net, p, ksq, d[i].from, d[i].piece, -sign);
            if (d[i].to >= 0)
                acc_feature(&g->acc, net, p, ksq, d[i].to,   d[i].piece,  sign);
        }
    }
}
