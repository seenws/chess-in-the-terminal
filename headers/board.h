#ifndef CITT_HEADERS_BOARD_H_
#define CITT_HEADERS_BOARD_H_

#include <stdint.h>

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

struct piece {
    enum color color;
    enum type type;
};

uint8_t             encode_piece    (enum color, enum type);
enum color          piece_color     (uint8_t);
enum type           piece_type      (uint8_t);
int                 is_empty        (uint8_t);
void                board_init      (uint8_t board[128]);
void                board_print     (uint8_t board[128]);

#endif
