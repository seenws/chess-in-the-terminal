/* attacks.c -- precomputed leaper attacks and magic-bitboard sliders.

   Slider attacks use a per-square perfect hash: given the relevant
   blocker bitboard b, the attack set is
       table[sq][((occ & mask[sq]) * magic[sq]) >> shift[sq]]
   where mask[sq] excludes edge squares the slider cannot be blocked at,
   magic[sq] is a constant searched at init that maps every blocker
   subset of mask[sq] to a unique table slot, and shift[sq] is
   64 - popcount(mask[sq]). The tables are filled in attacks_init.  */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "attacks.h"
#include "bits.h"
#include "board.h"

/* (rank_delta, file_delta) pairs used to fill the leaper tables and
   walk the slider rays.  */
static const int knight_deltas[8][2] = {
    {+2,+1},{+2,-1},{-2,+1},{-2,-1},{+1,+2},{+1,-2},{-1,+2},{-1,-2}
};
static const int king_deltas[8][2] = {
    {+1,-1},{+1,0},{+1,+1},{0,-1},{0,+1},{-1,-1},{-1,0},{-1,+1}
};
static const int bishop_dirs[4][2] = { {+1,+1},{+1,-1},{-1,+1},{-1,-1} };
static const int rook_dirs  [4][2] = { {+1, 0},{-1, 0},{ 0,+1},{ 0,-1} };

/* Squares a knight on sq attacks.  */
static uint64_t knight_table[64];
/* Squares a king on sq attacks.  */
static uint64_t king_table[64];
/* Squares a pawn of color c on sq attacks (diagonally forward).  */
static uint64_t pawn_table[2][64];

/* Slider blocker-relevance masks. Edge squares are excluded because a
   blocker on an edge does not change what lies beyond.  */
static uint64_t bishop_mask[64];
static uint64_t rook_mask  [64];

/* Magic numbers and right-shifts used to index the per-square attack
   tables. The shift is always 64 - popcount(mask[sq]).  */
static uint64_t bishop_magic[64];
static uint64_t rook_magic  [64];
static int      bishop_shift[64];
static int      rook_shift  [64];

/* Slider attack tables, "fancy magic" layout: a single contiguous
   block per piece, with each square pointing to its own variable-sized
   region of 2^popcount(mask[sq]) entries. The totals 5248 and 102400
   are the well-known sums over all 64 squares for chess geometry; a
   runtime check in attacks_init verifies we filled exactly that many
   entries. Compared to a fixed [64][1 << 12] layout, this halves the
   working set and keeps slider queries in L1/L2 on most CPUs.  */
#define BISHOP_ATTACK_TABLE_SIZE   5248
#define ROOK_ATTACK_TABLE_SIZE   102400
static uint64_t  bishop_attack_data[BISHOP_ATTACK_TABLE_SIZE];
static uint64_t  rook_attack_data  [ROOK_ATTACK_TABLE_SIZE];
static uint64_t *bishop_attack_ptr[64];
static uint64_t *rook_attack_ptr  [64];

/* Set once the tables are filled; guards against repeat init.  */
static int tables_ready = 0;

/* leaper_mask: union of every in-bounds (sq + delta) for the given
   delta list. Used to fill the knight and king tables.  */
static uint64_t
leaper_mask(int sq, const int deltas[][2], int n)
{
    uint64_t m  = 0;
    int      r0 = rank_of(sq);
    int      f0 = file_of(sq);

    for (int i = 0; i < n; ++i) {
        int r = r0 + deltas[i][0];
        int f = f0 + deltas[i][1];

        if (r < 0 || r > 7 || f < 0 || f > 7)
            continue;

        m |= bit_of(make_sq(r, f));
    }
    return m;
}

/* ray_attacks: ground-truth attack bitboard for a slider at sq given
   the full occupancy `occ`. Each ray includes the first occupied square
   (a possible capture) and stops there. Used during init by find_magic
   and not on the search hot path.  */
static uint64_t
ray_attacks(int sq, uint64_t occ, const int dirs[][2], int n_dirs)
{
    uint64_t attacks = 0;
    int      r0      = rank_of(sq);
    int      f0      = file_of(sq);

    for (int d = 0; d < n_dirs; ++d) {
        int r = r0;
        int f = f0;

        while (1) {
            r += dirs[d][0];
            f += dirs[d][1];

            if (r < 0 || r > 7 || f < 0 || f > 7)
                break;

            uint64_t b = bit_of(make_sq(r, f));

            attacks |= b;
            if (occ & b) break;
        }
    }
    return attacks;
}

/* slider_mask: blocker-relevance squares for a slider at sq. Excludes
   the final ray square in each direction because a blocker there
   cannot change what the slider reaches beyond.  */
static uint64_t
slider_mask(int sq, const int dirs[][2], int n_dirs)
{
    uint64_t m  = 0;
    int      r0 = rank_of(sq);
    int      f0 = file_of(sq);

    for (int d = 0; d < n_dirs; ++d) {
        int dr = dirs[d][0];
        int df = dirs[d][1];
        int r  = r0 + dr;
        int f  = f0 + df;

        while (r >= 0 && r <= 7 && f >= 0 && f <= 7) {
            int rnext = r + dr;
            int fnext = f + df;

            if (rnext < 0 || rnext > 7 || fnext < 0 || fnext > 7)
                break;

            m |= bit_of(make_sq(r, f));
            r = rnext;
            f = fnext;
        }
    }
    return m;
}

/* mask_subset: returns the `idx`-th subset of `mask`. Walks the set
   bits of mask from low to high and adopts each into the result iff
   the matching low bit of idx is set. Enumerates all 2^popcount(mask)
   subsets as idx ranges over [0, 2^popcount(mask)).  */
static uint64_t
mask_subset(uint64_t mask, int idx)
{
    uint64_t result = 0;

    while (mask) {
        int bit = pop_lsb(&mask);

        if (idx & 1)
            result |= bit_of(bit);

        idx >>= 1;
    }
    return result;
}

/* xorshift64: small deterministic PRNG used to enumerate magic-number
   candidates. Deterministic across runs given the same seed.  */
static uint64_t
xorshift64(uint64_t *state)
{
    uint64_t x = *state;

    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;

    *state = x;
    return x;
}

/* sparse_candidate: ANDs three PRNG outputs. Magics with few set bits
   have fewer multiply carries and are more likely to perfect-hash the
   blocker subsets; this is the standard candidate generator.  */
static uint64_t
sparse_candidate(uint64_t *state)
{
    return xorshift64(state) & xorshift64(state) & xorshift64(state);
}

/* find_magic: searches for a magic number that maps every blocker
   subset of `mask` to a unique slot in `table[0 .. (1 << popcount(mask)) - 1]`,
   with each slot holding the corresponding attack bitboard. Returns
   the magic and writes the shift to *shift_out.

   `table[key] == 0` is treated as "slot unused": a slider always
   attacks at least one square, so a genuine attack set is never zero.

   Aborts on failure; magic numbers are known to exist for every chess
   slider mask, so a failure indicates a bug in this routine.  */
static uint64_t
find_magic(int sq, uint64_t mask, const int dirs[][2], int n_dirs,
           uint64_t *table, int *shift_out)
{
    int bits  = popcount(mask);
    int n     = 1 << bits;
    int shift = 64 - bits;

    /* Sized for the worst case (rook = 12 bits) so the same buffer is
       reusable across both piece types.  */
    uint64_t blockers[1 << 12];
    uint64_t attacks [1 << 12];

    for (int i = 0; i < n; ++i) {
        blockers[i] = mask_subset(mask, i);
        attacks [i] = ray_attacks(sq, blockers[i], dirs, n_dirs);
    }

    /* Seed mixes the square index so each search runs an independent
       sequence; same seed on each invocation makes the chosen magic
       deterministic across runs.  */
    uint64_t state = 0x9E3779B97F4A7C15ULL ^ ((uint64_t)sq * 0xBF58476D1CE4E5B9ULL);

    for (int tries = 0; tries < 100000000; ++tries) {
        uint64_t magic = sparse_candidate(&state);

        memset(table, 0, sizeof(uint64_t) * (size_t)n);

        int collision = 0;
        for (int i = 0; i < n; ++i) {
            uint64_t key = (blockers[i] * magic) >> shift;

            if (table[key] == 0) {
                table[key] = attacks[i];
            } else if (table[key] != attacks[i]) {
                collision = 1;
                break;
            }
        }

        if (!collision) {
            *shift_out = shift;
            return magic;
        }
    }

    fprintf(stderr, "attacks_init: magic search failed for square %d\n", sq);
    abort();
}

void
attacks_init(void)
{
    if (tables_ready)
        return;

    /* Leaper tables.  */
    for (int sq = 0; sq < 64; ++sq) {
        knight_table[sq] = leaper_mask(sq, knight_deltas, 8);
        king_table[sq]   = leaper_mask(sq, king_deltas,   8);

        int r = rank_of(sq);
        int f = file_of(sq);

        uint64_t w = 0;
        uint64_t b = 0;

        if (r < 7) {
            if (f > 0) w |= bit_of(make_sq(r + 1, f - 1));
            if (f < 7) w |= bit_of(make_sq(r + 1, f + 1));
        }
        if (r > 0) {
            if (f > 0) b |= bit_of(make_sq(r - 1, f - 1));
            if (f < 7) b |= bit_of(make_sq(r - 1, f + 1));
        }

        pawn_table[COLOR_WHITE][sq] = w;
        pawn_table[COLOR_BLACK][sq] = b;
    }

    /* Slider magic tables. Per-square pointers carve the contiguous
       data blocks into variable-sized regions sized to each square's
       blocker subset count.  */
    size_t bishop_offset = 0;
    size_t rook_offset   = 0;

    for (int sq = 0; sq < 64; ++sq) {
        bishop_mask[sq] = slider_mask(sq, bishop_dirs, 4);

        size_t bishop_entries = (size_t)1 << popcount(bishop_mask[sq]);
        bishop_attack_ptr[sq] = &bishop_attack_data[bishop_offset];
        bishop_magic[sq]      = find_magic(sq, bishop_mask[sq], bishop_dirs, 4,
                                           bishop_attack_ptr[sq], &bishop_shift[sq]);
        bishop_offset += bishop_entries;

        rook_mask[sq] = slider_mask(sq, rook_dirs, 4);

        size_t rook_entries = (size_t)1 << popcount(rook_mask[sq]);
        rook_attack_ptr[sq] = &rook_attack_data[rook_offset];
        rook_magic[sq]      = find_magic(sq, rook_mask[sq], rook_dirs, 4,
                                         rook_attack_ptr[sq], &rook_shift[sq]);
        rook_offset += rook_entries;
    }

    /* Sanity-check the hardcoded totals against the actual fill.  */
    if (bishop_offset != BISHOP_ATTACK_TABLE_SIZE
        || rook_offset != ROOK_ATTACK_TABLE_SIZE) {
        fprintf(stderr,
                "attacks_init: slider table size mismatch "
                "(bishop %zu/%d, rook %zu/%d)\n",
                bishop_offset, BISHOP_ATTACK_TABLE_SIZE,
                rook_offset,   ROOK_ATTACK_TABLE_SIZE);
        abort();
    }

    tables_ready = 1;
}

uint64_t
pawn_attacks(int sq, enum color c)
{
    return pawn_table[c][sq];
}

uint64_t
knight_attacks(int sq)
{
    return knight_table[sq];
}

uint64_t
king_attacks(int sq)
{
    return king_table[sq];
}

uint64_t
bishop_attacks(int sq, uint64_t occ)
{
    uint64_t key = ((occ & bishop_mask[sq]) * bishop_magic[sq]) >> bishop_shift[sq];

    return bishop_attack_ptr[sq][key];
}

uint64_t
rook_attacks(int sq, uint64_t occ)
{
    uint64_t key = ((occ & rook_mask[sq]) * rook_magic[sq]) >> rook_shift[sq];

    return rook_attack_ptr[sq][key];
}
