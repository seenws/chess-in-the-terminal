/* search.c -- alpha-beta search, quiescence, transposition table, eval.  */

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "board.h"
#include "debug.h"
#include "game.h"
#include "movegen.h"
#include "search.h"
#include "zobrist.h"

/* ---- search state and counters ------------------------------------- */

static unsigned long long s_nodes;
static unsigned long long s_tt_hits;
static unsigned long long s_tt_stores;
static unsigned long long s_cutoffs;

/* Abort plumbing. `s_stop_external` is set from outside (UCI 'stop' or a
   signal handler); the search polls it every ABORT_POLL_NODES nodes,
   together with the deadline and node-limit checks. Once `s_aborted` is
   set, every recursive frame short-circuits — the returned score is
   garbage and must be discarded by the caller.  */
#define ABORT_POLL_NODES 4096
static volatile sig_atomic_t s_stop_external;
static uint64_t              s_deadline_ms;
static uint64_t              s_node_limit;
static int                   s_aborted;

static uint64_t
now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static int
check_abort(void)
{
    if (s_aborted)                          return 1;
    if (s_stop_external)                    { s_aborted = 1; return 1; }
    if (s_nodes >= s_node_limit)            { s_aborted = 1; return 1; }
    if (now_ms() >= s_deadline_ms)          { s_aborted = 1; return 1; }
    return 0;
}

void
search_signal_stop(void)
{
    s_stop_external = 1;
}

/* History accumulates across searches; killers are wiped per search.  */
#define HISTORY_MAX 16384
static uint16_t s_killers[SEARCH_MAX_DEPTH * 2][2];
static int      s_history[2][7][128];

#define PAWN_HASH_SIZE 4096
static struct pawn_hash_entry s_pawn_hash[PAWN_HASH_SIZE];

#ifdef DEBUG
  #define INC(x) ((x)++)
#else
  #define INC(x) ((void)0)
#endif

/* ---- transposition table ------------------------------------------- */

static struct tt_entry *tt       = NULL;
static size_t           tt_count = 0;   /* always a power of two */
static size_t           tt_mask  = 0;
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
    size_t n     = pow2_floor(bytes / sizeof(struct tt_entry));

    if (n < 1024) n = 1024;

    tt = calloc(n, sizeof(struct tt_entry));

    if (!tt) {
        tt_count = 0;
        tt_mask  = 0;
        return;
    }

    tt_count = n;
    tt_mask  = n - 1;
    tt_age   = 0;
}

void
tt_free(void)
{
    free(tt);
    tt       = NULL;
    tt_count = 0;
    tt_mask  = 0;
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
    return s_nodes;
}

void
search_reset_state(void)
{
    memset(s_killers,   0, sizeof(s_killers));
    memset(s_history,   0, sizeof(s_history));
    memset(s_pawn_hash, 0, sizeof(s_pawn_hash));
}

int
tt_probe(uint64_t key, int depth, int alpha, int beta, int *score_out, uint16_t *move_out)
{
    if (!tt) return 0;

    struct tt_entry *e = &tt[key & tt_mask];

    if (e->key != key) return 0;

    if (move_out) *move_out = e->move;

    if ((int)e->depth < depth) return 0;

    int s = e->score;

    if (e->bound == TT_BOUND_EXACT) {
        INC(s_tt_hits);
        *score_out = s;
        return 1;
    }
    if (e->bound == TT_BOUND_LOWER && s >= beta) {
        INC(s_tt_hits);
        *score_out = s;
        return 1;
    }
    if (e->bound == TT_BOUND_UPPER && s <= alpha) {
        INC(s_tt_hits);
        *score_out = s;
        return 1;
    }

    return 0;
}

/* Replacement: same position, older generation, or at least as deep.  */
void
tt_store(uint64_t key, int depth, int score, enum tt_bound bound, uint16_t move)
{
    if (!tt) return;

    struct tt_entry *e = &tt[key & tt_mask];

    if (e->key == key || e->age != tt_age || depth >= (int)e->depth) {
        e->key   = key;
        e->score = (int16_t)score;
        e->move  = move;
        e->depth = (uint8_t)depth;
        e->bound = (uint8_t)bound;
        e->age   = tt_age;
        INC(s_tt_stores);
    }
}

/* ---- move packing for TT entries ----------------------------------- */

/* Promo packing fits in 2 bits via parallel tables. Index 0 means
   "no promo" both ways; bishops/non-promos collapse to 0.  */
static const uint8_t         promo_pack_table[7]   = {
    [PIECE_QUEEN] = 1, [PIECE_ROOK] = 2, [PIECE_KNIGHT] = 3
};
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
    static const char promo_chars[] = " pbnrqk";

    buf[0] = (char)('a' + square_file(m->from));
    buf[1] = (char)('1' + square_rank(m->from));
    buf[2] = (char)('a' + square_file(m->to));
    buf[3] = (char)('1' + square_rank(m->to));

    if (m->promo != PIECE_NONE) {
        buf[4] = promo_chars[m->promo];
        buf[5] = '\0';
    } else {
        buf[4] = '\0';
    }
}

/* ---- piece values, phase, piece-square tables ---------------------- */

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

/* Michniewski "Simplified Evaluation" PSTs. Rank 8 is row 0; pst_lookup()
   mirrors for black. PIECE_NONE row is zero so a stray lookup against an
   empty square contributes nothing. Only PAWN and KING differ between mg
   and eg; minor/major piece tables are shared via the macros below.  */

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
    /* MG king: corner-favoring (sheltered behind the pawn line).  */
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
    /* EG king: center-favoring; king should be active in the endgame.  */
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

/* ---- evaluation term constants ------------------------------------- */

#define BISHOP_PAIR_BONUS          30
#define DOUBLED_PAWN_PENALTY       15
#define ISOLATED_PAWN_PENALTY      15
#define ROOK_OPEN_FILE_BONUS       20
#define ROOK_SEMI_OPEN_FILE_BONUS  10
#define KING_SHIELD_RANK1          15
#define KING_SHIELD_RANK2          10

/* Passed-pawn bonus by ranks-advanced-from-start (0..5; 5 = one move from
   promotion). EG ~2x MG: with little material left, a passer is decisive.  */
static const int passed_pawn_mg[6] = { 0,  5, 15, 25, 40,  70 };
static const int passed_pawn_eg[6] = { 0, 10, 20, 40, 70, 120 };

/* ---- evaluation -------------------------------------------------- */

/* Pawns can only live on ranks 1..6 (ranks 0/7 would have promoted),
   so iterate just that band.  */
static void
collect_pawn_info(const struct game *g, struct pawn_info *pi)
{
    for (int c = 0; c < 2; ++c)
        for (int f = 0; f < 8; ++f) {
            pi->count[c][f]    = 0;
            pi->max_rank[c][f] = -1;
            pi->min_rank[c][f] =  8;
        }

    for (int rank = 1; rank < 7; ++rank) {
        for (int file = 0; file < 8; ++file) {
            uint8_t p = g->board[(rank << 4) | file];
            if (is_empty(p))                 continue;
            if (piece_type(p) != PIECE_PAWN) continue;

            enum color c = piece_color(p);
            pi->count[c][file]++;
            if (rank > pi->max_rank[c][file]) pi->max_rank[c][file] = (int8_t)rank;
            if (rank < pi->min_rank[c][file]) pi->min_rank[c][file] = (int8_t)rank;
        }
    }
}

static struct pawn_eval
evaluate_pawn_structure(const struct game *g, const struct pawn_info *pi)
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

    /* Passed: per-pawn pass using the rank tables. White at (F, R) is
       passed iff no black pawn on files F-1/F/F+1 lies at rank > R.  */
    for (int rank = 1; rank < 7; ++rank) {
        for (int file = 0; file < 8; ++file) {
            uint8_t p = g->board[(rank << 4) | file];
            if (is_empty(p))                 continue;
            if (piece_type(p) != PIECE_PAWN) continue;

            enum color c = piece_color(p);

            if (c == COLOR_WHITE) {
                int blk_F  =              pi->max_rank[COLOR_BLACK][file];
                int blk_Fm = (file > 0) ? pi->max_rank[COLOR_BLACK][file - 1] : -1;
                int blk_Fp = (file < 7) ? pi->max_rank[COLOR_BLACK][file + 1] : -1;

                if (blk_F <= rank && blk_Fm <= rank && blk_Fp <= rank) {
                    int adv = rank - 1;
                    mg += passed_pawn_mg[adv];
                    eg += passed_pawn_eg[adv];
                }
            } else {
                int blk_F  =              pi->min_rank[COLOR_WHITE][file];
                int blk_Fm = (file > 0) ? pi->min_rank[COLOR_WHITE][file - 1] : 8;
                int blk_Fp = (file < 7) ? pi->min_rank[COLOR_WHITE][file + 1] : 8;

                if (blk_F >= rank && blk_Fm >= rank && blk_Fp >= rank) {
                    int adv = 6 - rank;
                    mg -= passed_pawn_mg[adv];
                    eg -= passed_pawn_eg[adv];
                }
            }
        }
    }

    struct pawn_eval pe = { mg, eg };
    return pe;
}

static struct pawn_eval
evaluate_rook_files(const struct game *g, const struct pawn_info *pi)
{
    int mg = 0;
    int eg = 0;

    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            uint8_t p = g->board[(rank << 4) | file];
            if (is_empty(p))                 continue;
            if (piece_type(p) != PIECE_ROOK) continue;

            enum color c    = piece_color(p);
            int        mine = pi->count[c][file];
            int        oppo = pi->count[(c == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE][file];

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

/* MG-only: an active endgame king ignores its pawn shield.  */
static struct pawn_eval
evaluate_king_shield(const struct game *g, const struct pawn_info *pi)
{
    int mg = 0;

    for (int c = 0; c < 2; ++c) {
        int king_sq = g->king_sq[c];
        int k_file  = square_file(king_sq);
        int k_rank  = square_rank(king_sq);

        int dir   = (c == COLOR_WHITE) ?  1 : -1;
        int rank1 = k_rank + dir;
        int rank2 = k_rank + 2 * dir;

        int side_mg = 0;

        for (int df = -1; df <= 1; ++df) {
            int f = k_file + df;
            if (f < 0 || f > 7) continue;

            if (rank1 >= 1 && rank1 <= 6) {
                uint8_t p = g->board[(rank1 << 4) | f];
                if (!is_empty(p)
                    && piece_type(p) == PIECE_PAWN
                    && piece_color(p) == (enum color)c)
                    side_mg += KING_SHIELD_RANK1;
            }
            if (rank2 >= 1 && rank2 <= 6) {
                uint8_t p = g->board[(rank2 << 4) | f];
                if (!is_empty(p)
                    && piece_type(p) == PIECE_PAWN
                    && piece_color(p) == (enum color)c)
                    side_mg += KING_SHIELD_RANK2;
            }
        }

        if (c == COLOR_WHITE) mg += side_mg;
        else                  mg -= side_mg;
    }

    (void)pi;

    struct pawn_eval pe = { mg, 0 };
    return pe;
}

/* Cached by g->pawn_hash; structure changes only on pawn moves/captures
   so most consecutive evaluate() calls hit.  */
static struct pawn_eval
pawn_eval_cached(const struct game *g, struct pawn_info *pi_out)
{
    struct pawn_hash_entry *e = &s_pawn_hash[g->pawn_hash & (PAWN_HASH_SIZE - 1)];

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
evaluate(const struct game *g)
{
    int mat = (int)g->material[COLOR_WHITE] - (int)g->material[COLOR_BLACK];
    int mg  = (int)g->psqt_mg[COLOR_WHITE]  - (int)g->psqt_mg[COLOR_BLACK];
    int eg  = (int)g->psqt_eg[COLOR_WHITE]  - (int)g->psqt_eg[COLOR_BLACK];

    struct pawn_info pi;
    struct pawn_eval ps = pawn_eval_cached(g, &pi);
    struct pawn_eval rf = evaluate_rook_files(g, &pi);
    struct pawn_eval ks = evaluate_king_shield(g, &pi);

    mg += ps.mg + rf.mg + ks.mg;
    eg += ps.eg + rf.eg + ks.eg;

    /* Clamp phase: mass-promotion positions can push it past PHASE_MAX.  */
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

/* ---- move ordering ------------------------------------------------- */

/* Score ranges:
     1000000  TT move
      100000+ captures (MVV/LVA + capture-promotion bonus)
       90000+ quiet promotions (Q/R/B/N)
       88000+ killer 1 / killer 2
       history (0..HISTORY_MAX) for the rest                                */
static int
score_move(const struct move *m, const uint8_t board[128], uint16_t tt_move, int ply)
{
    uint16_t packed = move_pack(m);

    if (packed == tt_move)
        return 1000000;

    if (m->flags & MOVE_CAPTURE) {
        enum piece_type victim_type = (m->flags & MOVE_ENP)
            ? PIECE_PAWN
            : piece_type(board[m->to]);
        int victim   = piece_value[victim_type];
        int attacker = piece_value[piece_type(board[m->from])];
        int s        = 100000 + victim * 10 - attacker;

        if (m->flags & MOVE_PROMO)
            s += piece_value[m->promo] - piece_value[PIECE_PAWN];

        return s;
    }

    if (m->flags & MOVE_PROMO)
        return 90000 + piece_value[m->promo];

    if (packed == s_killers[ply][0]) return 89000;
    if (packed == s_killers[ply][1]) return 88000;

    enum color      c = piece_color(board[m->from]);
    enum piece_type t = piece_type(board[m->from]);

    return s_history[c][t][m->to];
}

static void
score_moves(const struct move_list *ml, const uint8_t board[128],
            uint16_t tt_move, int ply, int *scores)
{
    for (size_t i = 0; i < ml->count; ++i)
        scores[i] = score_move(&ml->moves[i], board, tt_move, ply);
}

/* Lazy selection: pulls the best remaining move/score pair to `start`.
   Each iteration of the move loop calls this once, and cutting off the
   loop early saves the rest of the comparisons.  */
static void
pick_next_move(struct move_list *ml, int *scores, size_t start)
{
    size_t best = start;
    for (size_t j = start + 1; j < ml->count; ++j)
        if (scores[j] > scores[best])
            best = j;

    if (best != start) {
        int ts          = scores[start];
        scores[start]   = scores[best];
        scores[best]    = ts;

        struct move tm   = ml->moves[start];
        ml->moves[start] = ml->moves[best];
        ml->moves[best]  = tm;
    }
}

/* TT-stored mate scores must be ply-independent; convert at the boundary.  */
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

/* ---- quiescence search --------------------------------------------- */

/* Captures normally exhaust material and terminate, but perpetual-check
   evasion lines can recurse without bound. Hard cap at 2*MAX_DEPTH.  */
#define QSEARCH_PLY_LIMIT (SEARCH_MAX_DEPTH * 2)

/* Delta-pruning slack above the captured piece's value (~2 pawns).  */
#define QSEARCH_DELTA_MARGIN 200

static int
qsearch(struct game *g, int alpha, int beta, int ply)
{
    s_nodes++;
    if (s_aborted) return 0;
    if ((s_nodes & (ABORT_POLL_NODES - 1)) == 0 && check_abort()) return 0;
    DBG_ASSERT(g->hash == zobrist_compute(g));

    if (ply >= QSEARCH_PLY_LIMIT)
        return evaluate(g);

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
        /* Forced to move: no stand-pat. Mate detected below at legal==0.  */
        best = -SEARCH_INF;
    } else {
        int stand_pat = evaluate(g);

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

    /* tt_move is *not* passed: score_move would boost it to 1M and a
       quiet TT move would shadow real captures under the noise break.  */
    int scores[MAX_MOVES];
    score_moves(&ml, g->board, 0, ply, scores);

    int legal = 0;

    for (size_t i = 0; i < ml.count; ++i) {
        pick_next_move(&ml, scores, i);

        const struct move *m     = &ml.moves[i];
        const int          noisy = (m->flags & (MOVE_CAPTURE | MOVE_PROMO)) != 0;

        if (!in_check) {
            /* score_move yields >0 iff noisy; lazy sort puts noisy first,
               so the first quiet means no more noisy follow.  */
            if (!noisy)
                break;

            /* Delta pruning: skip clean captures that can't lift best to
               alpha. Promotions bypass (huge value swing); check evasions
               bypass (forced).  */
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

        int score = -qsearch(g, -beta, -alpha, ply + 1);

        unmake_move(g, m, &undo);

        if (score > best) {
            best      = score;
            best_move = move_pack(m);
        }

        if (best > alpha)
            alpha = best;

        if (alpha >= beta) {
            INC(s_cutoffs);
            break;
        }
    }

    /* legal==0 out of check just means we skipped all the quiets; only
       in-check + no replies is mate.  */
    if (in_check && legal == 0) {
        int mate_score = -SEARCH_MATE + ply;
        tt_store(g->hash, 0, score_to_tt(mate_score, ply),
                 TT_BOUND_EXACT, 0);
        return mate_score;
    }

    if (!s_aborted) {
        enum tt_bound bound = (best <= alpha_orig) ? TT_BOUND_UPPER
                            : (best >= beta)       ? TT_BOUND_LOWER
                            :                        TT_BOUND_EXACT;
        tt_store(g->hash, 0, score_to_tt(best, ply), bound, best_move);
    }

    return best;
}

/* ---- negamax + null-move + LMR ------------------------------------- */

/* Null-move pruning: R=2 reduction, skip near mate scores, require some
   non-pawn material to avoid worst-case zugzwang.  */
#define NULL_MOVE_R          2
#define NULL_MOVE_MIN_DEPTH  3
#define NULL_MOVE_MIN_MAT    (8 * 100)

/* LMR: first few legal moves at full depth; later quiets get R=1 (or R=2
   when both legal and depth are sufficient).  */
#define LMR_MIN_LEGAL    4
#define LMR_MIN_DEPTH    3
#define LMR_DEEP_LEGAL   8
#define LMR_DEEP_DEPTH   5

/* Aspiration window narrows the root search around the previous score;
   widens on fail.  */
#define ASPIRATION_MIN_DEPTH   5
#define ASPIRATION_INIT_DELTA  50

int
negamax(struct game *g, int depth, int ply, int alpha, int beta, int can_null)
{
    s_nodes++;
    if (s_aborted) return 0;
    if ((s_nodes & (ABORT_POLL_NODES - 1)) == 0 && check_abort()) return 0;
    DBG_ASSERT(g->hash == zobrist_compute(g));

    if (depth <= 0)
        return qsearch(g, alpha, beta, ply);

    int      alpha_orig = alpha;
    int      tt_score   = 0;
    uint16_t tt_move    = 0;

    if (tt_probe(g->hash, depth, alpha, beta, &tt_score, &tt_move))
        return score_from_tt(tt_score, ply);

    const int in_check = king_in_check(g, g->turn);

    if (can_null
        && depth >= NULL_MOVE_MIN_DEPTH
        && !in_check
        && g->material[g->turn] > NULL_MOVE_MIN_MAT
        && beta < SEARCH_MATE - 1000) {

        uint64_t saved_hash = g->hash;
        uint8_t  saved_ep   = g->ep_target;

        if (saved_ep != EP_NONE)
            g->hash ^= z_ep_file[square_file(saved_ep)];
        g->ep_target = EP_NONE;
        g->turn      = (g->turn == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
        g->hash     ^= z_side;

        int null_score = -negamax(g, depth - 1 - NULL_MOVE_R, ply + 1,
                                  -beta, -beta + 1, 0);

        g->turn      = (g->turn == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
        g->ep_target = saved_ep;
        g->hash      = saved_hash;

        if (null_score >= beta) {
            /* Clamp: reduced-depth searches can't be trusted to report
               correct mate distance.  */
            if (null_score >= SEARCH_MATE - 1000)
                return beta;
            return null_score;
        }
    }

    struct move_list ml; ml.count = 0;
    append_pseudolegal_moves(g, &ml);

    int scores[MAX_MOVES];
    score_moves(&ml, g->board, tt_move, ply, scores);

    int      best      = -SEARCH_INF;
    uint16_t best_move = 0;
    int      legal     = 0;

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
            score = -negamax(g, depth - 1, ply + 1, -beta, -alpha, 1);
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

            /* PVS scout: null window + (optionally) reduced depth.  */
            score = -negamax(g, depth - 1 - reduction, ply + 1,
                             -alpha - 1, -alpha, 1);

            /* Re-search if the scout suggests improvement; skip if the
               parent window is already null (re-search would be identical).  */
            if (score > alpha && beta - alpha > 1)
                score = -negamax(g, depth - 1, ply + 1, -beta, -alpha, 1);
        }

        unmake_move(g, m, &undo);

        if (score > best) {
            best      = score;
            best_move = move_pack(m);
        }

        if (best > alpha) {
            alpha = best;

            if (alpha >= beta) {
                INC(s_cutoffs);

                /* Killer + history only for quiet cutoffs; captures already
                   sort high via MVV/LVA.  */
                if (!(m->flags & (MOVE_CAPTURE | MOVE_PROMO))) {
                    uint16_t packed = move_pack(m);

                    if (s_killers[ply][0] != packed) {
                        s_killers[ply][1] = s_killers[ply][0];
                        s_killers[ply][0] = packed;
                    }

                    enum color      c = piece_color(g->board[m->from]);
                    enum piece_type t = piece_type(g->board[m->from]);
                    int            *h = &s_history[c][t][m->to];

                    *h += depth * depth;
                    if (*h > HISTORY_MAX) *h = HISTORY_MAX;
                }

                break;
            }
        }
    }

    if (legal == 0)
        return in_check ? -SEARCH_MATE + ply : 0;

    if (!s_aborted) {
        enum tt_bound bound = (best <= alpha_orig) ? TT_BOUND_UPPER
                            : (best >= beta)       ? TT_BOUND_LOWER
                            :                        TT_BOUND_EXACT;
        tt_store(g->hash, depth, score_to_tt(best, ply), bound, best_move);
    }

    return best;
}

/* Walk the TT from the current position, recording moves until we hit a
   miss, a collision, or an illegal stored move. Restores `g` on the way
   out via paired unmake.  */
static int
extract_pv(struct game *g, struct move *out, int max_len)
{
    struct undo_state undos[SEARCH_MAX_DEPTH];
    int n = 0;

    if (max_len > SEARCH_MAX_DEPTH) max_len = SEARCH_MAX_DEPTH;

    while (tt && n < max_len) {
        struct tt_entry *e = &tt[g->hash & tt_mask];
        if (e->key != g->hash || e->move == 0)
            break;

        struct move stored = move_unpack(e->move);

        /* Recover full flags by matching against the legal move list, and
           guard against TT-key collisions where stored.from/to is bogus.  */
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

/* Convert UCI-style clock controls into an absolute deadline. movetime
   beats clock-based budget; clock-based uses moves_to_go (defaults to 30)
   and adds 75% of increment, hard-capped at 1/4 of remaining time so we
   don't blow the clock on one move.  */
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

    uint64_t mtg      = (lim->moves_to_go > 0) ? (uint64_t)lim->moves_to_go : 30;
    uint64_t budget   = our_t / mtg + our_i * 3 / 4;
    uint64_t hard_cap = our_t / 4;
    if (budget > hard_cap) budget = hard_cap;
    if (budget < 10)       budget = 10;

    return start_ms + budget;
}

int
search_run(struct game *g, const struct search_limits *lim,
           struct move *best_out, search_info_cb info, void *info_ctx)
{
    if (!tt)
        tt_init(16);

    tt_new_search();

    s_nodes = s_tt_hits = s_tt_stores = s_cutoffs = 0;
    memset(s_killers, 0, sizeof(s_killers));

    const uint64_t start_ms = now_ms();

    s_stop_external = 0;
    s_aborted       = 0;
    s_deadline_ms   = compute_deadline(lim, g->turn, start_ms);
    s_node_limit    = lim->node_limit ? lim->node_limit : UINT64_MAX;

    int max_depth = lim->max_depth > 0 ? lim->max_depth : SEARCH_MAX_DEPTH;
    if (max_depth > SEARCH_MAX_DEPTH) max_depth = SEARCH_MAX_DEPTH;

    DBG_PRINTF("[search] root hash=%016llx max_depth=%d deadline_ms=%llu\n",
               (unsigned long long)g->hash, max_depth,
               (unsigned long long)(s_deadline_ms == UINT64_MAX
                                    ? 0 : s_deadline_ms - start_ms));

    int         score          = 0;
    struct move best_move      = (struct move){ 0 };
    int         have_best_move = 0;

    for (int d = 1; d <= max_depth; ++d) {
        /* Move list + scores computed once per depth iteration; aspiration
           retries reuse them.  */
        struct move_list ml; ml.count = 0;
        append_pseudolegal_moves(g, &ml);

        uint16_t tt_move = 0;
        if (tt) {
            struct tt_entry *e = &tt[g->hash & tt_mask];
            if (e->key == g->hash) tt_move = e->move;
        }

        int scores[MAX_MOVES];
        score_moves(&ml, g->board, tt_move, 0, scores);

        int alpha, beta;
        int delta = ASPIRATION_INIT_DELTA;

        if (d < ASPIRATION_MIN_DEPTH || !have_best_move) {
            alpha = -SEARCH_INF;
            beta  =  SEARCH_INF;
        } else {
            alpha = score - delta;
            beta  = score + delta;
        }

        int         best;
        struct move local_best;
        int         legal;

        for (;;) {
            best       = -SEARCH_INF;
            local_best = (struct move){ 0 };
            legal      = 0;

            /* Loop locally raises alpha; window's lower bound stays in
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

                int s = -negamax(g, d - 1, 1, -beta, -loop_alpha, 1);

                unmake_move(g, &ml.moves[i], &undo);

                if (s_aborted) break;

                if (s > best) {
                    best       = s;
                    local_best = ml.moves[i];
                }

                if (best > loop_alpha) loop_alpha = best;
            }

            if (s_aborted) break;
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

        if (s_aborted) break;
        if (legal == 0) break;

        best_move      = local_best;
        score          = best;
        have_best_move = 1;

        tt_store(g->hash, d, score_to_tt(score, 0), TT_BOUND_EXACT, move_pack(&local_best));

        if (info) {
            struct move pv[SEARCH_MAX_DEPTH];
            int         pv_len = extract_pv(g, pv, d);
            info(d, score, s_nodes, now_ms() - start_ms, pv, pv_len, info_ctx);
        }

#ifdef DEBUG
        {
            char uci[6];
            move_to_uci(&local_best, uci);
            DBG_PRINTF("[search] d =%2d score = %6d best = %s nodes = %llu tt_hits=%llu cutoffs=%llu\n",
                       d, score, uci, s_nodes, s_tt_hits, s_cutoffs);
        }
#endif
    }

    DBG_PRINTF("[search] total nodes=%llu tt_hits=%llu tt_stores=%llu cutoffs=%llu time=%llums%s\n",
               s_nodes, s_tt_hits, s_tt_stores, s_cutoffs,
               (unsigned long long)(now_ms() - start_ms),
               s_aborted ? " (aborted)" : "");

    if (best_out)
        *best_out = have_best_move ? best_move : (struct move){ 0 };

    return score;
}

int
search_root(struct game *g, int max_depth, struct move *best_out)
{
    struct search_limits lim = { 0 };
    lim.max_depth = max_depth;
    return search_run(g, &lim, best_out, NULL, NULL);
}
