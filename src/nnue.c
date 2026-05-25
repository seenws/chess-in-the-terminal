/* nnue.c -- classic HalfKP NNUE: net loading, HalfKP feature indexing, and
   the scalar quantized forward pass. This scalar path is the reference
   implementation any future SIMD path must match bit-for-bit.

   Byte order: the on-disk format is raw little-endian; loading assumes a
   little-endian host. Revisit before claiming big-endian portability.  */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "accumulator.h"
#include "board.h"
#include "game.h"
#include "nnue.h"

/* On-disk header: 8-byte magic, then five uint32 fields validated against
   the compiled-in architecture so a mismatched net is rejected cleanly.  */
#define NNUE_MAGIC     "CITTNNUE"
#define NNUE_MAGIC_LEN 8
#define NNUE_VERSION   1u

/* Piece-square slots per king square: 2 colors * 5 non-king types * 64.  */
#define NNUE_PS_PER_KING (10 * 64)

static struct nnue_network g_net;      /* g_net.ft_w == NULL when unloaded */
static bool g_loaded;

/* Non-king piece_type -> 0..4 for HalfKP indexing; king/none never used.  */
static const int pt_to_idx[7] = {
    [PIECE_PAWN]   = 0,
    [PIECE_KNIGHT] = 1,
    [PIECE_BISHOP] = 2,
    [PIECE_ROOK]   = 3,
    [PIECE_QUEEN]  = 4,
};

int
nnue_feature_index(enum color perspective, int king_sq, int piece_sq, uint8_t piece)
{
    int flip  = (perspective == COLOR_WHITE) ? 0 : 56;
    int ksq_o = king_sq  ^ flip;
    int psq_o = piece_sq ^ flip;
    int rel   = (piece_color(piece) == perspective) ? 0 : 1;
    int p_idx = rel * 5 + pt_to_idx[piece_type(piece)];

    return ksq_o * NNUE_PS_PER_KING + p_idx * 64 + psq_o;
}

/* Clipped ReLU: clamp to [0, NNUE_CRELU_MAX], yielding a uint8 activation.  */
static inline uint8_t
crelu(int32_t x)
{
    if (x < 0)              return 0;
    if (x > NNUE_CRELU_MAX) return NNUE_CRELU_MAX;

    return (uint8_t)x;
}

/* Back-affine output shaping: divide by 2^WEIGHT_SCALE_BITS then clip.
   Division (not >>) keeps the negative case well-defined; the result is
   identical to an arithmetic shift once the clip to [0,127] is applied.  */
static inline uint8_t
crelu_scaled(int32_t x)
{
    return crelu(x / (1 << NNUE_WEIGHT_SCALE_BITS));
}

int
nnue_propagate(const struct accumulator *acc, enum color stm)
{
    const struct nnue_network *net = nnue_net();
    const enum color nstm = (stm == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;

    /* Feature-transformer output: stm's half first, so the final score is
       side-to-move relative.  */
    uint8_t ft[NNUE_FT_OUT];
    for (int j = 0; j < NNUE_L1; ++j) {
        ft[j] = crelu(acc->v[stm][j]);
        ft[NNUE_L1 + j] = crelu(acc->v[nstm][j]);
    }

    uint8_t h0[NNUE_FC_OUT];
    for (int o = 0; o < NNUE_FC_OUT; ++o) {
        const int8_t *w = net->fc0_w + (size_t)o * NNUE_FT_OUT;
        int32_t s = net->fc0_b[o];

        for (int k = 0; k < NNUE_FT_OUT; ++k)
            s += (int32_t)w[k] * ft[k];
        h0[o] = crelu_scaled(s);
    }

    uint8_t h1[NNUE_FC_OUT];
    for (int o = 0; o < NNUE_FC_OUT; ++o) {
        const int8_t *w = net->fc1_w + (size_t)o * NNUE_FC_OUT;
        int32_t s = net->fc1_b[o];

        for (int k = 0; k < NNUE_FC_OUT; ++k)
            s += (int32_t)w[k] * h0[k];
        h1[o] = crelu_scaled(s);
    }

    int32_t out = net->fc2_b;
    for (int k = 0; k < NNUE_FC_OUT; ++k)
        out += (int32_t)net->fc2_w[k] * h1[k];

    /* Scale the raw output into centipawns. int64 guards the multiply.  */
    return (int)((int64_t)out * NNUE_OUTPUT_SCALE
                 / (NNUE_CRELU_MAX << NNUE_WEIGHT_SCALE_BITS));
}

int
nnue_evaluate(const struct game *g)
{
    struct accumulator acc;

    accumulator_refresh_all(&acc, g);

    return nnue_propagate(&acc, g->turn);
}

bool
nnue_available(void)
{
    return g_loaded;
}

const struct nnue_network *
nnue_net(void)
{
    return &g_net;
}

void
nnue_unload(void)
{
    free(g_net.ft_w);
    g_net.ft_w = NULL;
    g_loaded   = false;
}

/* Reads exactly `count` elements of `size` bytes; returns 1 on success.  */
static int
read_block(void *dst, size_t size, size_t count, FILE *f)
{
    return fread(dst, size, count, f) == count;
}

int
nnue_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return 1;

    char     magic[NNUE_MAGIC_LEN];
    uint32_t hdr[5];

    if (fread(magic, 1, NNUE_MAGIC_LEN, f) != NNUE_MAGIC_LEN
        || memcmp(magic, NNUE_MAGIC, NNUE_MAGIC_LEN) != 0
        || !read_block(hdr, sizeof hdr[0], 5, f)) {
        fclose(f);
        return 2;
    }

    if (hdr[0] != NNUE_VERSION || hdr[1] != NNUE_INPUT || hdr[2] != NNUE_L1
        || hdr[3] != NNUE_FT_OUT || hdr[4] != NNUE_FC_OUT) {
        fclose(f);
        return 3;
    }

    const size_t ft_count = (size_t)NNUE_INPUT * NNUE_L1;

    struct nnue_network tmp;
    tmp.ft_w = malloc(ft_count * sizeof(int16_t));
    if (tmp.ft_w == NULL) {
        fclose(f);
        return 4;
    }

    int ok = read_block(tmp.ft_w,  sizeof tmp.ft_w[0],  ft_count,                     f)
          && read_block(tmp.ft_b,  sizeof tmp.ft_b[0],  NNUE_L1,                      f)
          && read_block(tmp.fc0_w, sizeof tmp.fc0_w[0], NNUE_FC_OUT * NNUE_FT_OUT,    f)
          && read_block(tmp.fc0_b, sizeof tmp.fc0_b[0], NNUE_FC_OUT,                  f)
          && read_block(tmp.fc1_w, sizeof tmp.fc1_w[0], NNUE_FC_OUT * NNUE_FC_OUT,    f)
          && read_block(tmp.fc1_b, sizeof tmp.fc1_b[0], NNUE_FC_OUT,                  f)
          && read_block(tmp.fc2_w, sizeof tmp.fc2_w[0], NNUE_FC_OUT,                  f)
          && read_block(&tmp.fc2_b, sizeof tmp.fc2_b,   1,                            f);

    fclose(f);

    if (!ok) {
        free(tmp.ft_w);
        return 5;
    }

    nnue_unload();        /* release any previously loaded net */
    g_net    = tmp;       /* transfers ownership of tmp.ft_w */
    g_loaded = true;
    return 0;
}
