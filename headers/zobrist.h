#ifndef CITT_HEADERS_ZOBRIST_H_
#define CITT_HEADERS_ZOBRIST_H_

#include <stdint.h>

struct game;

extern uint64_t z_piece[16][128];
extern uint64_t z_castle[16];
extern uint64_t z_ep_file[8];
extern uint64_t z_side;

void     zobrist_init    (uint64_t seed);
uint64_t zobrist_compute (const struct game *g);

#endif // CITT_HEADERS_ZOBRIST_H_
