#ifndef CITT_HEADERS_GAME_H_
#define CITT_HEADERS_GAME_H_

#include <stdint.h>

#include "board.h"

struct move;

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
    uint64_t   hash;       // Zobrist hash, kept in sync by make_move
    uint8_t    ai_white;   // 1 -> engine plays white in game_step
    uint8_t    ai_black;   // 1 -> engine plays black in game_step
};

void game_init(struct game *g);
int  game_step(struct game *g);  // returns 0 to stop the game loop, nonzero to continue
void make_move(struct game *g, const struct move *m);

#endif
