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

/* `from_file`/`from_rank` are SAN_NONE when the SAN had no disambiguator.
   `to` is meaningful only when castle == SAN_CASTLE_NONE.  */
struct san_move {
    enum piece_type  type;
    uint8_t          from_file;
    uint8_t          from_rank;
    uint8_t          to;
    enum piece_type  promo;
    enum san_castle  castle;
};

size_t get_line  (char *buffer, size_t bufsz, FILE *file);
int    parse_san (char const *buffer, size_t bufsz, struct san_move *out);

const struct move *match_san(const struct move_list *list,
                             const struct san_move *sm,
                             const uint8_t board[128]);

/* Returns 0 on success, -1 on parse error. On error `g` is left in an
   inconsistent state and should be discarded.  */
int parse_fen(struct game *g, const char *fen);

#endif /* CITT_HEADERS_PARSER_H_ */
