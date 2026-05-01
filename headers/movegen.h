#ifndef CITT_HEADERS_MOVEGEN_H_
#define CITT_HEADERS_MOVEGEN_H_

#include <stddef.h>

#include "board.h"

#define MAX_MOVES 218

enum move_flag {
    MOVE_QUIET    = 0,
    MOVE_CAPTURE  = 1 << 0,
    MOVE_ENP      = 1 << 1,
    MOVE_CASTLE_K = 1 << 2,
    MOVE_CASTLE_Q = 1 << 3,
    MOVE_PROMO    = 1 << 4,
};

struct move {
    uint8_t from;
    uint8_t to;
    enum move_flag flags;
    enum type promo;
};

struct move_list {
    struct move moves[MAX_MOVES];
    size_t count;
};

#endif
