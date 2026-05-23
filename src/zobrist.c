/* zobrist.c -- random key tables and full-position hash for the TT.  */

#include <stdint.h>

#include "board.h"
#include "game.h"
#include "zobrist.h"

uint64_t z_piece[16][128];
uint64_t z_castle[16];
uint64_t z_ep_file[8];
uint64_t z_side;

/* Nonzero once the key tables have been filled; protects against
   re-seeding mid-game.  */
static int zobrist_initialized = 0;

/* SplitMix64; see rosettacode.org/wiki/Pseudo-random_numbers/Splitmix64.  */
static uint64_t
splitmix64(uint64_t *s)
{
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);

    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;

    return z ^ (z >> 31);
}

void
zobrist_init(uint64_t seed)
{
    if (zobrist_initialized)
        return;

    uint64_t s = seed ? seed : 0xD1B54A32D192ED03ULL;

    for (int p = 0; p < 16; ++p)
        for (int sq = 0; sq < 128; ++sq)
            z_piece[p][sq] = splitmix64(&s);

    for (int i = 0; i < 16; ++i)
        z_castle[i]  = splitmix64(&s);

    for (int i = 0; i < 8; ++i)
        z_ep_file[i] = splitmix64(&s);

    z_side = splitmix64(&s);

    zobrist_initialized = 1;
}

uint64_t
zobrist_compute(const struct game *g)
{
    uint64_t hash = 0;

    for (int sq = 0; sq < 128; ++sq) {
        if (!on_board(sq))
            continue;

        uint8_t p = g->board[sq];

        if (is_empty(p))
            continue;

        hash ^= z_piece[p][sq];
    }

    hash ^= z_castle[g->castling & 0xF];

    if (g->ep_target != EP_NONE && on_board(g->ep_target))
        hash ^= z_ep_file[square_file(g->ep_target)];

    if (g->turn == COLOR_BLACK)
        hash ^= z_side;

    return hash;
}
