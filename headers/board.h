#ifndef CITT_HEADERS_BOARD_H_
#define CITT_HEADERS_BOARD_H_

#include <stdint.h>
#include <assert.h>

#define EMPTY 0

enum color {
    COLOR_WHITE = 0,
    COLOR_BLACK
};

enum piece_type {
    PIECE_NONE = 0,
    PIECE_PAWN,
    PIECE_BISHOP,
    PIECE_KNIGHT,
    PIECE_ROOK,
    PIECE_QUEEN,
    PIECE_KING
};

/* 0x88 board: valid squares have (sq & 0x88) == 0; off-board sentinels fail
   that test, so movegen never special-cases edges.  */
static inline int
on_board(int sq)
{
    return (sq & 0x88) == 0;
}

/* Piece byte layout: bit 3 = color, bits 0..2 = piece_type.  */
static inline uint8_t
encode_piece(enum color c, enum piece_type t)
{
    return ((uint8_t)c << 3) | (uint8_t)t;
}

static inline enum color
piece_color(uint8_t p)
{
    assert(p != EMPTY);
    return (p >> 3) & 1;
}

static inline enum piece_type
piece_type(uint8_t p)
{
    assert(p != EMPTY);
    return p & 0x7;
}

static inline int
square_rank(int sq)
{
    return sq >> 4;
}

static inline int
square_file(int sq)
{
    return sq & 0x7;
}

static inline int
is_empty(uint8_t sq)
{
    return sq == EMPTY;
}

void board_init  (uint8_t board[128]);
void board_print (const uint8_t board[128]);

#endif
