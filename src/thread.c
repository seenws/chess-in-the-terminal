/* thread.c -- Lazy SMP worker pool. Spawns N search workers that share the
   transposition table and a single control block; thread 0 runs on the
   calling thread and is authoritative for the result.  */

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "search.h"
#include "search_internal.h"
#include "thread.h"

/* Per-worker bundle: its context, a private copy of the root position, the
   search parameters, and the slot its result lands in.  */
struct worker_arg {
    struct search_ctx   *ctx;
    struct game          g;
    int                  max_depth;
    uint64_t             start_ms;
    int                  report;
    search_info_cb       info;
    void                *info_ctx;
    struct search_result result;
};

static void *
worker_entry(void *p)
{
    struct worker_arg *a = p;
    search_worker(a->ctx, &a->g, a->max_depth, a->start_ms, a->report,
                  a->info, a->info_ctx, &a->result);
    return NULL;
}

void
thread_search(int n, const struct game *root, int max_depth, uint64_t start_ms,
              struct search_shared *shared, search_info_cb info, void *info_ctx,
              struct search_result *out)
{
    *out = (struct search_result){ 0 };
    if (n < 1) n = 1;

    struct search_ctx *ctxs = calloc((size_t)n, sizeof *ctxs);
    struct worker_arg *args = calloc((size_t)n, sizeof *args);
    pthread_t *helpers = calloc((size_t)n - 1, sizeof *helpers);

    if (!ctxs || !args || !helpers) {
        free(ctxs);
        free(args);
        free(helpers);
        return; /* out stays zeroed; ~sub-MB allocations make this unreachable */
    }

    for (int i = 0; i < n; ++i) {
        ctxs[i].shared    = shared; /* calloc already zeroed the rest */
        args[i].ctx       = &ctxs[i];
        args[i].g         = *root;  /* private copy keeps make/unmake thread-local */
        args[i].max_depth = max_depth;
        args[i].start_ms  = start_ms;
        args[i].report    = (i == 0);
        args[i].info      = (i == 0) ? info : NULL;
        args[i].info_ctx  = info_ctx;
    }

    /* Spawn helpers 1..n-1; a failed spawn just means that worker is absent,
       which only costs search efficiency, never correctness.  */
    int n_helpers = 0;
    for (int i = 1; i < n; ++i)
        if (pthread_create(&helpers[n_helpers], NULL, worker_entry, &args[i]) == 0)
            ++n_helpers;

    /* Thread 0 runs here and blocks until its iterative deepening ends.  */
    worker_entry(&args[0]);

    /* Thread 0 is done; wind the helpers down and collect them.  */
    shared->stop = 1;
    for (int i = 0; i < n_helpers; ++i)
        pthread_join(helpers[i], NULL);

    *out = args[0].result;

    free(helpers);
    free(args);
    free(ctxs);
}
