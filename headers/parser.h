#ifndef CITT_HEADERS_PARSER_H_
#define CITT_HEADERS_PARSER_H_

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "game.h"
#include "movegen.h"

#define SAN_NONE 0xFF

enum san_castle {
    SAN_CASTLE_NONE = 0,
    SAN_CASTLE_K,
    SAN_CASTLE_Q,
};

struct san_move {
    enum piece_type  type;
    uint8_t          from_file;   // 0..7 or SAN_NONE if unspecified
    uint8_t          from_rank;   // 0..7 or SAN_NONE if unspecified
    uint8_t          to;          // 0x88 square (valid if castle == SAN_CASTLE_NONE)
    enum piece_type  promo;       // PIECE_NONE if no promotion specified
    enum san_castle  castle;
};

size_t get_line(char *buffer, size_t bufsz, FILE *file);
int    parse_san(char const *buffer, size_t bufsz, struct san_move *out);

const struct move *match_san(const struct move_list *list,
                             const struct san_move *sm,
                             const uint8_t board[128]);

#endif
