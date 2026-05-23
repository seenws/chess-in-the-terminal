#ifndef CITT_HEADERS_BITS_H_
#define CITT_HEADERS_BITS_H_

#include <stdint.h>

/* Bitboard primitives and little-endian rank-file square indexing.
   Squares run a1=0, b1=1, ..., h1=7, a2=8, ..., h8=63.
   rank = sq >> 3; file = sq & 7; sq = (rank << 3) | file.  */

enum file { FILE_A, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H };
enum rank { RANK_1, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8 };

static inline int
popcount(uint64_t b)
{
    return __builtin_popcountll(b);
}

static inline int
lsb(uint64_t b)
{
    return __builtin_ctzll(b);
}

static inline int
msb(uint64_t b)
{
    return 63 ^ __builtin_clzll(b);
}

static inline int
pop_lsb(uint64_t *b)
{
    int s = lsb(*b);
    *b &= *b - 1;
    return s;
}

static inline uint64_t  bit_of  (int sq)             { return 1ULL << sq; }
static inline int       make_sq (int rank, int file) { return (rank << 3) | file; }
static inline enum rank rank_of (int sq)             { return (enum rank)(sq >> 3); }
static inline enum file file_of (int sq)             { return (enum file)(sq & 7); }

/* Bitboard of every square on a given file; file_bb[FILE_A] = a1..a8.  */
static const uint64_t file_bb[8] = {
    0x0101010101010101ULL,
    0x0202020202020202ULL,
    0x0404040404040404ULL,
    0x0808080808080808ULL,
    0x1010101010101010ULL,
    0x2020202020202020ULL,
    0x4040404040404040ULL,
    0x8080808080808080ULL,
};

/* Bitboard of every square on a given rank; rank_bb[RANK_1] = a1..h1.  */
static const uint64_t rank_bb[8] = {
    0x00000000000000FFULL,
    0x000000000000FF00ULL,
    0x0000000000FF0000ULL,
    0x00000000FF000000ULL,
    0x000000FF00000000ULL,
    0x0000FF0000000000ULL,
    0x00FF000000000000ULL,
    0xFF00000000000000ULL,
};

#endif /* CITT_HEADERS_BITS_H_ */
