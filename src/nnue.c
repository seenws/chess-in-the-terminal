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
#include "debug.h"
#include "game.h"
#include "nnue.h"

/* Optional AVX2 acceleration of the back-layer dot products, dispatched at
   runtime. The kernel is compiled via a function target attribute so the
   default -O2 build stays portable (no -mavx2): hosts without AVX2 fall back
   to the scalar reference, which remains the source of truth. Limited to
   GNU C on x86 where the attribute and __builtin_cpu_supports exist.  */
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
  #include <immintrin.h>
  #define NNUE_X86_DISPATCH 1
#endif

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

/* Dot product of `n` uint8 activations with `n` int8 weights into int32.
   `n` is always a multiple of 32 here (512, 32). Both back-layer inputs are
   clipped-ReLU outputs in [0, 127], so they fit uint8.  */
typedef int32_t (*dot_fn)(const uint8_t *a, const int8_t *w, int n);

static int32_t
dot_scalar(const uint8_t *a, const int8_t *w, int n)
{
    int32_t s = 0;

    for (int k = 0; k < n; ++k)
        s += (int32_t)w[k] * a[k];
    return s;
}

#if NNUE_X86_DISPATCH
/* maddubs needs the unsigned operand <= 127 to avoid int16 saturation: a
   pair sums to at most 127*128*2 = 32512 < 32767, which holds because the
   activations are clipped to [0, 127]. madd_epi16 widens to int32; for
   n=512 the int32 accumulator peaks well under INT32_MAX.  */
__attribute__((target("avx2")))
static int32_t
dot_avx2(const uint8_t *a, const int8_t *w, int n)
{
    __m256i       acc  = _mm256_setzero_si256();
    const __m256i ones = _mm256_set1_epi16(1);

    for (int k = 0; k < n; k += 32) {
        __m256i va = _mm256_loadu_si256((const __m256i *)(a + k));
        __m256i vw = _mm256_loadu_si256((const __m256i *)(w + k));
        __m256i p  = _mm256_maddubs_epi16(va, vw);
        acc = _mm256_add_epi32(acc, _mm256_madd_epi16(p, ones));
    }

    __m128i lo = _mm256_castsi256_si128(acc);
    __m128i hi = _mm256_extracti128_si256(acc, 1);
    __m128i s  = _mm_add_epi32(lo, hi);
    s = _mm_add_epi32(s, _mm_shuffle_epi32(s, _MM_SHUFFLE(1, 0, 3, 2)));
    s = _mm_add_epi32(s, _mm_shuffle_epi32(s, _MM_SHUFFLE(2, 3, 0, 1)));
    return _mm_cvtsi128_si32(s);
}

/* Clipped ReLU of one perspective's NNUE_L1 int16 accumulator into uint8,
   clamped to [0, NNUE_CRELU_MAX]. packus interleaves the two 128-bit lanes,
   so permute4x64 (0xD8: qwords 0,2,1,3) restores sequential order to match
   the scalar crelu exactly.  */
__attribute__((target("avx2")))
static void
ft_crelu_avx2(uint8_t *dst, const int16_t *src)
{
    const __m256i zero = _mm256_setzero_si256();
    const __m256i hi   = _mm256_set1_epi16(NNUE_CRELU_MAX);

    for (int j = 0; j < NNUE_L1; j += 32) {
        __m256i a = _mm256_loadu_si256((const __m256i *)(src + j));
        __m256i b = _mm256_loadu_si256((const __m256i *)(src + j + 16));

        a = _mm256_min_epi16(_mm256_max_epi16(a, zero), hi);
        b = _mm256_min_epi16(_mm256_max_epi16(b, zero), hi);

        __m256i packed = _mm256_permute4x64_epi64(_mm256_packus_epi16(a, b), 0xD8);
        _mm256_storeu_si256((__m256i *)(dst + j), packed);
    }
}

#endif /* NNUE_X86_DISPATCH */

int
nnue_avx2_active(void)
{
#if NNUE_X86_DISPATCH
    static int cached = -1;

    if (cached < 0)
        cached = __builtin_cpu_supports("avx2") ? 1 : 0;
    return cached;
#else
    return 0;
#endif
}

/* Runs the three back affine layers over the feature-transformer output `ft`.
   `dot` selects scalar or SIMD; the surrounding integer math (bias, scale,
   clipped ReLU) is identical either way, so the result is bit-for-bit equal
   regardless of which dot product is used. */
static int
propagate_back(const struct nnue_network *net, const uint8_t *ft, dot_fn dot)
{
    uint8_t h0[NNUE_FC_OUT];
    for (int o = 0; o < NNUE_FC_OUT; ++o)
        h0[o] = crelu_scaled(net->fc0_b[o]
                             + dot(ft, net->fc0_w + (size_t)o * NNUE_FT_OUT, NNUE_FT_OUT));

    uint8_t h1[NNUE_FC_OUT];
    for (int o = 0; o < NNUE_FC_OUT; ++o)
        h1[o] = crelu_scaled(net->fc1_b[o]
                             + dot(h0, net->fc1_w + (size_t)o * NNUE_FC_OUT, NNUE_FC_OUT));

    int32_t out = net->fc2_b + dot(h1, net->fc2_w, NNUE_FC_OUT);

    /* Scale the raw output into centipawns. int64 guards the multiply.  */
    return (int)((int64_t)out * NNUE_OUTPUT_SCALE
                 / (NNUE_CRELU_MAX << NNUE_WEIGHT_SCALE_BITS));
}

int
nnue_propagate(const struct accumulator *acc, enum color stm)
{
    const struct nnue_network *net = nnue_net();
    const enum color nstm = (stm == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;

    /* Feature-transformer output: stm's half first, so the final score is
       side-to-move relative. Each half is a clipped-ReLU of one perspective's
       accumulator (int16 -> uint8 in [0,127]).  */
    uint8_t ft[NNUE_FT_OUT];

#if NNUE_X86_DISPATCH
    if (nnue_avx2_active()) {
        ft_crelu_avx2(ft,           acc->v[stm]);
        ft_crelu_avx2(ft + NNUE_L1, acc->v[nstm]);
#ifdef DEBUG
        for (int j = 0; j < NNUE_L1; ++j) {
            DBG_ASSERT(ft[j]           == crelu(acc->v[stm][j]));
            DBG_ASSERT(ft[NNUE_L1 + j] == crelu(acc->v[nstm][j]));
        }
#endif
        int v = propagate_back(net, ft, dot_avx2);
        DBG_ASSERT(v == propagate_back(net, ft, dot_scalar));
        return v;
    }
#endif

    for (int j = 0; j < NNUE_L1; ++j) {
        ft[j] = crelu(acc->v[stm][j]);
        ft[NNUE_L1 + j] = crelu(acc->v[nstm][j]);
    }
    return propagate_back(net, ft, dot_scalar);
}

int
nnue_evaluate(const struct game *g)
{
    /* Fast path: make/unmake have kept g->acc current, so just read it.
       The DEBUG build re-derives it from scratch and asserts they agree,
       catching any drift in the incremental update the moment it appears.  */
    if (g->acc.computed[COLOR_WHITE] && g->acc.computed[COLOR_BLACK]) {
#ifdef DEBUG
        struct accumulator chk;
        accumulator_refresh_all(&chk, g);
        for (int p = 0; p < 2; ++p)
            for (int j = 0; j < NNUE_L1; ++j)
                DBG_ASSERT(chk.v[p][j] == g->acc.v[p][j]);
#endif
        return nnue_propagate(&g->acc, g->turn);
    }

    /* Accumulator not seeded (e.g. net loaded after the position was built):
       fall back to a one-off refresh so eval is still correct.  */
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

    int ok = read_block(tmp.ft_w,   sizeof tmp.ft_w[0],  ft_count,                     f)
          && read_block(tmp.ft_b,   sizeof tmp.ft_b[0],  NNUE_L1,                      f)
          && read_block(tmp.fc0_w,  sizeof tmp.fc0_w[0], NNUE_FC_OUT * NNUE_FT_OUT,    f)
          && read_block(tmp.fc0_b,  sizeof tmp.fc0_b[0], NNUE_FC_OUT,                  f)
          && read_block(tmp.fc1_w,  sizeof tmp.fc1_w[0], NNUE_FC_OUT * NNUE_FC_OUT,    f)
          && read_block(tmp.fc1_b,  sizeof tmp.fc1_b[0], NNUE_FC_OUT,                  f)
          && read_block(tmp.fc2_w,  sizeof tmp.fc2_w[0], NNUE_FC_OUT,                  f)
          && read_block(&tmp.fc2_b, sizeof tmp.fc2_b,    1,                            f);

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
