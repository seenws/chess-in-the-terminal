#ifndef CITT_HEADERS_SEARCH_INTERNAL_H_
#define CITT_HEADERS_SEARCH_INTERNAL_H_

#include <signal.h>
#include <stdint.h>

#include "search.h"

struct game;

/* Pawn-structure eval cache size in entries; indexed by the pawn-only
   Zobrist key masked to this power of two.  */
#define PAWN_HASH_SIZE 4096

/* Search-wide control shared by every worker in a Lazy SMP search. Set up
   once before the workers launch and then read-mostly; `stop` is the sole
   field written during the search (asynchronously, by search_signal_stop).  */
struct search_shared {
    volatile sig_atomic_t stop;        /* raise to abort; polled by all workers */
    uint64_t              deadline_ms; /* absolute wall deadline; UINT64_MAX = none */
    uint64_t              node_limit;  /* node ceiling; UINT64_MAX = none */
};

/* Per-search mutable state. One instance backs a single search; Lazy SMP
   gives each worker thread its own, so the transposition table is the only
   shared mutable structure. Holds the node counter, the per-thread abort
   latch, the move-ordering tables (killers, history), the pawn-structure
   eval cache, and a pointer to the shared control block.  */
struct search_ctx {
    unsigned long long     nodes;
    int                    aborted;    /* sticky once any abort condition trips */
    struct search_shared  *shared;
    uint16_t               killers[SEARCH_MAX_DEPTH * 2][2];
    int                    history[2][7][128];
    struct pawn_hash_entry pawn_hash[PAWN_HASH_SIZE];
};

/* `can_null` gates null-move pruning: 1 from external callers, 0 only
   inside negamax's own null-move recursion. Internal to the search; not
   part of the public API.  */
int negamax(struct search_ctx *ctx, struct game *g, int depth, int ply,
            int alpha, int beta, int can_null);

/* Outcome of one worker's iterative-deepening run.  */
struct search_result {
    int         score;
    struct move best_move;
    int         have_best;
};

/* One Lazy SMP worker. Runs iterative deepening to `max_depth` on its
   private game `g`, sharing the TT and `ctx->shared` with the other
   workers. When `report` is nonzero (thread 0) it drives the `info`
   callback. Writes the final score and best move to *out. `start_ms` is
   the search start used for elapsed-time reporting.  */
void search_worker(struct search_ctx *ctx, struct game *g, int max_depth,
                   uint64_t start_ms, int report,
                   search_info_cb info, void *info_ctx,
                   struct search_result *out);

#endif /* CITT_HEADERS_SEARCH_INTERNAL_H_ */
