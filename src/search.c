/* search.c -- alpha-beta search, quiescence, transposition table, eval.  */

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "attacks.h"
#include "board.h"
#include "debug.h"
#include "game.h"
#include "movegen.h"
#include "nnue.h"
#include "search.h"
#include "search_internal.h"
#include "thread.h"
#include "zobrist.h"

/* Backs the single-threaded search path (UCI/CLI/bench). Lazy SMP worker
   threads use their own per-thread contexts instead.  */
static struct search_ctx s_main_ctx;

/* Count of TT probes that returned a usable score.  */
static unsigned long long s_tt_hits;
/* Count of TT entries written during the current search.  */
static unsigned long long s_tt_stores;
/* Count of beta cutoffs across negamax + qsearch.  */
static unsigned long long s_cutoffs;

/* Polling rate (in nodes) for the abort/deadline check.  */
#define ABORT_POLL_NODES 4096

/* Control block for the single in-flight search. One search runs at a time
   (UCI is request/response), so a file-static instance is sufficient; every
   worker context points its `shared` here. search_signal_stop raises
   `s_shared.stop`.  */
static struct search_shared s_shared;

static uint64_t
now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static int
check_abort(struct search_ctx *ctx)
{
    const struct search_shared *sh = ctx->shared;

    if (ctx->aborted)                       return 1;
    if (sh->stop)                           { ctx->aborted = 1; return 1; }
    if (ctx->nodes >= sh->node_limit)       { ctx->aborted = 1; return 1; }
    if (now_ms() >= sh->deadline_ms)        { ctx->aborted = 1; return 1; }
    return 0;
}

void
search_signal_stop(void)
{
    s_shared.stop = 1;
}

/* Worker-thread count for Lazy SMP; 1 selects the single-threaded path.  */
static int s_threads = 1;

void
search_set_threads(int n)
{
    if (n < 1)                  n = 1;
    if (n > SEARCH_MAX_THREADS) n = SEARCH_MAX_THREADS;
    s_threads = n;
}

#define HISTORY_MAX 16384

#ifdef DEBUG
  #define INC(x) ((x)++)
#else
  #define INC(x) ((void)0)
#endif

/* Base of the transposition table; NULL when uninitialised.  */
static struct tt_entry *tt       = NULL;
/* Number of TT slots; always a power of two so tt_mask is valid.  */
static size_t           tt_count = 0;
/* Slot index mask (tt_count - 1).  */
static size_t           tt_mask  = 0;
/* Monotonically incrementing search counter used by replacement.  */
static uint8_t          tt_age   = 0;

static size_t
pow2_floor(size_t n)
{
    size_t p = 1;
    while ((p << 1) && (p << 1) <= n) p <<= 1;
    return p;
}

void
tt_init(size_t mb)
{
    tt_free();

    size_t bytes = mb * 1024 * 1024;
    size_t n = pow2_floor(bytes / sizeof(struct tt_entry));

    if (n < 1024) n = 1024;

    tt = calloc(n, sizeof(struct tt_entry));
    if (!tt) {
        tt_count = 0;
        tt_mask = 0;
        return;
    }

    tt_count = n;
    tt_mask = n - 1;
    tt_age = 0;
}

void
tt_free(void)
{
    free(tt);
    tt = NULL;
    tt_count = 0;
    tt_mask = 0;
}

void
tt_clear(void)
{
    if (tt)
        memset(tt, 0, tt_count * sizeof(struct tt_entry));
    tt_age = 0;
}

void
tt_new_search(void)
{
    tt_age++;
}

unsigned long long
search_get_nodes(void)
{
    return s_main_ctx.nodes;
}

void
search_reset_state(void)
{
    memset(s_main_ctx.killers,   0, sizeof(s_main_ctx.killers));
    memset(s_main_ctx.history,   0, sizeof(s_main_ctx.history));
    memset(s_main_ctx.pawn_hash, 0, sizeof(s_main_ctx.pawn_hash));
}

/* TT payload bit layout within the 64-bit `data` word:
     bits  0..15  score (stored as the low 16 bits of int16_t)
     bits 16..31  packed move
     bits 32..39  depth
     bits 40..47  bound
     bits 48..55  age                                                  */
static uint64_t
tt_pack(int score, uint16_t move, int depth, enum tt_bound bound, uint8_t age)
{
    return (uint64_t)(uint16_t)(int16_t)score
         | ((uint64_t)move          << 16)
         | ((uint64_t)(uint8_t)depth << 32)
         | ((uint64_t)(uint8_t)bound << 40)
         | ((uint64_t)age            << 48);
}

/* Sign-extend the low 16 bits of `data` back to the stored score without
   relying on implementation-defined unsigned-to-signed conversion.  */
static int
tt_unpack_score(uint64_t data)
{
    uint16_t u = (uint16_t)(data & 0xFFFFu);
    return (u < 0x8000u) ? (int)u : (int)u - 0x10000;
}

static uint16_t tt_unpack_move (uint64_t data) { return (uint16_t)((data >> 16) & 0xFFFFu); }
static int      tt_unpack_depth(uint64_t data) { return (int)((data >> 32) & 0xFFu); }
static int      tt_unpack_bound(uint64_t data) { return (int)((data >> 40) & 0xFFu); }
static uint8_t  tt_unpack_age  (uint64_t data) { return (uint8_t)((data >> 48) & 0xFFu); }

int
tt_probe(uint64_t key, int depth, int alpha, int beta, int *score_out, uint16_t *move_out)
{
    if (!tt) return 0;

    struct tt_entry *e = &tt[key & tt_mask];

    /* Snapshot both words once; the XOR check rejects an index collision
       or a torn read from a concurrent writer (treated as a miss).  */
    uint64_t data = e->data;
    if ((e->key_xor ^ data) != key) return 0;

    if (move_out) *move_out = tt_unpack_move(data);

    if (tt_unpack_depth(data) < depth) return 0;

    int s = tt_unpack_score(data);
    int bound = tt_unpack_bound(data);

    if (bound == TT_BOUND_EXACT) {
        INC(s_tt_hits);
        *score_out = s;
        return 1;
    }
    if (bound == TT_BOUND_LOWER && s >= beta) {
        INC(s_tt_hits);
        *score_out = s;
        return 1;
    }
    if (bound == TT_BOUND_UPPER && s <= alpha) {
        INC(s_tt_hits);
        *score_out = s;
        return 1;
    }

    return 0;
}

/* Replaces when the slot matches the key, holds a stale generation,
   or stores at greater-or-equal depth.  */
void
tt_store(uint64_t key, int depth, int score, enum tt_bound bound, uint16_t move)
{
    if (!tt) return;

    struct tt_entry *e = &tt[key & tt_mask];

    uint64_t old_data = e->data;
    uint64_t old_key = e->key_xor ^ old_data;

    if (old_key == key || tt_unpack_age(old_data) != tt_age
        || depth >= tt_unpack_depth(old_data)) {
        uint64_t data = tt_pack(score, move, depth, bound, tt_age);
        e->data = data;
        e->key_xor = key ^ data;
        INC(s_tt_stores);
    }
}

/* 2-bit encoding of the promotion piece; PIECE_BISHOP and any non-promo
   collapse to 0 (we never store the rare bishop promotion in the TT).  */
static const uint8_t promo_pack_table[7] = {
    [PIECE_QUEEN] = 1, [PIECE_ROOK] = 2, [PIECE_KNIGHT] = 3
};
/* Inverse table for promo_pack_table.  */
static const enum piece_type promo_unpack_table[4] = {
    PIECE_NONE, PIECE_QUEEN, PIECE_ROOK, PIECE_KNIGHT
};

static uint16_t
pack_promo(enum piece_type p)
{
    return (p <= PIECE_KING) ? promo_pack_table[p] : 0;
}

static enum piece_type
unpack_promo(uint16_t p)
{
    return promo_unpack_table[p & 3];
}

uint16_t
move_pack(const struct move *m)
{
    return (uint16_t)((m->from & 0x7F)
                   | ((m->to   & 0x7F) << 7)
                   | (pack_promo(m->promo) << 14));
}

struct move
move_unpack(uint16_t v)
{
    struct move m;
    m.from  = (uint8_t)(v & 0x7F);
    m.to    = (uint8_t)((v >> 7) & 0x7F);
    m.promo = unpack_promo((v >> 14) & 0x3);
    m.flags = MOVE_QUIET;
    return m;
}

void
move_to_uci(const struct move *m, char buf[6])
{
    /* Lower-case promotion suffix indexed by piece_type; index 0 unused.  */
    static const char promo_chars[] = " pbnrqk";

    buf[0] = (char)('a' + file_of(m->from));
    buf[1] = (char)('1' + rank_of(m->from));
    buf[2] = (char)('a' + file_of(m->to));
    buf[3] = (char)('1' + rank_of(m->to));

    if (m->promo != PIECE_NONE) {
        buf[4] = promo_chars[m->promo];
        buf[5] = '\0';
    } else {
        buf[4] = '\0';
    }
}

const int piece_value[7] = {
    0,      /* PIECE_NONE */
    100,    /* PAWN */
    330,    /* BISHOP */
    320,    /* KNIGHT */
    500,    /* ROOK */
    900,    /* QUEEN */
    20000,  /* KING -- sentinel; excluded from material[] since it cancels */
};

/* PHASE_MAX = 24 = starting position total (4N + 4B + 4R + 2Q).  */
const int phase_weight[7] = {
    0, 0, 1, 1, 2, 4, 0,
};

/* Michniewski "Simplified Evaluation" PSTs; only PAWN and KING differ
   between middlegame and endgame, so minor/major tables are shared.  */

#define PST_KNIGHT_DATA {                              \
    -50, -40, -30, -30, -30, -30, -40, -50,            \
    -40, -20,   0,   0,   0,   0, -20, -40,            \
    -30,   0,  10,  15,  15,  10,   0, -30,            \
    -30,   5,  15,  20,  20,  15,   5, -30,            \
    -30,   0,  15,  20,  20,  15,   0, -30,            \
    -30,   5,  10,  15,  15,  10,   5, -30,            \
    -40, -20,   0,   5,   5,   0, -20, -40,            \
    -50, -40, -30, -30, -30, -30, -40, -50,            \
}

#define PST_BISHOP_DATA {                              \
    -20, -10, -10, -10, -10, -10, -10, -20,            \
    -10,   0,   0,   0,   0,   0,   0, -10,            \
    -10,   0,   5,  10,  10,   5,   0, -10,            \
    -10,   5,   5,  10,  10,   5,   5, -10,            \
    -10,   0,  10,  10,  10,  10,   0, -10,            \
    -10,  10,  10,  10,  10,  10,  10, -10,            \
    -10,   5,   0,   0,   0,   0,   5, -10,            \
    -20, -10, -10, -10, -10, -10, -10, -20,            \
}

#define PST_ROOK_DATA {                                \
      0,   0,   0,   0,   0,   0,   0,   0,            \
      5,  10,  10,  10,  10,  10,  10,   5,            \
     -5,   0,   0,   0,   0,   0,   0,  -5,            \
     -5,   0,   0,   0,   0,   0,   0,  -5,            \
     -5,   0,   0,   0,   0,   0,   0,  -5,            \
     -5,   0,   0,   0,   0,   0,   0,  -5,            \
     -5,   0,   0,   0,   0,   0,   0,  -5,            \
      0,   0,   0,   5,   5,   0,   0,   0,            \
}

#define PST_QUEEN_DATA {                               \
    -20, -10, -10,  -5,  -5, -10, -10, -20,            \
    -10,   0,   0,   0,   0,   0,   0, -10,            \
    -10,   0,   5,   5,   5,   5,   0, -10,            \
     -5,   0,   5,   5,   5,   5,   0,  -5,            \
      0,   0,   5,   5,   5,   5,   0,  -5,            \
    -10,   5,   5,   5,   5,   5,   0, -10,            \
    -10,   0,   5,   0,   0,   0,   0, -10,            \
    -20, -10, -10,  -5,  -5, -10, -10, -20,            \
}

const int16_t pst_mg[7][64] = {
    [PIECE_NONE] = { 0 },

    [PIECE_PAWN] = {
          0,   0,   0,   0,   0,   0,   0,   0,
         50,  50,  50,  50,  50,  50,  50,  50,
         10,  10,  20,  30,  30,  20,  10,  10,
          5,   5,  10,  25,  25,  10,   5,   5,
          0,   0,   0,  20,  20,   0,   0,   0,
          5,  -5, -10,   0,   0, -10,  -5,   5,
          5,  10,  10, -20, -20,  10,  10,   5,
          0,   0,   0,   0,   0,   0,   0,   0,
    },
    [PIECE_KNIGHT] = PST_KNIGHT_DATA,
    [PIECE_BISHOP] = PST_BISHOP_DATA,
    [PIECE_ROOK]   = PST_ROOK_DATA,
    [PIECE_QUEEN]  = PST_QUEEN_DATA,
    [PIECE_KING] = {
        -30, -40, -40, -50, -50, -40, -40, -30,
        -30, -40, -40, -50, -50, -40, -40, -30,
        -30, -40, -40, -50, -50, -40, -40, -30,
        -30, -40, -40, -50, -50, -40, -40, -30,
        -20, -30, -30, -40, -40, -30, -30, -20,
        -10, -20, -20, -20, -20, -20, -20, -10,
         20,  20,   0,   0,   0,   0,  20,  20,
         20,  30,  10,   0,   0,  10,  30,  20,
    },
};

const int16_t pst_eg[7][64] = {
    [PIECE_NONE] = { 0 },

    [PIECE_PAWN] = {
          0,   0,   0,   0,   0,   0,   0,   0,
         80,  80,  80,  80,  80,  80,  80,  80,
         50,  50,  50,  50,  50,  50,  50,  50,
         30,  30,  30,  30,  30,  30,  30,  30,
         20,  20,  20,  20,  20,  20,  20,  20,
         10,  10,  10,  10,  10,  10,  10,  10,
         10,  10,  10,  10,  10,  10,  10,  10,
          0,   0,   0,   0,   0,   0,   0,   0,
    },
    [PIECE_KNIGHT] = PST_KNIGHT_DATA,
    [PIECE_BISHOP] = PST_BISHOP_DATA,
    [PIECE_ROOK]   = PST_ROOK_DATA,
    [PIECE_QUEEN]  = PST_QUEEN_DATA,
    [PIECE_KING] = {
        -50, -40, -30, -20, -20, -30, -40, -50,
        -30, -20, -10,   0,   0, -10, -20, -30,
        -30, -10,  20,  30,  30,  20, -10, -30,
        -30, -10,  30,  40,  40,  30, -10, -30,
        -30, -10,  30,  40,  40,  30, -10, -30,
        -30, -10,  20,  30,  30,  20, -10, -30,
        -30, -30,   0,   0,   0,   0, -30, -30,
        -50, -30, -30, -30, -30, -30, -30, -50,
    },
};

#define BISHOP_PAIR_BONUS          30
#define DOUBLED_PAWN_PENALTY       15
#define ISOLATED_PAWN_PENALTY      15
#define ROOK_OPEN_FILE_BONUS       20
#define ROOK_SEMI_OPEN_FILE_BONUS  10
#define KING_SHIELD_RANK1          15
#define KING_SHIELD_RANK2          10

/* Middlegame passed-pawn bonus indexed by ranks advanced from the start
   square (0..5; 5 = one push from promoting).  */
static const int passed_pawn_mg[6] = { 0,  5, 15, 25, 40,  70 };
/* Endgame passed-pawn bonus; ~2x middlegame because passers decide endgames.  */
static const int passed_pawn_eg[6] = { 0, 10, 20, 40, 70, 120 };

static void
collect_pawn_info(const struct game *g, struct pawn_info *pi)
{
    for (int c = 0; c < 2; ++c)
        for (int f = 0; f < 8; ++f) {
            pi->count[c][f] = 0;
            pi->max_rank[c][f] = -1;
            pi->min_rank[c][f] = 8;
        }

    for (int c = 0; c < 2; ++c) {
        uint64_t pawns = g->pieces[c][PIECE_PAWN];
        while (pawns) {
            int sq = pop_lsb(&pawns);
            int file = file_of(sq);
            int rank = rank_of(sq);

            pi->count[c][file]++;
            if (rank > pi->max_rank[c][file]) pi->max_rank[c][file] = (int8_t)rank;
            if (rank < pi->min_rank[c][file]) pi->min_rank[c][file] = (int8_t)rank;
        }
    }
}

static struct pawn_eval
evaluate_doubled_isolated(const struct pawn_info *pi)
{
    int mg = 0;
    int eg = 0;

    for (int c = 0; c < 2; ++c) {
        const int sign = (c == COLOR_WHITE) ? -1 : 1;

        for (int file = 0; file < 8; ++file) {
            int cnt = pi->count[c][file];
            if (cnt == 0) continue;

            if (cnt > 1) {
                int p = sign * (cnt - 1) * DOUBLED_PAWN_PENALTY;
                mg += p; eg += p;
            }

            int adj = (file > 0 ? pi->count[c][file - 1] : 0)
                    + (file < 7 ? pi->count[c][file + 1] : 0);
            if (adj == 0) {
                int p = sign * cnt * ISOLATED_PAWN_PENALTY;
                mg += p; eg += p;
            }
        }
    }

    struct pawn_eval pe = { mg, eg };
    return pe;
}

/* A white pawn at (file, rank) is passed iff no black pawn on the
   adjacent or same files is at a rank > rank, i.e. could block or
   capture it on its push path. Symmetric for black.  */
static struct pawn_eval
evaluate_passed_pawns(const struct game *g, const struct pawn_info *pi)
{
    int mg = 0;
    int eg = 0;

    uint64_t white = g->pieces[COLOR_WHITE][PIECE_PAWN];
    while (white) {
        int sq = pop_lsb(&white);
        int file = file_of(sq);
        int rank = rank_of(sq);

        int blk_F = pi->max_rank[COLOR_BLACK][file];
        int blk_Fm = (file > 0) ? pi->max_rank[COLOR_BLACK][file - 1] : -1;
        int blk_Fp = (file < 7) ? pi->max_rank[COLOR_BLACK][file + 1] : -1;

        if (blk_F <= rank && blk_Fm <= rank && blk_Fp <= rank) {
            int adv = rank - 1;
            mg += passed_pawn_mg[adv];
            eg += passed_pawn_eg[adv];
        }
    }

    uint64_t black = g->pieces[COLOR_BLACK][PIECE_PAWN];
    while (black) {
        int sq = pop_lsb(&black);
        int file = file_of(sq);
        int rank = rank_of(sq);

        int blk_F = pi->min_rank[COLOR_WHITE][file];
        int blk_Fm = (file > 0) ? pi->min_rank[COLOR_WHITE][file - 1] : 8;
        int blk_Fp = (file < 7) ? pi->min_rank[COLOR_WHITE][file + 1] : 8;

        if (blk_F >= rank && blk_Fm >= rank && blk_Fp >= rank) {
            int adv = 6 - rank;
            mg -= passed_pawn_mg[adv];
            eg -= passed_pawn_eg[adv];
        }
    }

    struct pawn_eval pe = { mg, eg };
    return pe;
}

static struct pawn_eval
evaluate_pawn_structure(const struct game *g, const struct pawn_info *pi)
{
    struct pawn_eval di = evaluate_doubled_isolated(pi);
    struct pawn_eval pp = evaluate_passed_pawns(g, pi);

    struct pawn_eval pe = { di.mg + pp.mg, di.eg + pp.eg };
    return pe;
}

static struct pawn_eval
evaluate_rook_files(const struct game *g, const struct pawn_info *pi)
{
    int mg = 0;
    int eg = 0;

    for (int c = 0; c < 2; ++c) {
        const enum color oppo_c = (c == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
        uint64_t rooks = g->pieces[c][PIECE_ROOK];

        while (rooks) {
            int sq   = pop_lsb(&rooks);
            int file = file_of(sq);
            int mine = pi->count[c][file];
            int oppo = pi->count[oppo_c][file];

            int bonus = 0;
            if (mine == 0)
                bonus = (oppo == 0) ? ROOK_OPEN_FILE_BONUS : ROOK_SEMI_OPEN_FILE_BONUS;
            if (c == COLOR_BLACK) bonus = -bonus;
            mg += bonus; eg += bonus;
        }
    }

    struct pawn_eval pe = { mg, eg };
    return pe;
}

/* Middlegame-only; an active endgame king does not care about its shield.  */
static struct pawn_eval
evaluate_king_shield(const struct game *g, const struct pawn_info *pi)
{
    int mg = 0;

    for (int c = 0; c < 2; ++c) {
        uint64_t my_pawns = g->pieces[c][PIECE_PAWN];
        int      king_sq  = g->king_sq[c];
        int      k_file   = file_of(king_sq);
        int      k_rank   = rank_of(king_sq);

        int dir = (c == COLOR_WHITE) ? 1 : -1;
        int rank1 = k_rank + dir;
        int rank2 = k_rank + 2 * dir;

        int side_mg = 0;

        for (int df = -1; df <= 1; ++df) {
            int f = k_file + df;
            if (f < 0 || f > 7) continue;

            if (rank1 >= 1 && rank1 <= 6
                && (my_pawns & bit_of(make_sq(rank1, f))))
                side_mg += KING_SHIELD_RANK1;
            if (rank2 >= 1 && rank2 <= 6
                && (my_pawns & bit_of(make_sq(rank2, f))))
                side_mg += KING_SHIELD_RANK2;
        }

        if (c == COLOR_WHITE) mg += side_mg;
        else                  mg -= side_mg;
    }

    (void)pi;

    struct pawn_eval pe = { mg, 0 };
    return pe;
}

/* Looks up the pawn-structure eval in the per-search pawn cache; on a
   miss, computes it and stores the result.  */
static struct pawn_eval
pawn_eval_cached(struct search_ctx *ctx, const struct game *g, struct pawn_info *pi_out)
{
    struct pawn_hash_entry *e = &ctx->pawn_hash[g->pawn_hash & (PAWN_HASH_SIZE - 1)];

    if (e->key == g->pawn_hash) {
        *pi_out = e->pi;
        struct pawn_eval r = { e->mg, e->eg };
        return r;
    }

    collect_pawn_info(g, pi_out);
    struct pawn_eval r = evaluate_pawn_structure(g, pi_out);

    e->key = g->pawn_hash;
    e->pi  = *pi_out;
    e->mg  = (int16_t)r.mg;
    e->eg  = (int16_t)r.eg;

    return r;
}

static int
evaluate(struct search_ctx *ctx, const struct game *g)
{
    if (nnue_available())
        return nnue_evaluate(g);

    int mat = (int)g->material[COLOR_WHITE] - (int)g->material[COLOR_BLACK];
    int mg = (int)g->psqt_mg[COLOR_WHITE] - (int)g->psqt_mg[COLOR_BLACK];
    int eg = (int)g->psqt_eg[COLOR_WHITE] - (int)g->psqt_eg[COLOR_BLACK];

    struct pawn_info pi;
    struct pawn_eval ps = pawn_eval_cached(ctx, g, &pi);
    struct pawn_eval rf = evaluate_rook_files(g, &pi);
    struct pawn_eval ks = evaluate_king_shield(g, &pi);

    mg += ps.mg + rf.mg + ks.mg;
    eg += ps.eg + rf.eg + ks.eg;

    /* Mass-promotion positions can push phase past PHASE_MAX; clamp.  */
    int phase = g->phase;
    if (phase > PHASE_MAX) phase = PHASE_MAX;
    if (phase < 0)         phase = 0;

    int psqt = (mg * phase + eg * (PHASE_MAX - phase)) / PHASE_MAX;

    int pair = 0;
    if (g->bishops[COLOR_WHITE] >= 2) pair += BISHOP_PAIR_BONUS;
    if (g->bishops[COLOR_BLACK] >= 2) pair -= BISHOP_PAIR_BONUS;

    int score = mat + psqt + pair;

    return (g->turn == COLOR_WHITE) ? score : -score;
}

static int
mover_in_check(const struct game *g)
{
    enum color moved = (g->turn == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
    return king_in_check(g, moved);
}

/* Bitboard of every piece of either color attacking `to` under occupancy
   `occ`. Slider rays honor `occ`, so clearing a square from `occ` between
   calls reveals any x-ray attacker that stood behind it.  */
static uint64_t
attackers_to(const struct game *g, int to, uint64_t occ)
{
    const uint64_t knights = g->pieces[COLOR_WHITE][PIECE_KNIGHT]
                           | g->pieces[COLOR_BLACK][PIECE_KNIGHT];
    const uint64_t kings   = g->pieces[COLOR_WHITE][PIECE_KING]
                           | g->pieces[COLOR_BLACK][PIECE_KING];
    const uint64_t diag    = g->pieces[COLOR_WHITE][PIECE_BISHOP]
                           | g->pieces[COLOR_BLACK][PIECE_BISHOP]
                           | g->pieces[COLOR_WHITE][PIECE_QUEEN]
                           | g->pieces[COLOR_BLACK][PIECE_QUEEN];
    const uint64_t ortho   = g->pieces[COLOR_WHITE][PIECE_ROOK]
                           | g->pieces[COLOR_BLACK][PIECE_ROOK]
                           | g->pieces[COLOR_WHITE][PIECE_QUEEN]
                           | g->pieces[COLOR_BLACK][PIECE_QUEEN];

    /* A pawn of color c attacks `to` from the squares a pawn of the
       opposite color on `to` would attack, so the polarity is flipped.  */
    uint64_t attackers = 0;
    attackers |= pawn_attacks(to, COLOR_BLACK) & g->pieces[COLOR_WHITE][PIECE_PAWN];
    attackers |= pawn_attacks(to, COLOR_WHITE) & g->pieces[COLOR_BLACK][PIECE_PAWN];
    attackers |= knight_attacks(to)       & knights;
    attackers |= king_attacks(to)         & kings;
    attackers |= bishop_attacks(to, occ)  & diag;
    attackers |= rook_attacks(to, occ)    & ortho;

    return attackers;
}

/* Cheapest attacker in `side_attackers` (already masked to one side's
   pieces): writes its single bit to `*bit_out`, or returns PIECE_NONE and
   leaves `*bit_out` untouched. The scan order is by value, so knight
   precedes bishop and must not be reordered to match the piece_type enum.  */
static enum piece_type
see_least_valuable(const struct game *g, enum color side,
                   uint64_t side_attackers, uint64_t *bit_out)
{
    static const enum piece_type order[6] = {
        PIECE_PAWN, PIECE_KNIGHT, PIECE_BISHOP,
        PIECE_ROOK, PIECE_QUEEN,  PIECE_KING,
    };

    for (int i = 0; i < 6; ++i) {
        uint64_t set = side_attackers & g->pieces[side][order[i]];
        if (set) {
            *bit_out = bit_of(lsb(set));
            return order[i];
        }
    }

    return PIECE_NONE;
}

/* Reverse-pass minimax: each side picks the better of "stop now" and
   "continue the swap-off". Final score lands in gain[0].  */
static int
see_unfold_gain(int *gain, int d)
{
    while (d > 0) {
        int stand_pat = -gain[d - 1];
        int continue_ = gain[d];

        gain[d - 1] = -(stand_pat > continue_ ? stand_pat : continue_);
        --d;
    }
    return gain[0];
}

int
see(const struct game *g, const struct move *m)
{
    const int        to    = m->to;
    const int        from  = m->from;
    enum color       mover;
    enum color       side;
    enum piece_type  on_to;
    uint64_t         occ;
    int              gain[32];
    int              d;

    if (!(m->flags & MOVE_CAPTURE))
        return 0;

    mover = piece_color(g->board[from]);
    on_to = piece_type(g->board[from]);

    gain[0] = (m->flags & MOVE_ENP)
        ? piece_value[PIECE_PAWN]
        : piece_value[piece_type(g->board[to])];

    /* The mover vacates `from`; `to` stays occupied throughout the swap-off
       as successive capturers replace each other on it.  */
    occ = g->occ_all ^ bit_of(from);
    if (m->flags & MOVE_ENP)
        occ ^= bit_of(to + (mover == COLOR_WHITE ? -8 : 8));

    if (m->flags & MOVE_PROMO) {
        gain[0] += piece_value[m->promo] - piece_value[PIECE_PAWN];
        on_to = m->promo;
    }

    side = (mover == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
    d = 0;

    while (1) {
        uint64_t        attackers = attackers_to(g, to, occ) & occ;
        uint64_t        side_attackers = attackers & g->occ[side];
        uint64_t        lva_bit = 0;
        enum piece_type lva;

        if (side_attackers == 0)
            break;

        lva = see_least_valuable(g, side, side_attackers, &lva_bit);

        /* A king may recapture only when it lands on an undefended square;
           recapturing into check is illegal. Removing the king from `occ`
           may itself reveal an x-ray defender, so test against that.  */
        if (lva == PIECE_KING) {
            const enum color enemy = (side == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
            uint64_t         occ_after = occ ^ lva_bit;

            if (attackers_to(g, to, occ_after) & occ_after & g->occ[enemy])
                break;
        }

        d++;
        gain[d] = piece_value[on_to] - gain[d - 1];

        occ ^= lva_bit;
        on_to = lva;
        side = (side == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
    }

    return see_unfold_gain(gain, d);
}

/* Score ranges (highest first):
     1 000 000  TT move
       100 000+ good captures (SEE >= 0; MVV/LVA + promo bonus tie-break)
        90 000+ quiet promotions (Q/R/B/N)
        88 000+ killer 1 / killer 2
             0..HISTORY_MAX  quiet history
      -100 000+ losing captures (SEE < 0), pushed below quiets.  */
static int
score_move(const struct search_ctx *ctx, const struct move *m,
           const struct game *g, uint16_t tt_move, int ply)
{
    const uint8_t *board = g->board;
    uint16_t packed = move_pack(m);

    if (packed == tt_move)
        return 1000000;

    if (m->flags & MOVE_CAPTURE) {
        enum piece_type victim_type = (m->flags & MOVE_ENP)
            ? PIECE_PAWN
            : piece_type(board[m->to]);
        int victim = piece_value[victim_type];
        int attacker = piece_value[piece_type(board[m->from])];
        int s = victim * 10 - attacker;

        if (m->flags & MOVE_PROMO)
            s += piece_value[m->promo] - piece_value[PIECE_PAWN];

        if (victim >= attacker)
            return 100000 + s;

        int see_score = see(g, m);
        if (see_score >= 0)
            return 100000 + s;
        return -100000 + see_score;
    }

    if (m->flags & MOVE_PROMO)
        return 90000 + piece_value[m->promo];

    if (packed == ctx->killers[ply][0]) return 89000;
    if (packed == ctx->killers[ply][1]) return 88000;

    enum color      c = piece_color(board[m->from]);
    enum piece_type t = piece_type(board[m->from]);

    return ctx->history[c][t][m->to];
}

static void
score_moves(const struct search_ctx *ctx, const struct move_list *ml,
            const struct game *g, uint16_t tt_move, int ply, int *scores)
{
    for (size_t i = 0; i < ml->count; ++i)
        scores[i] = score_move(ctx, &ml->moves[i], g, tt_move, ply);
}

/* Pulls the highest-scoring remaining entry to position `start`.  */
static void
pick_next_move(struct move_list *ml, int *scores, size_t start)
{
    size_t best = start;
    for (size_t j = start + 1; j < ml->count; ++j)
        if (scores[j] > scores[best])
            best = j;

    if (best != start) {
        int ts = scores[start];
        scores[start] = scores[best];
        scores[best] = ts;

        struct move tm = ml->moves[start];
        ml->moves[start] = ml->moves[best];
        ml->moves[best] = tm;
    }
}

/* Strips current ply from a mate score so the stored value is
   position-relative rather than search-relative.  */
static int
score_to_tt(int s, int ply)
{
    if (s >=  SEARCH_MATE - 1000) return s + ply;
    if (s <= -SEARCH_MATE + 1000) return s - ply;
    return s;
}

static int
score_from_tt(int s, int ply)
{
    if (s >=  SEARCH_MATE - 1000) return s - ply;
    if (s <= -SEARCH_MATE + 1000) return s + ply;
    return s;
}

#define QSEARCH_PLY_LIMIT     (SEARCH_MAX_DEPTH * 2)
/* Delta-pruning slack above the captured piece value (~2 pawns).  */
#define QSEARCH_DELTA_MARGIN  200

static int
qsearch(struct search_ctx *ctx, struct game *g, int alpha, int beta, int ply)
{
    ctx->nodes++;
    if (ctx->aborted) return 0;
    if ((ctx->nodes & (ABORT_POLL_NODES - 1)) == 0 && check_abort(ctx)) return 0;
    DBG_ASSERT(g->hash == zobrist_compute(g));

    if (ply >= QSEARCH_PLY_LIMIT)
        return evaluate(ctx, g);

    const int alpha_orig = alpha;
    {
        int tt_score = 0;
        if (tt_probe(g->hash, 0, alpha, beta, &tt_score, NULL))
            return score_from_tt(tt_score, ply);
    }

    const int in_check = king_in_check(g, g->turn);
    int       best;
    uint16_t  best_move = 0;

    if (in_check) {
        best = -SEARCH_INF;
    } else {
        int stand_pat = evaluate(ctx, g);

        if (stand_pat >= beta) {
            tt_store(g->hash, 0, score_to_tt(stand_pat, ply),
                     TT_BOUND_LOWER, 0);
            return stand_pat;
        }

        if (stand_pat > alpha)
            alpha = stand_pat;

        best = stand_pat;
    }

    struct move_list ml; ml.count = 0;
    append_pseudolegal_moves(g, &ml);

    /* Pass tt_move = 0; otherwise score_move would boost a quiet TT move
       to 1M and shadow real captures under the noise break below.  */
    int scores[MAX_MOVES];
    score_moves(ctx, &ml, g, 0, ply, scores);

    int legal = 0;

    for (size_t i = 0; i < ml.count; ++i) {
        pick_next_move(&ml, scores, i);

        const struct move *m = &ml.moves[i];
        const int noisy = (m->flags & (MOVE_CAPTURE | MOVE_PROMO)) != 0;

        if (!in_check) {
            /* Lazy sort places all noisy moves first; reaching a quiet
               move means no more noisy follow.  */
            if (!noisy)
                break;

            if ((m->flags & MOVE_CAPTURE) && !(m->flags & MOVE_PROMO)) {
                enum piece_type victim_t = (m->flags & MOVE_ENP)
                    ? PIECE_PAWN
                    : piece_type(g->board[m->to]);
                int gain = piece_value[victim_t];

                if (best + gain + QSEARCH_DELTA_MARGIN < alpha)
                    continue;
            }
        }

        struct undo_state undo;
        make_move(g, m, &undo);

        if (mover_in_check(g)) {
            unmake_move(g, m, &undo);
            continue;
        }

        legal++;

        int score = -qsearch(ctx, g, -beta, -alpha, ply + 1);

        unmake_move(g, m, &undo);

        if (score > best) {
            best = score;
            best_move = move_pack(m);
        }

        if (best > alpha)
            alpha = best;

        if (alpha >= beta) {
            INC(s_cutoffs);
            break;
        }
    }

    if (in_check && legal == 0) {
        int mate_score = -SEARCH_MATE + ply;
        tt_store(g->hash, 0, score_to_tt(mate_score, ply),
                 TT_BOUND_EXACT, 0);
        return mate_score;
    }

    if (!ctx->aborted) {
        enum tt_bound bound = (best <= alpha_orig) ? TT_BOUND_UPPER
                            : (best >= beta)       ? TT_BOUND_LOWER
                            :                        TT_BOUND_EXACT;
        tt_store(g->hash, 0, score_to_tt(best, ply), bound, best_move);
    }

    return best;
}

#define NULL_MOVE_R          2
#define NULL_MOVE_MIN_DEPTH  3
#define NULL_MOVE_MIN_MAT    (8 * 100)

#define LMR_MIN_LEGAL    4
#define LMR_MIN_DEPTH    3
#define LMR_DEEP_LEGAL   8
#define LMR_DEEP_DEPTH   5

#define ASPIRATION_MIN_DEPTH   5
#define ASPIRATION_INIT_DELTA  50

int
negamax(struct search_ctx *ctx, struct game *g, int depth, int ply,
        int alpha, int beta, int can_null)
{
    ctx->nodes++;
    if (ctx->aborted) return 0;
    if ((ctx->nodes & (ABORT_POLL_NODES - 1)) == 0 && check_abort(ctx)) return 0;
    DBG_ASSERT(g->hash == zobrist_compute(g));

    if (depth <= 0)
        return qsearch(ctx, g, alpha, beta, ply);

    int alpha_orig = alpha;
    int tt_score = 0;
    uint16_t tt_move = 0;

    if (tt_probe(g->hash, depth, alpha, beta, &tt_score, &tt_move))
        return score_from_tt(tt_score, ply);

    const int in_check = king_in_check(g, g->turn);

    if (can_null
        && depth >= NULL_MOVE_MIN_DEPTH
        && !in_check
        && g->material[g->turn] > NULL_MOVE_MIN_MAT
        && beta < SEARCH_MATE - 1000) {

        uint64_t saved_hash = g->hash;
        uint8_t saved_ep = g->ep_target;

        if (saved_ep != EP_NONE)
            g->hash ^= z_ep_file[file_of(saved_ep)];
        g->ep_target = EP_NONE;
        g->turn = (g->turn == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
        g->hash ^= z_side;

        int null_score = -negamax(ctx, g, depth - 1 - NULL_MOVE_R, ply + 1,
                                  -beta, -beta + 1, 0);

        g->turn = (g->turn == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
        g->ep_target = saved_ep;
        g->hash = saved_hash;

        if (null_score >= beta) {
            if (null_score >= SEARCH_MATE - 1000)
                return beta;

            return null_score;
        }
    }

    struct move_list ml; ml.count = 0;
    append_pseudolegal_moves(g, &ml);

    int scores[MAX_MOVES];
    score_moves(ctx, &ml, g, tt_move, ply, scores);

    int best = -SEARCH_INF;
    uint16_t best_move = 0;
    int legal = 0;

    for (size_t i = 0; i < ml.count; ++i) {
        pick_next_move(&ml, scores, i);
        const struct move *m = &ml.moves[i];

        struct undo_state undo;
        make_move(g, m, &undo);

        if (mover_in_check(g)) {
            unmake_move(g, m, &undo);
            continue;
        }

        legal++;

        int score;

        if (legal == 1) {
            score = -negamax(ctx, g, depth - 1, ply + 1, -beta, -alpha, 1);
        } else {
            int reduction = 0;
            if (legal >= LMR_MIN_LEGAL
                && depth >= LMR_MIN_DEPTH
                && !in_check
                && !(m->flags & (MOVE_CAPTURE | MOVE_PROMO))) {

                reduction = 1;
                if (legal >= LMR_DEEP_LEGAL && depth >= LMR_DEEP_DEPTH)
                    reduction = 2;
            }

            score = -negamax(ctx, g, depth - 1 - reduction, ply + 1,
                             -alpha - 1, -alpha, 1);

            if (score > alpha && beta - alpha > 1)
                score = -negamax(ctx, g, depth - 1, ply + 1, -beta, -alpha, 1);
        }

        unmake_move(g, m, &undo);

        if (score > best) {
            best = score;
            best_move = move_pack(m);
        }

        if (best > alpha) {
            alpha = best;

            if (alpha >= beta) {
                INC(s_cutoffs);

                /* Update killers/history only for quiet cutoffs; noisy
                   moves already sort high via MVV/LVA.  */
                if (!(m->flags & (MOVE_CAPTURE | MOVE_PROMO))) {
                    uint16_t packed = move_pack(m);

                    if (ctx->killers[ply][0] != packed) {
                        ctx->killers[ply][1] = ctx->killers[ply][0];
                        ctx->killers[ply][0] = packed;
                    }

                    enum color      c = piece_color(g->board[m->from]);
                    enum piece_type t = piece_type(g->board[m->from]);
                    int            *h = &ctx->history[c][t][m->to];

                    *h += depth * depth;
                    if (*h > HISTORY_MAX) *h = HISTORY_MAX;
                }

                break;
            }
        }
    }

    if (legal == 0)
        return in_check ? -SEARCH_MATE + ply : 0;

    if (!ctx->aborted) {
        enum tt_bound bound = (best <= alpha_orig) ? TT_BOUND_UPPER
                            : (best >= beta)       ? TT_BOUND_LOWER
                            :                        TT_BOUND_EXACT;
        tt_store(g->hash, depth, score_to_tt(best, ply), bound, best_move);
    }

    return best;
}

/* Walks the TT from `g`, recording moves until a miss, a collision, or
   an illegal stored move; restores `g` before returning.  */
static int
extract_pv(struct game *g, struct move *out, int max_len)
{
    struct undo_state undos[SEARCH_MAX_DEPTH];
    int n = 0;

    if (max_len > SEARCH_MAX_DEPTH) max_len = SEARCH_MAX_DEPTH;

    while (tt && n < max_len) {
        struct tt_entry *e = &tt[g->hash & tt_mask];
        uint64_t data = e->data;
        if ((e->key_xor ^ data) != g->hash)
            break;

        uint16_t mv = tt_unpack_move(data);
        if (mv == 0)
            break;

        struct move stored = move_unpack(mv);

        /* Recover flags by matching against the legal-move list; this
           also rejects collisions where stored.from/to is bogus.  */
        struct move_list ml; ml.count = 0;
        append_pseudolegal_moves(g, &ml);

        struct move full = { 0 };
        int found = 0;
        for (size_t i = 0; i < ml.count; ++i) {
            const struct move *m = &ml.moves[i];
            if (m->from == stored.from && m->to == stored.to
                && (!(m->flags & MOVE_PROMO) || m->promo == stored.promo)) {
                full = *m;
                found = 1;
                break;
            }
        }
        if (!found) break;

        out[n] = full;
        make_move(g, &full, &undos[n]);

        enum color moved = (g->turn == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
        if (king_in_check(g, moved)) {
            unmake_move(g, &full, &undos[n]);
            break;
        }
        ++n;
    }

    for (int i = n - 1; i >= 0; --i)
        unmake_move(g, &out[i], &undos[i]);

    return n;
}

/* Converts UCI clock controls into an absolute deadline. `movetime` wins
   over clock-based budget; clock-based budget = our_time/moves_to_go
   + 75% of increment, hard-capped at 1/4 of remaining time.  */
static uint64_t
compute_deadline(const struct search_limits *lim, enum color stm, uint64_t start_ms)
{
    if (lim->infinite)
        return UINT64_MAX;

    if (lim->movetime_ms)
        return start_ms + lim->movetime_ms;

    uint64_t our_t = (stm == COLOR_WHITE) ? lim->wtime_ms : lim->btime_ms;
    uint64_t our_i = (stm == COLOR_WHITE) ? lim->winc_ms  : lim->binc_ms;
    if (our_t == 0)
        return UINT64_MAX;

    uint64_t mtg = (lim->moves_to_go > 0) ? (uint64_t)lim->moves_to_go : 30;
    uint64_t budget = our_t / mtg + our_i * 3 / 4;
    uint64_t hard_cap = our_t / 4;
    if (budget > hard_cap) budget = hard_cap;
    if (budget < 10)       budget = 10;

    return start_ms + budget;
}

void
search_worker(struct search_ctx *ctx, struct game *g, int max_depth,
              uint64_t start_ms, int report,
              search_info_cb info, void *info_ctx,
              struct search_result *out)
{
    if (report)
        DBG_PRINTF("[search] root hash=%016llx max_depth=%d deadline_ms=%llu\n",
                   (unsigned long long)g->hash, max_depth,
                   (unsigned long long)(ctx->shared->deadline_ms == UINT64_MAX
                                        ? 0 : ctx->shared->deadline_ms - start_ms));

    int score = 0;
    struct move best_move = (struct move){ 0 };
    int have_best_move = 0;

    for (int d = 1; d <= max_depth; ++d) {
        /* Move list and scores are reused across aspiration retries.  */
        struct move_list ml; ml.count = 0;
        append_pseudolegal_moves(g, &ml);

        uint16_t tt_move = 0;
        if (tt) {
            struct tt_entry *e = &tt[g->hash & tt_mask];
            uint64_t data = e->data;
            if ((e->key_xor ^ data) == g->hash) tt_move = tt_unpack_move(data);
        }

        int scores[MAX_MOVES];
        score_moves(ctx, &ml, g, tt_move, 0, scores);

        int alpha, beta;
        int delta = ASPIRATION_INIT_DELTA;

        if (d < ASPIRATION_MIN_DEPTH || !have_best_move) {
            alpha = -SEARCH_INF;
            beta = SEARCH_INF;
        } else {
            alpha = score - delta;
            beta = score + delta;
        }

        int         best;
        struct move local_best;
        int         legal;

        for (;;) {
            best = -SEARCH_INF;
            local_best = (struct move){ 0 };
            legal = 0;

            /* Local copy of alpha; the aspiration lower bound stays in
               `alpha` for fail-low detection.  */
            int loop_alpha = alpha;

            for (size_t i = 0; i < ml.count; ++i) {
                pick_next_move(&ml, scores, i);

                struct undo_state undo;
                make_move(g, &ml.moves[i], &undo);

                if (mover_in_check(g)) {
                    unmake_move(g, &ml.moves[i], &undo);
                    continue;
                }

                legal++;

                int s = -negamax(ctx, g, d - 1, 1, -beta, -loop_alpha, 1);

                unmake_move(g, &ml.moves[i], &undo);

                if (ctx->aborted) break;

                if (s > best) {
                    best = s;
                    local_best = ml.moves[i];
                }

                if (best > loop_alpha) loop_alpha = best;
            }

            if (ctx->aborted) break;
            if (legal == 0) break;

            if (best <= alpha) {
                delta *= 2;
                alpha = best - delta;
                if (alpha < -SEARCH_INF + 1000) alpha = -SEARCH_INF;
                continue;
            }
            if (best >= beta) {
                delta *= 2;
                beta = best + delta;
                if (beta > SEARCH_INF - 1000) beta = SEARCH_INF;
                continue;
            }

            break;
        }

        if (ctx->aborted) break;
        if (legal == 0) break;

        best_move = local_best;
        score = best;
        have_best_move = 1;

        tt_store(g->hash, d, score_to_tt(score, 0), TT_BOUND_EXACT, move_pack(&local_best));

        if (info) {
            struct move pv[SEARCH_MAX_DEPTH];
            int         pv_len = extract_pv(g, pv, d);
            info(d, score, ctx->nodes, now_ms() - start_ms, pv, pv_len, info_ctx);
        }

#ifdef DEBUG
        if (report) {
            char uci[6];
            move_to_uci(&local_best, uci);
            DBG_PRINTF("[search] d =%2d score = %6d best = %s nodes = %llu tt_hits=%llu cutoffs=%llu\n",
                       d, score, uci, ctx->nodes, s_tt_hits, s_cutoffs);
        }
#endif
    }

    if (report)
        DBG_PRINTF("[search] total nodes=%llu tt_hits=%llu tt_stores=%llu cutoffs=%llu time=%llums%s\n",
                   ctx->nodes, s_tt_hits, s_tt_stores, s_cutoffs,
                   (unsigned long long)(now_ms() - start_ms),
                   ctx->aborted ? " (aborted)" : "");

    out->score = score;
    out->best_move = best_move;
    out->have_best = have_best_move;
}

int
search_run(struct game *g, const struct search_limits *lim,
           struct move *best_out, search_info_cb info, void *info_ctx)
{
    if (!tt)
        tt_init(16);

    tt_new_search();

    const uint64_t start_ms = now_ms();

    s_shared.stop = 0;
    s_shared.deadline_ms = compute_deadline(lim, g->turn, start_ms);
    s_shared.node_limit = lim->node_limit ? lim->node_limit : UINT64_MAX;
    s_tt_hits = s_tt_stores = s_cutoffs = 0;

    int max_depth = lim->max_depth > 0 ? lim->max_depth : SEARCH_MAX_DEPTH;
    if (max_depth > SEARCH_MAX_DEPTH) max_depth = SEARCH_MAX_DEPTH;

    struct search_result r;

    if (s_threads <= 1) {
        /* Single-threaded path: reuse the file-static context so killers
           are wiped per search while history/pawn cache persist across
           searches, exactly as before Lazy SMP.  */
        struct search_ctx *ctx = &s_main_ctx;
        ctx->nodes = 0;
        ctx->aborted = 0;
        ctx->shared = &s_shared;
        memset(ctx->killers, 0, sizeof(ctx->killers));

        search_worker(ctx, g, max_depth, start_ms, 1, info, info_ctx, &r);
    } else {
        thread_search(s_threads, g, max_depth, start_ms, &s_shared,
                      info, info_ctx, &r);
    }

    if (best_out)
        *best_out = r.have_best ? r.best_move : (struct move){ 0 };

    return r.score;
}

int
search_root(struct game *g, int max_depth, struct move *best_out)
{
    struct search_limits lim = { 0 };
    lim.max_depth = max_depth;
    return search_run(g, &lim, best_out, NULL, NULL);
}
