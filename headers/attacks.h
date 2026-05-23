#ifndef CITT_HEADERS_ATTACKS_H_
#define CITT_HEADERS_ATTACKS_H_

#include <stdint.h>

#include "board.h"

void attacks_init(void);

uint64_t pawn_attacks   (int sq, enum color c);
uint64_t knight_attacks (int sq);
uint64_t king_attacks   (int sq);

uint64_t bishop_attacks (int sq, uint64_t occ);
uint64_t rook_attacks   (int sq, uint64_t occ);

static inline uint64_t
queen_attacks(int sq, uint64_t occ)
{
    return bishop_attacks(sq, occ) | rook_attacks(sq, occ);
}

#endif /* CITT_HEADERS_ATTACKS_H_ */
