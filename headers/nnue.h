#ifndef CITT_HEADERS_NNUE_H_
#define CITT_HEADERS_NNUE_H_

#include <stdbool.h>
#include <stdint.h>

#include "board.h"   /* enum color */

struct game;
struct accumulator;

/* NNUE evaluation: a classic HalfKP network, kept entirely separate from
   the hand-tuned tapered eval. It is consulted only when a net is loaded
   (nnue_available()); otherwise the engine falls back to the classic eval.

   Architecture (see NNUE_NOTES.md):

     40960 inputs --(feature transformer)--> 256 per perspective
                                              |  concat [stm, !stm] -> 512
                                              v  clipped ReLU
                                             512 -> 32   (affine + clipped ReLU)
                                              32 -> 32   (affine + clipped ReLU)
                                              32 ->  1   (affine, scalar output)

   Quantization: int16 feature transformer + int16 accumulator; int8 back
   layers with int32 accumulation, each affine output >> WEIGHT_SCALE_BITS
   and clamped to [0, CRELU_MAX]. The scalar forward pass here is the
   reference implementation any future SIMD path must match bit-for-bit.  */

#define NNUE_INPUT          40960   /* 64 king squares * 640 piece-square slots */
#define NNUE_L1             256     /* feature transformer width per perspective */
#define NNUE_FT_OUT         512     /* 2 * NNUE_L1, the concatenated accumulator */
#define NNUE_FC_OUT         32      /* hidden width of both back affine layers */

#define NNUE_CRELU_MAX      127     /* clipped-ReLU upper bound (uint8 activations) */
#define NNUE_WEIGHT_SCALE_BITS 6    /* right shift applied after each back affine */
#define NNUE_OUTPUT_SCALE   16      /* final scaling into centipawns */

/* Loaded weights. The feature-transformer matrix is large (~21 MB) and
   heap-owned; the small back-layer blocks live inline. Layout conventions:
     ft_w  : [feature][NNUE_L1]            -- a feature's column is contiguous
     fc0_w : [NNUE_FC_OUT][NNUE_FT_OUT]    -- each output's row is contiguous
     fc1_w : [NNUE_FC_OUT][NNUE_FC_OUT]
     fc2_w : [NNUE_FC_OUT]                 -- single output  */
struct nnue_network {
    int16_t *ft_w;                              /* heap: NNUE_INPUT * NNUE_L1 */
    int16_t  ft_b[NNUE_L1];

    int8_t   fc0_w[NNUE_FC_OUT * NNUE_FT_OUT];
    int32_t  fc0_b[NNUE_FC_OUT];

    int8_t   fc1_w[NNUE_FC_OUT * NNUE_FC_OUT];
    int32_t  fc1_b[NNUE_FC_OUT];

    int8_t   fc2_w[NNUE_FC_OUT];
    int32_t  fc2_b;
};

/* Loads and validates a net from `path` (our own little-endian format;
   see src/nnue.c). Replaces any currently loaded net. Returns 0 on
   success, nonzero on open/format/allocation failure (leaving no net
   loaded). After success, nnue_available() is true.  */
int  nnue_load      (const char *path);

/* Releases the loaded net. Safe to call when none is loaded.  */
void nnue_unload    (void);

/* True when a net is loaded and nnue_evaluate may be used.  */
bool nnue_available (void);

/* Read access to the loaded net for the accumulator code. Returns a valid
   pointer only while nnue_available(); do not call otherwise.  */
const struct nnue_network *nnue_net(void);

/* HalfKP feature index for a non-king piece, from `perspective`'s point of
   view, with that side's king on `king_sq`. The board is vertically
   flipped (sq ^ 56) for the black perspective so "our side" always faces
   the same way. Kings are context, not features, and must not be passed.  */
int  nnue_feature_index(enum color perspective, int king_sq,
                        int piece_sq, uint8_t piece);

/* Runs the back layers over a filled accumulator and returns the score in
   centipawns from `stm`'s perspective (stm's half of the accumulator is
   fed first, so the result is already side-to-move relative).  */
int  nnue_propagate (const struct accumulator *acc, enum color stm);

/* Refresh-path evaluation: builds both accumulators from `g` and
   propagates. Returns centipawns from g->turn's perspective, matching the
   classic evaluate() contract. Caller must ensure nnue_available().  */
int  nnue_evaluate  (const struct game *g);

#endif /* CITT_HEADERS_NNUE_H_ */
