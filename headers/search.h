#ifndef CITT_HEADERS_SEARCH_H_
#define CITT_HEADERS_SEARCH_H_

#include <stdint.h>
#include <stddef.h>

#include "board.h"
#include "movegen.h"

struct game;

#define SEARCH_INF       30000
#define SEARCH_MATE      29000
#define SEARCH_MAX_DEPTH 64

#ifdef DEBUG
  #define AI_DEFAULT_DEPTH 3
#else
  #define AI_DEFAULT_DEPTH 5
#endif

extern const int piece_value[7];

/* Tapered piece-square tables. Eval interpolates between mg and eg by
   `phase` (remaining non-pawn material weight, 0..PHASE_MAX). Tables are
   laid out with row 0 = rank 8; pst_lookup mirrors for black.  */
#define PHASE_MAX 24
extern const int     phase_weight[7];
extern const int16_t pst_mg[7][64];
extern const int16_t pst_eg[7][64];

struct pst_pair { int16_t mg; int16_t eg; };

static inline struct pst_pair
pst_lookup(enum color c, enum piece_type t, int sq)
{
    int rank = sq >> 4;
    int file = sq & 7;
    int row  = (c == COLOR_WHITE) ? (7 - rank) : rank;
    int idx  = (row << 3) | file;

    struct pst_pair p = { pst_mg[t][idx], pst_eg[t][idx] };
    return p;
}

enum tt_bound {
    TT_BOUND_NONE = 0,
    TT_BOUND_EXACT,
    TT_BOUND_LOWER,
    TT_BOUND_UPPER,
};

/* 16-byte entry. `key` is the full Zobrist; the index in the TT array is
   key & mask, so we compare against `key` to reject index collisions.
   `move` is packed by move_pack; 0 means no stored move.  */
struct tt_entry {
    uint64_t key;
    int16_t  score;
    uint16_t move;
    uint8_t  depth;
    uint8_t  bound;
    uint8_t  age;
    uint8_t  _pad;
};

void tt_init       (size_t mb);
void tt_free       (void);
void tt_clear      (void);
void tt_new_search (void);

int  tt_probe (uint64_t key, int depth, int alpha, int beta,
               int *score_out, uint16_t *move_out);
void tt_store (uint64_t key, int depth, int score,
               enum tt_bound bound, uint16_t move);

/* Move-in-TT packing: bits 0..6 from, 7..13 to, 14..15 promo. Flags are
   not stored; unpack returns MOVE_QUIET.  */
uint16_t    move_pack   (const struct move *m);
struct move move_unpack (uint16_t packed);

int search_root(struct game *g, int max_depth, struct move *best);

/* Rich search entry point used by UCI and any caller that needs time
   controls or per-iteration reporting. Set unused fields to 0:
     max_depth   = 0 means "go to SEARCH_MAX_DEPTH unless time runs out"
     movetime_ms = 0 disables fixed per-move time
     node_limit  = 0 disables node limit
     w/btime_ms  = side clocks (used together with w/binc and moves_to_go)
     infinite    = 1 means "search until search_signal_stop() is called"  */
struct search_limits {
    int      max_depth;
    uint64_t movetime_ms;
    uint64_t node_limit;
    uint64_t wtime_ms, btime_ms;
    uint64_t winc_ms,  binc_ms;
    int      moves_to_go;
    int      infinite;
};

/* Fired after each completed iterative-deepening iteration. `time_ms` is
   elapsed since search_run was called. `pv`/`pv_len` describe the PV
   extracted from the TT; pv_len may be 0 if extraction failed.  */
typedef void (*search_info_cb)(int depth, int score,
                               unsigned long long nodes,
                               unsigned long long time_ms,
                               const struct move *pv, int pv_len,
                               void *ctx);

int  search_run         (struct game *g, const struct search_limits *lim,
                         struct move *best_out,
                         search_info_cb info, void *info_ctx);

/* Called by UCI from the same thread between commands, OR (if you wire it
   up) from a signal handler.  Safe to call at any time; takes effect at
   the next poll inside negamax/qsearch.  */
void search_signal_stop (void);

/* Always tracked, regardless of build mode, so bench can read it.  */
unsigned long long search_get_nodes(void);

/* Wipe per-search ordering tables (killers, history, pawn-eval cache).
   Use between independent benchmark positions. Does NOT touch the TT.  */
void search_reset_state(void);

/* `can_null` gates null-move pruning: 1 from external callers, 0 only
   inside negamax's own null-move recursion.  */
int negamax(struct game *g, int depth, int ply, int alpha, int beta, int can_null);

/* The following structs are search-internal but live here so all engine
   types are header-resident.  */

struct pawn_eval { int mg; int eg; };

/* Per-file pawn data shared across eval terms. count[c][f] = pawns of
   color c on file f. max_rank/min_rank are -1/8 if no pawn on that file.  */
struct pawn_info {
    int8_t count[2][8];
    int8_t max_rank[2][8];
    int8_t min_rank[2][8];
};

struct pawn_hash_entry {
    uint64_t         key;
    struct pawn_info pi;
    int16_t          mg;
    int16_t          eg;
};

/* UCI-style move string ("e2e4", "a7a8q"). buf must hold at least 6.  */
void move_to_uci(const struct move *m, char buf[6]);

#endif
