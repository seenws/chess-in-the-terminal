#ifndef CITT_HEADERS_GAME_H_
#define CITT_HEADERS_GAME_H_

struct game {
    uint8_t board[128];
    enum color turn;
};

#endif
