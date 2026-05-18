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

static unsigned long long s_nodes;
static unsigned long long s_tt_hits;
static unsigned long long s_tt_stores;
static unsigned long long s_cutoffs;

#ifdef DEBUG
  #define INC(x) ((x)++)
#else
  #define INC(x) ((void)0)
#endif

#ifdef DEBUG
static double
now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
#endif

// ----- Transposition table ----------------------------------------------------

static struct tt_entry *tt       = NULL;
static size_t           tt_count = 0;   // always a power of two
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

void
tt_store(uint64_t key, int depth, int score, enum tt_bound bound, uint16_t move)
{
    if (!tt) return;

    struct tt_entry *e = &tt[key & tt_mask];

    // Replace if same position, older generation, or at least as deep.
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

// 16-bit packing for storage on TT entries:
//   bits  0..6  : from (0x88 square, max 0x77 = 7 bits)
//   bits  7..13 : to
//   bits 14..15 : promo (0=none, 1=Q, 2=R, 3=N — bishop promo is rare/lossy)
static uint16_t
pack_promo(enum piece_type p)
{
    switch (p) {
        case PIECE_QUEEN:  return 1;
        case PIECE_ROOK:   return 2;
        case PIECE_KNIGHT: return 3;
        default:           return 0;
    }
}

static enum piece_type
unpack_promo(uint16_t p)
{
    switch (p) {
        case 1: return PIECE_QUEEN;
        case 2: return PIECE_ROOK;
        case 3: return PIECE_KNIGHT;
        default: return PIECE_NONE;
    }
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
    m.to    = (uint8_t)((v >> 7)  & 0x7F);
    m.promo = unpack_promo((v >> 14) & 0x3);
    m.flags = MOVE_QUIET;   // re-derived from board state at use site
    
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

static const int piece_value[7] = {
    0,      // PIECE_NONE
    100,    // PAWN
    330,    // BISHOP
    320,    // KNIGHT
    500,    // ROOK
    900,    // QUEEN
    20000   // KING (sentinel)
};

static int
evaluate(const struct game *g)
{
    int score = 0;

    for (int sq = 0; sq < 128; ++sq) {
        if (!on_board(sq)) continue;

        uint8_t p = g->board[sq];
        
        if (is_empty(p)) continue;

        int v = piece_value[piece_type(p)];
        score += (piece_color(p) == COLOR_WHITE) ? v : -v;
    }

    return (g->turn == COLOR_WHITE) ? score : -score;
}

static int
find_king(const uint8_t board[128], enum color c)
{
    uint8_t target = encode_piece(c, PIECE_KING);

    for (int sq = 0; sq < 128; ++sq)
        if (on_board(sq) && board[sq] == target)
            return sq;

    return -1;
}

static int
in_check_after_move(const struct game *g)
{
    enum color just_moved = (g->turn == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
    int king_sq = find_king(g->board, just_moved);
    if (king_sq < 0) return 1;

    struct move_list ml; ml.count = 0;
    append_pseudolegal_moves(g, &ml);

    for (size_t i = 0; i < ml.count; ++i)
        if (ml.moves[i].to == (uint8_t)king_sq)
            return 1;

    return 0;
}

// TT move first, then MVV-LVA on captures, then quiet. Selection sort over
// the move list — small N, simple, fine until we profile.
static int
score_move(const struct move *m, const uint8_t board[128], uint16_t tt_move)
{
    if (tt_move) {
        struct move tm = move_unpack(tt_move);

        if (m->from == tm.from && m->to == tm.to && m->promo == tm.promo)
            return 1000000;
    }

    if (m->flags & MOVE_CAPTURE) {
        int victim   = piece_value[piece_type(board[m->to])];
        int attacker = piece_value[piece_type(board[m->from])];
        
        return 100000 + victim * 10 - attacker;
    }

    return 0;
}

static void
order_moves(struct move_list *ml, const uint8_t board[128], uint16_t tt_move)
{
    int scores[MAX_MOVES];

    for (size_t i = 0; i < ml->count; ++i)
        scores[i] = score_move(&ml->moves[i], board, tt_move);

    for (size_t i = 0; i + 1 < ml->count; ++i) {
        size_t best = i;
        for (size_t j = i + 1; j < ml->count; ++j)
            if (scores[j] > scores[best])
                best = j;

        if (best != i) {
            int         ts = scores[i]; scores[i] = scores[best]; scores[best] = ts;
            struct move tm = ml->moves[i]; ml->moves[i] = ml->moves[best]; ml->moves[best] = tm;
        }
    }
}

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

int
negamax(struct game *g, int depth, int ply, int alpha, int beta)
{
    INC(s_nodes);
    DBG_ASSERT(g->hash == zobrist_compute(g));

    if (depth <= 0)
        return evaluate(g);

    int      alpha_orig = alpha;
    int      tt_score   = 0;
    uint16_t tt_move    = 0;

    if (tt_probe(g->hash, depth, alpha, beta, &tt_score, &tt_move))
        return score_from_tt(tt_score, ply);

    struct move_list ml; ml.count = 0;
    append_pseudolegal_moves(g, &ml);
    order_moves(&ml, g->board, tt_move);

    int      best      = -SEARCH_INF;
    uint16_t best_move = 0;
    int      legal     = 0;

    for (size_t i = 0; i < ml.count; ++i) {
        struct game child = *g;
        make_move(&child, &ml.moves[i]);

        if (in_check_after_move(&child))
            continue;

        legal++;

        int score = -negamax(&child, depth - 1, ply + 1, -beta, -alpha);

        if (score > best) {
            best      = score;
            best_move = move_pack(&ml.moves[i]);
        }
        if (best > alpha) alpha = best;
        if (alpha >= beta) { INC(s_cutoffs); break; }
    }

    if (legal == 0) {
        // Distinguish mate from stalemate by checking whether the side
        // to move is in check right now.
        enum color me      = g->turn;
        int        king_sq = find_king(g->board, me);

        struct game flipped = *g;
        flipped.turn = (me == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;

        struct move_list opp; opp.count = 0;
        append_pseudolegal_moves(&flipped, &opp);

        int checked = 0;
        for (size_t i = 0; i < opp.count; ++i)
            if (opp.moves[i].to == (uint8_t)king_sq) { checked = 1; break; }

        return checked ? -SEARCH_MATE + ply : 0;
    }

    enum tt_bound bound = (best <= alpha_orig) ? TT_BOUND_UPPER
                        : (best >= beta)       ? TT_BOUND_LOWER
                        :                        TT_BOUND_EXACT;
    tt_store(g->hash, depth, score_to_tt(best, ply), bound, best_move);

    return best;
}

int
search_root(struct game *g, int max_depth, struct move *best_out)
{
    if (!tt)
        tt_init(16);   // 16 MiB default

    tt_new_search();

    s_nodes = s_tt_hits = s_tt_stores = s_cutoffs = 0;
#ifdef DEBUG
    double t_start = now_seconds();
#endif

    int         score          = 0;
    struct move best_move      = (struct move){ 0 };
    int         have_best_move = 0;

    if (max_depth > SEARCH_MAX_DEPTH) max_depth = SEARCH_MAX_DEPTH;

    DBG_PRINTF("[search] root hash=%016llx max_depth=%d ai_default=%d\n",
               (unsigned long long)g->hash, max_depth, AI_DEFAULT_DEPTH);

    for (int d = 1; d <= max_depth; ++d) {
#ifdef DEBUG
        unsigned long long iter_nodes_before = s_nodes;
        double             iter_t_before     = now_seconds();
#endif
        int alpha = -SEARCH_INF;
        int beta  =  SEARCH_INF;

        struct move_list ml; ml.count = 0;
        append_pseudolegal_moves(g, &ml);

        uint16_t tt_move = 0;
        if (tt) {
            struct tt_entry *e = &tt[g->hash & tt_mask];
            if (e->key == g->hash) tt_move = e->move;
        }

        order_moves(&ml, g->board, tt_move);

        int         best        = -SEARCH_INF;
        struct move local_best  = (struct move){ 0 };
        int         legal       = 0;

        for (size_t i = 0; i < ml.count; ++i) {
            struct game child = *g;
            make_move(&child, &ml.moves[i]);

            if (in_check_after_move(&child))
                continue;

            legal++;

            int s = -negamax(&child, d - 1, 1, -beta, -alpha);

            if (s > best) {
                best       = s;
                local_best = ml.moves[i];
            }
            if (best > alpha) alpha = best;
        }

        if (legal == 0)
            break;

        best_move      = local_best;
        score          = best;
        have_best_move = 1;

        tt_store(g->hash, d, score_to_tt(score, 0), TT_BOUND_EXACT, move_pack(&local_best));

#ifdef DEBUG
        {
            char uci[6];
            move_to_uci(&local_best, uci);
            double             dt          = now_seconds() - iter_t_before;
            unsigned long long iter_nodes  = s_nodes - iter_nodes_before;
            double             knps        = dt > 0 ? (iter_nodes / 1000.0) / dt : 0.0;
            DBG_PRINTF("[search] d=%2d score=%6d best=%s nodes=%llu (%.1f kn/s) tt_hits=%llu cutoffs=%llu\n",
                       d, score, uci, iter_nodes, knps, s_tt_hits, s_cutoffs);
        }
#endif
    }

#ifdef DEBUG
    DBG_PRINTF("[search] total nodes=%llu tt_hits=%llu tt_stores=%llu cutoffs=%llu time=%.3fs\n",
               s_nodes, s_tt_hits, s_tt_stores, s_cutoffs, now_seconds() - t_start);
#endif

    if (best_out)
        *best_out = have_best_move ? best_move : (struct move){ 0 };

    return score;
}
