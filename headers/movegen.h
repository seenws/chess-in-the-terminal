#ifndef CITT_HEADERS_MOVEGEN_H_
#define CITT_HEADERS_MOVEGEN_H_

#include <stddef.h>
#include <assert.h>

#include "board.h"

/* 218 is the theoretical maximum legal-move count for any chess position.  */
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
    enum piece_type promo;
};

struct move_list {
    struct move moves[MAX_MOVES];
    size_t count;
};

static inline void
move_list_push(struct move_list *list,
               const uint8_t from,
               const uint8_t to,
               const enum move_flag flags,
               const enum piece_type promo)
{
    assert(list != NULL);
    assert(list->count < MAX_MOVES);

    list->moves[list->count++] = (struct move) {
        .from = from,
        .to = to,
        .flags = flags,
        .promo = promo
    };
}

struct game;

void append_pseudolegal_moves (const struct game *g, struct move_list *list);

/* Restores the position before returning, but takes a mutable pointer
   because it uses make/unmake internally.  */
void append_legal_moves       (struct game *g, struct move_list *list);

int  square_attacked (const uint8_t board[128], int target_sq, enum color by_color);
int  find_king       (const uint8_t board[128], enum color c);
int  king_in_check   (const struct game *g, enum color side);

#endif
