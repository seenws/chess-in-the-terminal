/* bench.c -- deterministic search benchmark over a fixed FEN suite.  */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "attacks.h"
#include "game.h"
#include "nnue.h"
#include "parser.h"
#include "search.h"

/* FEN positions exercised by the benchmark suite; each row is one trial.  */
static const struct {
    const char *name;
    const char *fen;
} positions[] = {
    { "startpos",     "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" },
    { "kiwipete",     "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1" },
    { "perft3",       "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1" },
    { "perft4",       "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2pP/R2Q1RK1 w kq - 0 1" },
    { "perft5",       "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8" },
    { "perft6",       "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10" },
    { "wac001",       "2rr3k/pp3pp1/1nnqbN1p/3pN3/2pP4/2P3Q1/PPB4P/R4RK1 w - - 0 1" },
    { "sicilian",     "rnbqkb1r/pp1p1ppp/2p1pn2/8/2PP4/2N2N2/PP2PPPP/R1BQKB1R b KQkq - 0 4" },
    { "endgame-kpk",  "8/8/8/3k4/8/3K4/3P4/8 w - - 0 1" },
};

#define POS_COUNT (sizeof(positions) / sizeof(positions[0]))

static double
now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void
print_usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [depth]\n"
            "       %s -f \"FEN\" [depth]\n"
            "  depth      iterative-deepening max depth (default 8)\n"
            "  -f FEN     run a single position from FEN instead of the suite\n",
            prog, prog);
}

/* Runs one row; wipes the TT and per-search tables for reproducibility.  */
static void
bench_one(const char *label, const struct game *seed, int depth,
          unsigned long long *total_nodes, double *total_time)
{
    struct game g = *seed;

    tt_clear();
    search_reset_state();

    struct move m;
    double      t0    = now_seconds();
    int         score = search_root(&g, depth, &m);
    double      dt    = now_seconds() - t0;

    unsigned long long nodes = search_get_nodes();
    double knps = (dt > 0.0) ? (double)nodes / 1000.0 / dt : 0.0;

    char uci[6];
    move_to_uci(&m, uci);

    printf("%-14s  nodes=%10" PRIu64 "  time=%6.3fs  %7.1f knps  best=%-5s score=%d\n",
           label, (uint64_t)nodes, dt, knps, uci, score);

    *total_nodes += nodes;
    *total_time += dt;
}

int
main(int argc, char **argv)
{
    attacks_init();

    /* Optional NNUE eval; absent or unreadable file falls back to classic.  */
    const char *evalfile = getenv("CITT_EVAL_FILE");
    if (evalfile != NULL && nnue_load(evalfile) != 0)
        fprintf(stderr, "warning: could not load eval file %s\n", evalfile);

    int         depth  = 8;
    const char *single = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }

        if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--fen") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "bench: -f requires a FEN argument\n");
                print_usage(argv[0]);
                return 1;
            }
            single = argv[i];
            continue;
        }

        char *end = NULL;
        long  v   = strtol(argv[i], &end, 10);
        if (end == argv[i] || *end != '\0' || v <= 0 || v > 32) {
            fprintf(stderr, "bench: bad argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
        depth = (int)v;
    }

    if (single) {
        struct game g;
        if (parse_fen(&g, single) != 0) {
            fprintf(stderr, "bench: FEN parse error: %s\n", single);
            return 1;
        }

        unsigned long long total_nodes = 0;
        double             total_time  = 0;
        bench_one("custom", &g, depth, &total_nodes, &total_time);
        return 0;
    }

    printf("bench suite: %zu positions, depth %d\n", POS_COUNT, depth);
    printf("%-14s  %-16s  %-13s  %-12s  %-10s %s\n",
           "position", "nodes", "time", "knps", "best", "score");

    unsigned long long total_nodes = 0;
    double             total_time  = 0;

    for (size_t i = 0; i < POS_COUNT; ++i) {
        struct game g;
        if (parse_fen(&g, positions[i].fen) != 0) {
            fprintf(stderr, "bench: FEN parse error in %s: %s\n",
                    positions[i].name, positions[i].fen);
            return 1;
        }
        bench_one(positions[i].name, &g, depth, &total_nodes, &total_time);
    }

    double total_knps = (total_time > 0.0)
        ? (double)total_nodes / 1000.0 / total_time
        : 0.0;

    printf("---\n");
    printf("%-14s  nodes=%10" PRIu64 "  time=%6.3fs  %7.1f knps\n",
           "total", (uint64_t)total_nodes, total_time, total_knps);

    return 0;
}
