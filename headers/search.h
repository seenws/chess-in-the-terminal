#ifndef CITT_HEADERS_SEARCH_H_
#define CITT_HEADERS_SEARCH_H_

#include <stdint.h>
#include <stddef.h>

#include "board.h"
#include "movegen.h"

struct game;

#define SEARCH_INF       30000
#define SEARCH_MATE      29000
#define SEARCH_MAX_DEPTH 64

// Default depth used by game_step's engine hook. Lower in debug so the
// per-iteration prints stay readable and runs finish promptly.
#ifdef DEBUG
  #define AI_DEFAULT_DEPTH 3
#else
  #define AI_DEFAULT_DEPTH 5
#endif

enum tt_bound {
    TT_BOUND_NONE = 0,
    TT_BOUND_EXACT,
    TT_BOUND_LOWER,  // fail-high: true score >= score
    TT_BOUND_UPPER,  // fail-low:  true score <= score
};

// 16 bytes per entry. `key` is the full Zobrist hash so collisions in the
// index can be rejected by comparing against `g->hash`.
struct tt_entry {
    uint64_t key;
    int16_t  score;
    uint16_t move;   // packed from|to|flags|promo, 0 = no move
    uint8_t  depth;
    uint8_t  bound;  // enum tt_bound
    uint8_t  age;
    uint8_t  _pad;
};

void tt_init        (size_t mb);    // allocate to nearest power-of-two entries
void tt_free        (void);
void tt_clear       (void);
void tt_new_search  (void);         // bump age for replacement policy

int  tt_probe  (uint64_t key, int depth, int alpha, int beta,
                int *score_out, uint16_t *move_out);
void tt_store  (uint64_t key, int depth, int score,
                enum tt_bound bound, uint16_t move);

// Pack/unpack a struct move into 16 bits for storage in the TT.
// bits 0..6: from (0x88 square fits in 7 bits)
// bits 7..13: to
// bits 14..15: promo type low bits (combined with flags on unpack)
// Flags live in a separate field on disk-backed engines.
uint16_t      move_pack   (const struct move *m);
struct move   move_unpack (uint16_t packed);

int search_root(struct game *g, int max_depth, struct move *best);

// Internal but exposed for testing.
int negamax(struct game *g, int depth, int ply, int alpha, int beta);

// UCI-style move string ("e2e4", "a7a8q"). buf must hold at least 6 bytes.
void move_to_uci(const struct move *m, char buf[6]);

#endif // CITT_HEADERS_SEARCH_H_
