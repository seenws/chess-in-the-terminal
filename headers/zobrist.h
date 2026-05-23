#ifndef CITT_HEADERS_ZOBRIST_H_
#define CITT_HEADERS_ZOBRIST_H_

#include <stdint.h>

struct game;

/* Random key per (piece byte, square), XORed into the position hash.  */
extern uint64_t z_piece[16][64];
/* Random key per castling-rights mask (4 bits → 16 entries).  */
extern uint64_t z_castle[16];
/* Random key per en-passant file (used only when an ep_target is set).  */
extern uint64_t z_ep_file[8];
/* Random key XORed in when the side to move is black.  */
extern uint64_t z_side;

/* Fills the key tables from `seed`. Idempotent: the first call wins,
   subsequent calls are no-ops. Must run before zobrist_compute or
   before any make_move that consults z_piece/z_castle/z_ep_file.  */
void     zobrist_init    (uint64_t seed);

/* Recomputes the position hash from scratch by walking board[]. Used
   at game_init / parse_fen; the search hot path relies on incremental
   updates inside make_move.  */
uint64_t zobrist_compute (const struct game *g);

#endif /* CITT_HEADERS_ZOBRIST_H_ */
