#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "board.h"
#include "movegen.h"
#include "parser.h"

// Reads at most bufsz bytes into buffer and returns the number of characters written to the buffer, or 0.
// The newline and discarded trailoff are not included in this count, nor NUL.
// The return value will never exceed the buffer size.
//
// There is no distinction made between 0 bytes read and EOF. To check for EOF, use feof(file).
size_t
get_line(char *buffer, size_t bufsz, FILE *file)
{
    char ch = 0;
    size_t nread;
    size_t buflen = 0;

    if (file == NULL || feof(file))
        return 0;

    if (ferror(file))
        return 0;

    while (buflen < bufsz) {
        if ((nread = fread(&ch, sizeof ch, 1, file)) == 0 || ch == '\n')
            break;

        buffer[buflen++] = ch;
    }

    if (buflen >= bufsz)
        while ((nread = fread(&ch, sizeof ch, 1, file)) > 0 && ch != '\n')
            ;

    return buflen;
}

static int
parse_promo_piece(char c, enum piece_type *out)
{
    switch (c) {
        case 'Q': *out = PIECE_QUEEN;  return 1;
        case 'R': *out = PIECE_ROOK;   return 1;
        case 'B': *out = PIECE_BISHOP; return 1;
        case 'N': *out = PIECE_KNIGHT; return 1;
        default: return 0;
    }
}

// https://en.wikipedia.org/wiki/Algebraic_notation_(chess)
// Parses one SAN token into *out and returns 1 on success, 0 on malformed input.
// Recognises: piece letter (NBRQK, pawn implicit), optional file/rank/both disambiguator,
// optional 'x' capture marker, destination square, optional "=X" promotion, trailing +/#/!/?
// annotations, and the castling tokens "O-O" / "O-O-O".
int
parse_san(char const *buffer, size_t bufsz, struct san_move *out)
{
    out->type      = PIECE_PAWN;
    out->from_file = SAN_NONE;
    out->from_rank = SAN_NONE;
    out->to        = SAN_NONE;
    out->promo     = PIECE_NONE;
    out->castle    = SAN_CASTLE_NONE;

    size_t len = 0;
    while (len < bufsz && buffer[len] != '\0')
        ++len;
    if (len == 0)
        return 0;

    while (len > 0 && (buffer[len-1] == '+' || buffer[len-1] == '#'
                       || buffer[len-1] == '!' || buffer[len-1] == '?'))
        --len;

    if (len == 0)
        return 0;

    if (len >= 5 && memcmp(buffer, "O-O-O", 5) == 0) {
        out->type   = PIECE_KING;
        out->castle = SAN_CASTLE_Q;
        return 1;
    }
    if (len >= 3 && memcmp(buffer, "O-O", 3) == 0) {
        out->type   = PIECE_KING;
        out->castle = SAN_CASTLE_K;
        return 1;
    }

    if (len >= 2 && buffer[len-2] == '=') {
        if (!parse_promo_piece(buffer[len-1], &out->promo))
            return 0;
        len -= 2;
    }

    // Drop the optional 'x' capture marker so positional indexing below is uniform.
    char body[8];
    size_t blen = 0;
    for (size_t i = 0; i < len; ++i) {
        if (buffer[i] == 'x')
            continue;
        if (blen >= sizeof(body))
            return 0;
        body[blen++] = buffer[i];
    }

    if (blen < 2)
        return 0;

    size_t pos = 0;
    switch (body[0]) {
        case 'N': out->type = PIECE_KNIGHT; pos = 1; break;
        case 'B': out->type = PIECE_BISHOP; pos = 1; break;
        case 'R': out->type = PIECE_ROOK;   pos = 1; break;
        case 'Q': out->type = PIECE_QUEEN;  pos = 1; break;
        case 'K': out->type = PIECE_KING;   pos = 1; break;
        default:  out->type = PIECE_PAWN; break;
    }

    size_t rest = blen - pos;
    if (rest < 2 || rest > 4)
        return 0;

    char tf = body[pos + rest - 2];
    char tr = body[pos + rest - 1];
    if (tf < 'a' || tf > 'h' || tr < '1' || tr > '8')
        return 0;
    out->to = ((uint8_t)(tr - '1') << 4) | (uint8_t)(tf - 'a');

    if (rest == 3) {
        char c = body[pos];
        if (c >= 'a' && c <= 'h')      out->from_file = c - 'a';
        else if (c >= '1' && c <= '8') out->from_rank = c - '1';
        else return 0;
    } else if (rest == 4) {
        char fc = body[pos];
        char rc = body[pos + 1];
        if (fc < 'a' || fc > 'h' || rc < '1' || rc > '8')
            return 0;
        out->from_file = fc - 'a';
        out->from_rank = rc - '1';
    }

    return 1;
}

// Walks the pseudolegal move list and returns the first move matching the parsed SAN, or NULL.
// Promotion handling: if SAN specified =X the match requires MOVE_PROMO with that piece;
// if SAN omitted =X on a promoting pawn move, we default to queen.
const struct move *
match_san(const struct move_list *list,
          const struct san_move *sm,
          const uint8_t board[128])
{
    for (size_t i = 0; i < list->count; ++i) {
        const struct move *m = &list->moves[i];

        if (sm->castle == SAN_CASTLE_K) {
            if (m->flags & MOVE_CASTLE_K)
                return m;
            continue;
        }
        if (sm->castle == SAN_CASTLE_Q) {
            if (m->flags & MOVE_CASTLE_Q)
                return m;
            continue;
        }

        if (m->to != sm->to)
            continue;

        if (piece_type(board[m->from]) != sm->type)
            continue;

        if (sm->from_file != SAN_NONE && square_file(m->from) != sm->from_file)
            continue;
        if (sm->from_rank != SAN_NONE && square_rank(m->from) != sm->from_rank)
            continue;

        if (sm->promo != PIECE_NONE) {
            if (!(m->flags & MOVE_PROMO) || m->promo != sm->promo)
                continue;
        } else if (m->flags & MOVE_PROMO) {
            if (m->promo != PIECE_QUEEN)
                continue;
        }

        return m;
    }
    return NULL;
}
