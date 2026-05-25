#ifndef CITT_HEADERS_THREAD_H_
#define CITT_HEADERS_THREAD_H_

#include <stdint.h>

#include "search.h"          /* search_info_cb */
#include "search_internal.h" /* struct search_shared, struct search_result */

struct game;

/* Runs a Lazy SMP search with `n_threads` workers. Each worker gets a
   private copy of *root and its own search_ctx; all of them share *shared
   and the transposition table, which is the only mutable structure touched
   concurrently. Thread 0 runs on the calling thread, drives the `info`
   callback, and is authoritative for the result written to *out; helper
   threads only enrich the shared TT. Blocks until thread 0's search ends
   (depth reached, deadline, or stop), then raises stop and joins the
   helpers. On allocation failure *out is zeroed.  */
void thread_search(int n_threads, const struct game *root, int max_depth,
                   uint64_t start_ms, struct search_shared *shared,
                   search_info_cb info, void *info_ctx,
                   struct search_result *out);

#endif /* CITT_HEADERS_THREAD_H_ */
