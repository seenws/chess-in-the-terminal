#ifndef CITT_HEADERS_BOARD_H_
#define CITT_HEADERS_BOARD_H_

#include <stdint.h>
#include <assert.h>

#define EMPTY 0

enum color {
    white = 0,
    black
};

enum type {
    pawn = 1,
    bishop,
    knight,
    rook,
    queen,
    king
};

static inline int
on_board(int sq)
{
    return (sq & 0x88) == 0;
}

// A piece is packed into a single byte as: 0b00000CRR R
// where C is the color bit and RRR are the 3 rank bits.
// 
//      bit: 7 6 5 4 3 2 1 0
//           0 0 0 0 C R R R
//
// e.g. black rook  = (1 << 3) | 3 = 0b00001011 = 0x0B
//      white queen = (0 << 3) | 5 = 0b00000101 = 0x05
//
// &1 and &0x7 are both just masks that isolate as many bits as needed for each field, e.g. 1 for color, 3 for rank.
// The shift in piece_color moves the field down to bit 0 first so the mask lines up, piece_rank doesn't need a shift
// because the rank already lives at the bottom of the byte.

// Packs a color and type into a single byte.
//
// e.g. encode_piece(black, queen):
//      (1 << 3) = 0b00001000
//            4  = 0b00000100
//                 ----------
//            OR   0b00001100
static inline uint8_t
encode_piece(enum color c, enum type t)
{
    return ((uint8_t)c << 3) | (uint8_t)t;
}

// Extracts the color bit from a packed piece byte.
//
// e.g. piece_color(0b00001100):
//      >> 3 -> 0b00000001
//      & 1  -> 0b00000001 (black)
static inline enum color
piece_color(uint8_t p)
{
    assert(p != EMPTY);
    return (p >> 3) & 1;
}

// Extracts the rank from a packed piece byte.
//
// e.g. piece_rank(0b00001100):
//      & 0x7 -> 0b00000100 (queen)
static inline enum type
piece_type(uint8_t p)
{
    assert(p != EMPTY);
    return p & 0x7;
}

static inline int
is_empty(uint8_t p)
{
    return p == EMPTY;
}

void                board_init      (uint8_t board[128]);
void                board_print     (uint8_t board[128]);

#endif
