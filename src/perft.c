/* perft.c -- movegen correctness benchmark: count leaves vs reference.
   See https://www.chessprogramming.org/Perft_Results.  */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "attacks.h"
#include "board.h"
#include "game.h"
#include "movegen.h"
#include "search.h"

/* Expected leaf-node counts from the starting position at depths 0..N.  */
static const uint64_t perft_reference[] = {
    1ULL,           /* depth 0 */
    20ULL,
    400ULL,
    8902ULL,
    197281ULL,
    4865609ULL,
    119060324ULL,
    3195901860ULL,
};

#define PERFT_REFERENCE_MAX_DEPTH \
    ((int)(sizeof(perft_reference) / sizeof(perft_reference[0])) - 1)

static double
now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static uint64_t
perft(struct game *g, int depth)
{
    if (depth == 0)
        return 1;

    struct move_list ml = { 0 };
    append_pseudolegal_moves(g, &ml);

    uint64_t nodes = 0;
    for (size_t i = 0; i < ml.count; ++i) {
        enum color mover = g->turn;
        struct undo_state undo;
        make_move(g, &ml.moves[i], &undo);

        if (king_in_check(g, mover)) {
            unmake_move(g, &ml.moves[i], &undo);
            continue;
        }

        nodes += perft(g, depth - 1);

        unmake_move(g, &ml.moves[i], &undo);
    }

    return nodes;
}

/* Per-root-move breakdown; prints each root's subtotal.  */
static uint64_t
perft_divide(struct game *g, int depth)
{
    if (depth <= 0) {
        printf("(divide requires depth >= 1)\n");
        return 0;
    }

    struct move_list ml = { 0 };
    append_pseudolegal_moves(g, &ml);

    uint64_t total = 0;
    for (size_t i = 0; i < ml.count; ++i) {
        enum color mover = g->turn;
        struct undo_state undo;
        make_move(g, &ml.moves[i], &undo);

        if (king_in_check(g, mover)) {
            unmake_move(g, &ml.moves[i], &undo);
            continue;
        }

        uint64_t sub = perft(g, depth - 1);
        unmake_move(g, &ml.moves[i], &undo);

        total += sub;

        char uci[6];
        move_to_uci(&ml.moves[i], uci);
        printf("  %-6s %" PRIu64 "\n", uci, sub);
    }

    return total;
}

static void
print_usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [depth] [--divide]\n"
            "  depth     positive integer (default 5)\n"
            "  --divide  print per-root-move subtotals at `depth` instead of a depth ladder\n",
            prog);
}

int
main(int argc, char **argv)
{
    int max_depth = 5;
    int divide    = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--divide") == 0) {
            divide = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            char *end = NULL;
            long v = strtol(argv[i], &end, 10);

            if (end == argv[i] || *end != '\0' || v <= 0 || v > 16) {
                fprintf(stderr, "perft: bad argument: %s\n", argv[i]);
                print_usage(argv[0]);
                return 1;
            }

            max_depth = (int)v;
        }
    }

    attacks_init();

    struct game g;
    game_init(&g);

    if (divide) {
        printf("perft divide: depth=%d, starting position\n\n", max_depth);
        double t0 = now_seconds();
        uint64_t total = perft_divide(&g, max_depth);
        double dt = now_seconds() - t0;
        printf("\ntotal: %" PRIu64 "  time: %.3fs\n", total, dt);

        if (max_depth <= PERFT_REFERENCE_MAX_DEPTH) {
            uint64_t expected = perft_reference[max_depth];
            if (total == expected)
                printf("OK (matches reference for depth %d).\n", max_depth);
            else
                printf("MISMATCH: expected %" PRIu64 ", got %" PRIu64 ".\n",
                       expected, total);
        }
        return 0;
    }

    printf("perft from starting position, depths 1..%d\n", max_depth);
    printf("%-6s %-18s %-10s %-12s %s\n",
           "depth", "nodes", "time(s)", "knps", "status");

    int failed = 0;
    for (int d = 1; d <= max_depth; ++d) {
        double t0 = now_seconds();
        uint64_t nodes = perft(&g, d);
        double dt = now_seconds() - t0;
        double knps = dt > 0.0 ? (double)nodes / 1000.0 / dt : 0.0;

        const char *status;
        if (d <= PERFT_REFERENCE_MAX_DEPTH) {
            if (nodes == perft_reference[d])
                status = "OK";
            else {
                status = "FAIL";
                failed = 1;
            }
        } else {
            status = "(no reference)";
        }

        printf("%-6d %-18" PRIu64 " %-10.3f %-12.1f %s",
               d, nodes, dt, knps, status);

        if (strcmp(status, "FAIL") == 0)
            printf(" (expected %" PRIu64 ")", perft_reference[d]);

        putchar('\n');
    }

    return failed ? 1 : 0;
}
