#ifndef CITT_HEADERS_GAME_H_
#define CITT_HEADERS_GAME_H_

#include <stdint.h>

#include "board.h"

// Sentinel for ep_target meaning "no en passant capture available."
// 0xFF fails on_board(), so attack/move generation needs no special-case branch.
#define EP_NONE 0xFF

enum castle_rights {
    CASTLE_WK  = 1 << 0,
    CASTLE_WQ  = 1 << 1,
    CASTLE_BK  = 1 << 2,
    CASTLE_BQ  = 1 << 3,
    CASTLE_ALL = CASTLE_WK | CASTLE_WQ | CASTLE_BK | CASTLE_BQ,
};

struct game {
    uint8_t    board[128];
    enum color turn;
    uint8_t    castling;   // bitmask of enum castle_rights
    uint8_t    ep_target;  // 0x88 square, or EP_NONE
    uint8_t    halfmove;   // plies since last pawn move or capture (50-move rule)
    uint16_t   fullmove;   // increments after each black move, starts at 1
};

void game_init(struct game *g);
int  game_step(struct game *g);  // returns 0 to stop the game loop, nonzero to continue

#endif
