/* tnnue.c -- NNUE unit tests: file round-trip, header rejection, HalfKP
   feature indexing, accumulator refresh correctness, and a bias-only
   forward-pass check. Builds a deterministic synthetic net (no trained net
   required) so the scalar inference path is validated in isolation.  */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "accumulator.h"
#include "attacks.h"
#include "board.h"
#include "game.h"
#include "nnue.h"
#include "parser.h"

static int g_pass, g_fail;

#define CHECK(cond, msg)                                          \
    do {                                                          \
        if (cond) { printf("PASS  %s\n", (msg)); ++g_pass; }      \
        else      { printf("FAIL  %s\n", (msg)); ++g_fail; }      \
    } while (0)

#define TEST_NET  "/tmp/citt_tnnue.net"
#define TEST_ZERO "/tmp/citt_tnnue_zero.net"

static const size_t FT_COUNT = (size_t)NNUE_INPUT * NNUE_L1;

/* Small deterministic PRNG so generated nets are reproducible by seed.  */
static uint32_t rng;
static uint32_t
xrand(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static int
randrange(int lo, int hi)
{
    return lo + (int)(xrand() % (uint32_t)(hi - lo + 1));
}

/* Allocates ft_w and fills every weight/bias from `seed`. Caller frees ft_w.  */
static void
gen_net(struct nnue_network *n, uint32_t seed)
{
    rng = seed ? seed : 1u;

    n->ft_w = malloc(FT_COUNT * sizeof(int16_t));
    assert(n->ft_w != NULL);

    for (size_t i = 0; i < FT_COUNT; ++i)
        n->ft_w[i] = (int16_t)randrange(-64, 64);
    for (int i = 0; i < NNUE_L1; ++i)
        n->ft_b[i] = (int16_t)randrange(-64, 64);

    for (int i = 0; i < NNUE_FC_OUT * NNUE_FT_OUT; ++i)
        n->fc0_w[i] = (int8_t)randrange(-127, 127);
    for (int i = 0; i < NNUE_FC_OUT; ++i)
        n->fc0_b[i] = randrange(-1024, 1024);

    for (int i = 0; i < NNUE_FC_OUT * NNUE_FC_OUT; ++i)
        n->fc1_w[i] = (int8_t)randrange(-127, 127);
    for (int i = 0; i < NNUE_FC_OUT; ++i)
        n->fc1_b[i] = randrange(-1024, 1024);

    for (int i = 0; i < NNUE_FC_OUT; ++i)
        n->fc2_w[i] = (int8_t)randrange(-127, 127);
    n->fc2_b = randrange(-1024, 1024);
}

/* Writes a net in the on-disk format the loader expects. Returns 0 on success.  */
static int
write_net(const char *path, const struct nnue_network *n)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL)
        return 1;

    uint32_t hdr[5] = { 1u, NNUE_INPUT, NNUE_L1, NNUE_FT_OUT, NNUE_FC_OUT };

    int ok = fwrite("CITTNNUE", 1, 8, f) == 8
          && fwrite(hdr, sizeof hdr[0], 5, f) == 5
          && fwrite(n->ft_w,  sizeof n->ft_w[0],  FT_COUNT,                  f) == FT_COUNT
          && fwrite(n->ft_b,  sizeof n->ft_b[0],  NNUE_L1,                   f) == NNUE_L1
          && fwrite(n->fc0_w, sizeof n->fc0_w[0], NNUE_FC_OUT * NNUE_FT_OUT, f) == (size_t)NNUE_FC_OUT * NNUE_FT_OUT
          && fwrite(n->fc0_b, sizeof n->fc0_b[0], NNUE_FC_OUT,               f) == NNUE_FC_OUT
          && fwrite(n->fc1_w, sizeof n->fc1_w[0], NNUE_FC_OUT * NNUE_FC_OUT, f) == (size_t)NNUE_FC_OUT * NNUE_FC_OUT
          && fwrite(n->fc1_b, sizeof n->fc1_b[0], NNUE_FC_OUT,               f) == NNUE_FC_OUT
          && fwrite(n->fc2_w, sizeof n->fc2_w[0], NNUE_FC_OUT,               f) == NNUE_FC_OUT
          && fwrite(&n->fc2_b, sizeof n->fc2_b,   1,                         f) == 1;

    fclose(f);
    return ok ? 0 : 1;
}

static void
test_round_trip(void)
{
    struct nnue_network exp;
    gen_net(&exp, 0xC1771234u);
    assert(write_net(TEST_NET, &exp) == 0);

    CHECK(nnue_load(TEST_NET) == 0, "load round-trip net");
    CHECK(nnue_available(),         "available after load");

    const struct nnue_network *got = nnue_net();
    CHECK(memcmp(got->ft_w,  exp.ft_w,  FT_COUNT * sizeof(int16_t)) == 0, "ft_w  round-trip");
    CHECK(memcmp(got->ft_b,  exp.ft_b,  sizeof exp.ft_b)  == 0, "ft_b  round-trip");
    CHECK(memcmp(got->fc0_w, exp.fc0_w, sizeof exp.fc0_w) == 0, "fc0_w round-trip");
    CHECK(memcmp(got->fc0_b, exp.fc0_b, sizeof exp.fc0_b) == 0, "fc0_b round-trip");
    CHECK(memcmp(got->fc1_w, exp.fc1_w, sizeof exp.fc1_w) == 0, "fc1_w round-trip");
    CHECK(memcmp(got->fc1_b, exp.fc1_b, sizeof exp.fc1_b) == 0, "fc1_b round-trip");
    CHECK(memcmp(got->fc2_w, exp.fc2_w, sizeof exp.fc2_w) == 0, "fc2_w round-trip");
    CHECK(got->fc2_b == exp.fc2_b,                             "fc2_b round-trip");

    free(exp.ft_w);
}

static void
test_header_reject(void)
{
    nnue_unload();

    /* Wrong magic.  */
    FILE *f = fopen(TEST_NET, "wb");
    assert(f != NULL);
    uint32_t hdr[5] = { 1u, NNUE_INPUT, NNUE_L1, NNUE_FT_OUT, NNUE_FC_OUT };
    fwrite("XXXXXXXX", 1, 8, f);
    fwrite(hdr, sizeof hdr[0], 5, f);
    fclose(f);
    CHECK(nnue_load(TEST_NET) != 0, "reject bad magic");
    CHECK(!nnue_available(),        "no net after bad magic");

    /* Good magic, wrong dimensions.  */
    f = fopen(TEST_NET, "wb");
    assert(f != NULL);
    hdr[2] = NNUE_L1 + 1;   /* corrupt the L1 field */
    fwrite("CITTNNUE", 1, 8, f);
    fwrite(hdr, sizeof hdr[0], 5, f);
    fclose(f);
    CHECK(nnue_load(TEST_NET) != 0, "reject dimension mismatch");

    /* Missing file.  */
    CHECK(nnue_load("/tmp/citt_tnnue_does_not_exist.net") != 0, "reject missing file");
}

static void
test_feature_index(void)
{
    /* White perspective: white king e1=4, white pawn e2=12.
       ksq_o=4, psq_o=12, rel=0, p_idx=0 -> 4*640 + 0 + 12 = 2572.  */
    CHECK(nnue_feature_index(COLOR_WHITE, 4, 12, encode_piece(COLOR_WHITE, PIECE_PAWN)) == 2572,
          "feature index: white pawn, white perspective");

    /* Black perspective: black king e8=60, black knight g8=62. flip=56,
       ksq_o=4, psq_o=6, rel=0, p_idx=1 -> 4*640 + 64 + 6 = 2630.  */
    CHECK(nnue_feature_index(COLOR_BLACK, 60, 62, encode_piece(COLOR_BLACK, PIECE_KNIGHT)) == 2630,
          "feature index: black knight, black perspective");

    /* Enemy piece, white perspective: white king e1=4, black queen d8=59.
       ksq_o=4, psq_o=59, rel=1, p_idx=1*5+4=9 -> 4*640 + 9*64 + 59 = 3195.  */
    CHECK(nnue_feature_index(COLOR_WHITE, 4, 59, encode_piece(COLOR_BLACK, PIECE_QUEEN)) == 3195,
          "feature index: enemy queen, white perspective");
}

/* Independent re-derivation of one perspective, mirroring nothing from
   accumulator.c, used as the oracle for refresh correctness.  */
static void
reference_refresh(int16_t out[NNUE_L1], const struct game *g, enum color persp)
{
    const struct nnue_network *net = nnue_net();
    int                        ksq = g->king_sq[persp];

    for (int j = 0; j < NNUE_L1; ++j)
        out[j] = net->ft_b[j];

    for (int sq = 0; sq < 64; ++sq) {
        uint8_t p = g->board[sq];
        if (is_empty(p) || piece_type(p) == PIECE_KING)
            continue;

        int            idx = nnue_feature_index(persp, ksq, sq, p);
        const int16_t *col = net->ft_w + (size_t)idx * NNUE_L1;
        for (int j = 0; j < NNUE_L1; ++j)
            out[j] += col[j];
    }
}

static void
check_refresh_position(const struct game *g, const char *label)
{
    struct accumulator acc;
    accumulator_refresh_all(&acc, g);

    int16_t ref_w[NNUE_L1], ref_b[NNUE_L1];
    reference_refresh(ref_w, g, COLOR_WHITE);
    reference_refresh(ref_b, g, COLOR_BLACK);

    CHECK(memcmp(acc.v[COLOR_WHITE], ref_w, sizeof ref_w) == 0
       && memcmp(acc.v[COLOR_BLACK], ref_b, sizeof ref_b) == 0, label);
}

static void
test_refresh(void)
{
    struct nnue_network exp;
    gen_net(&exp, 0xBEEF01u);
    assert(write_net(TEST_NET, &exp) == 0);
    free(exp.ft_w);
    assert(nnue_load(TEST_NET) == 0);

    struct game g;
    game_init(&g);
    check_refresh_position(&g, "refresh matches oracle (startpos)");

    assert(parse_fen(&g,
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1") == 0);
    check_refresh_position(&g, "refresh matches oracle (kiwipete)");
}

static void
test_propagate_bias_only(void)
{
    /* All weights zero, all biases zero except fc2_b: the accumulator
       collapses to zero, every hidden activation is zero, and the raw
       output equals fc2_b. With fc2_b = 127*64 the centipawn result is
       fc2_b * OUTPUT_SCALE / (127 * 64) = OUTPUT_SCALE = 16.  */
    struct nnue_network zero;
    zero.ft_w = calloc(FT_COUNT, sizeof(int16_t));
    assert(zero.ft_w != NULL);
    memset(zero.ft_b,  0, sizeof zero.ft_b);
    memset(zero.fc0_w, 0, sizeof zero.fc0_w);
    memset(zero.fc0_b, 0, sizeof zero.fc0_b);
    memset(zero.fc1_w, 0, sizeof zero.fc1_w);
    memset(zero.fc1_b, 0, sizeof zero.fc1_b);
    memset(zero.fc2_w, 0, sizeof zero.fc2_w);
    zero.fc2_b = NNUE_CRELU_MAX << NNUE_WEIGHT_SCALE_BITS;   /* 127 * 64 = 8128 */

    assert(write_net(TEST_ZERO, &zero) == 0);
    free(zero.ft_w);
    assert(nnue_load(TEST_ZERO) == 0);

    struct game g;
    game_init(&g);

    struct accumulator acc;
    accumulator_refresh_all(&acc, &g);

    int cp = nnue_propagate(&acc, g.turn);
    int cp2 = nnue_propagate(&acc, g.turn);

    CHECK(cp == NNUE_OUTPUT_SCALE, "zero-weight net yields bias-only output");
    CHECK(cp == cp2,               "propagate is deterministic");
}

int
main(void)
{
    attacks_init();

    test_round_trip();
    test_header_reject();
    test_feature_index();
    test_refresh();
    test_propagate_bias_only();

    nnue_unload();
    remove(TEST_NET);
    remove(TEST_ZERO);

    printf("\n%d pass, %d fail\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
