/* datagen.c -- self-play training-data generator for NNUE.

   Plays CITT-vs-CITT games with the existing search, and for every *quiet*
   position visited records a fixed-size binary sample: the packed board, the
   side to move, the search score (centipawns, side-to-move relative), and --
   back-filled once the game ends -- the game result (white POV). The trainer
   consumes these as (position -> eval, result) pairs.

   Output file layout (little-endian):
     header: magic "CITTDATA" (8 bytes) | u32 version | u32 record_size
     records: [board 32B (nibble/square)] [stm 1B] [score i16] [result 1B]
   Record count is (file_size - 16) / record_size.  */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "attacks.h"
#include "board.h"
#include "game.h"
#include "movegen.h"
#include "parser.h"
#include "search.h"

#define DATA_MAGIC      "CITTDATA"
#define DATA_VERSION    1u
#define RECORD_SIZE     36          /* 32 + 1 + 2 + 1 */

#define PLY_CAP         400         /* hard stop; long games adjudicated draw */
#define ADJUDICATE_CP   2000        /* |score| >= this ends the game decisively */
#define MAX_OPENING     8           /* up to this many random opening plies */

/* Result byte, white POV.  */
enum { RESULT_BLACK_WIN = 0, RESULT_DRAW = 1, RESULT_WHITE_WIN = 2 };

/* xorshift64* PRNG: deterministic self-play for a given --seed.  */
static uint64_t g_rng;

static uint64_t
xrand(void)
{
    g_rng ^= g_rng >> 12;
    g_rng ^= g_rng << 25;
    g_rng ^= g_rng >> 27;
    return g_rng * 0x2545F4914F6CDD1DULL;
}

static uint32_t
rand_below(uint32_t n)
{
    return (uint32_t)(xrand() % n);
}

/* One pending sample, board kept unpacked until the whole game is flushed.  */
struct sample {
    uint8_t board[64];
    uint8_t stm;
    int16_t score;
};

static void
pack_board(uint8_t out[32], const uint8_t board[64])
{
    memset(out, 0, 32);
    for (int sq = 0; sq < 64; ++sq) {
        uint8_t v = board[sq] & 0x0F;
        if (sq & 1)
            out[sq >> 1] |= (uint8_t)(v << 4);
        else
            out[sq >> 1] |= v;
    }
}

static int16_t
clamp_score(int s)
{
    if (s >  30000) return  30000;
    if (s < -30000) return -30000;
    return (int16_t)s;
}

static int
is_quiet(const struct game *g, const struct move *m)
{
    if (king_in_check(g, g->turn))
        return 0;
    if (m->flags & (MOVE_CAPTURE | MOVE_ENP | MOVE_PROMO))
        return 0;
    return 1;
}

/* Plays one game, buffering quiet samples, then writes them all stamped with
   the final result. Returns the number of records written.  */
static size_t
play_game(FILE *out, int depth)
{
    struct game   g;
    struct sample buf[PLY_CAP];
    size_t        n      = 0;
    int           result = RESULT_DRAW;

    game_init(&g);

    /* Random opening for position diversity.  */
    int opening = (int)rand_below(MAX_OPENING + 1);
    for (int i = 0; i < opening; ++i) {
        struct move_list  list = { 0 };
        struct undo_state undo;

        append_legal_moves(&g, &list);
        if (list.count == 0)
            break;
        make_move(&g, &list.moves[rand_below((uint32_t)list.count)], &undo);
    }

    for (int ply = 0; ply < PLY_CAP; ++ply) {
        struct move_list list = { 0 };
        append_legal_moves(&g, &list);

        if (list.count == 0) {
            if (king_in_check(&g, g.turn))
                result = (g.turn == COLOR_WHITE) ? RESULT_BLACK_WIN : RESULT_WHITE_WIN;
            else
                result = RESULT_DRAW;
            break;
        }
        if (g.halfmove >= 100) {
            result = RESULT_DRAW;
            break;
        }

        struct move m;
        int score = search_root(&g, depth, &m);

        if (score >= ADJUDICATE_CP) {
            result = (g.turn == COLOR_WHITE) ? RESULT_WHITE_WIN : RESULT_BLACK_WIN;
            break;
        }
        if (score <= -ADJUDICATE_CP) {
            result = (g.turn == COLOR_WHITE) ? RESULT_BLACK_WIN : RESULT_WHITE_WIN;
            break;
        }

        if (is_quiet(&g, &m) && n < PLY_CAP) {
            memcpy(buf[n].board, g.board, 64);
            buf[n].stm = (uint8_t)g.turn;
            buf[n].score = clamp_score(score);
            ++n;
        }

        struct undo_state undo;
        make_move(&g, &m, &undo);
    }

    for (size_t i = 0; i < n; ++i) {
        uint8_t rec[RECORD_SIZE];
        pack_board(rec, buf[i].board);
        rec[32] = buf[i].stm;
        memcpy(rec + 33, &buf[i].score, 2);
        rec[35] = (uint8_t)result;

        if (fwrite(rec, 1, RECORD_SIZE, out) != RECORD_SIZE)
            return n;   /* short write; caller will notice via ferror */
    }

    return n;
}

static void
print_usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [--games N] [--depth D] [--out FILE] [--seed S]\n"
            "  --games N   number of self-play games (default 1000)\n"
            "  --depth D   search depth per move (default 8)\n"
            "  --out FILE  output data file (default data.bin)\n"
            "  --seed S    PRNG seed for reproducibility (default 1)\n",
            prog);
}

int
main(int argc, char **argv)
{
    long        games = 1000;
    int         depth = 8;
    const char *path  = "data.bin";
    uint64_t    seed  = 1;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--games") == 0 && i + 1 < argc) {
            games = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--depth") == 0 && i + 1 < argc) {
            depth = (int)strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            path = argv[++i];
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = strtoull(argv[++i], NULL, 10);
        } else {
            fprintf(stderr, "datagen: bad argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (games <= 0 || depth <= 0) {
        fprintf(stderr, "datagen: --games and --depth must be positive\n");
        return 1;
    }

    g_rng = seed ? seed : 1;
    attacks_init();

    FILE *out = fopen(path, "wb");
    if (out == NULL) {
        fprintf(stderr, "datagen: cannot open %s\n", path);
        return 1;
    }

    uint32_t hdr[2] = { DATA_VERSION, RECORD_SIZE };
    if (fwrite(DATA_MAGIC, 1, 8, out) != 8
        || fwrite(hdr, sizeof hdr[0], 2, out) != 2) {
        fprintf(stderr, "datagen: header write failed\n");
        fclose(out);
        return 1;
    }

    size_t total = 0;
    for (long game = 0; game < games; ++game) {
        total += play_game(out, depth);

        if (ferror(out)) {
            fprintf(stderr, "datagen: write error after game %ld\n", game);
            fclose(out);
            return 1;
        }
        if ((game + 1) % 50 == 0 || game + 1 == games)
            fprintf(stderr, "\rgames %ld/%ld  samples %zu", game + 1, games, total);
    }
    fprintf(stderr, "\n");

    if (fclose(out) != 0) {
        fprintf(stderr, "datagen: close failed\n");
        return 1;
    }

    printf("wrote %zu samples from %ld games to %s\n", total, games, path);
    return 0;
}
