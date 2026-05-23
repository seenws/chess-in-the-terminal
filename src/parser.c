/* parser.c -- SAN move parser, SAN-to-move matcher, and FEN loader.  */

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bits.h"
#include "board.h"
#include "game.h"
#include "movegen.h"
#include "parser.h"
#include "zobrist.h"

/* Reads up to bufsz bytes from `file` into `buffer`, stopping at newline
   or EOF. The newline and any discarded overflow are not counted; caller
   uses feof(file) to distinguish end-of-stream from a blank line.  */
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

/* Maps an uppercase piece letter to its piece_type; returns 1 on success.
   K is rejected unless `allow_king`; P unless `allow_pawn`.  */
static int
piece_from_letter(char c, enum piece_type *out, int allow_king, int allow_pawn)
{
    switch (c) {
        case 'N': *out = PIECE_KNIGHT; return 1;
        case 'B': *out = PIECE_BISHOP; return 1;
        case 'R': *out = PIECE_ROOK;   return 1;
        case 'Q': *out = PIECE_QUEEN;  return 1;
        case 'K': if (!allow_king) return 0; *out = PIECE_KING; return 1;
        case 'P': if (!allow_pawn) return 0; *out = PIECE_PAWN; return 1;
        default:  return 0;
    }
}

static int
encode_square(char file_ch, char rank_ch, uint8_t *out)
{
    if (file_ch < 'a' || file_ch > 'h' || rank_ch < '1' || rank_ch > '8')
        return 0;

    *out = (uint8_t)make_sq(rank_ch - '1', file_ch - 'a');
    return 1;
}

/* Parses one SAN token. Returns 1 on success, 0 on malformed input.
   Accepts piece letter (NBRQK; pawn implicit), optional disambiguator,
   optional 'x', destination square, optional "=X", trailing +/#/!/?,
   and the castling tokens "O-O" and "O-O-O".  */
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
        if (!piece_from_letter(buffer[len-1], &out->promo, 0, 0))
            return 0;
        len -= 2;
    }

    /* Working buffer with 'x' stripped; positional layout is then
         [piece letter?] [disambiguator 0..2 chars] [file] [rank].  */
    char   move[8];
    size_t move_len = 0;

    for (size_t i = 0; i < len; ++i) {
        if (buffer[i] == 'x')
            continue;
        if (move_len >= sizeof(move))
            return 0;
        move[move_len++] = buffer[i];
    }

    if (move_len < 2)
        return 0;

    size_t cursor = 0;
    if (piece_from_letter(move[0], &out->type, 1, 0))
        cursor = 1;

    if (move_len < cursor + 2)
        return 0;
    if (!encode_square(move[move_len - 2], move[move_len - 1], &out->to))
        return 0;

    size_t disambig_len = move_len - cursor - 2;

    if (disambig_len == 0)
        return 1;

    if (disambig_len == 1) {
        char d = move[cursor];
        if      (d >= 'a' && d <= 'h') out->from_file = d - 'a';
        else if (d >= '1' && d <= '8') out->from_rank = d - '1';
        else return 0;
        return 1;
    }

    if (disambig_len == 2) {
        char f = move[cursor];
        char r = move[cursor + 1];
        if (f < 'a' || f > 'h' || r < '1' || r > '8')
            return 0;
        out->from_file = f - 'a';
        out->from_rank = r - '1';
        return 1;
    }

    return 0;
}

/* Returns the first move in `list` matching `sm`, or NULL. Omitting "=X"
   on a promoting pawn move means queen.  */
const struct move *
match_san(const struct move_list *list, const struct san_move *sm, const uint8_t board[64])
{
    for (size_t i = 0; i < list->count; ++i) {
        const struct move *m = &list->moves[i];

        if (sm->castle == SAN_CASTLE_K) {
            if (m->flags & MOVE_CASTLE_K) return m;
            continue;
        }

        if (sm->castle == SAN_CASTLE_Q) {
            if (m->flags & MOVE_CASTLE_Q) return m;
            continue;
        }

        if (m->to != sm->to)
            continue;

        if (piece_type(board[m->from]) != sm->type)
            continue;

        if (sm->from_file != SAN_NONE && file_of(m->from) != sm->from_file)
            continue;
        if (sm->from_rank != SAN_NONE && rank_of(m->from) != sm->from_rank)
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

/* FEN fields: 1. placement (rank 8 first, '/' separators, digits = empties,
   KQRBNP/kqrbnp), 2. side ('w'|'b'), 3. castling, 4. ep square or '-',
   5. halfmove, 6. fullmove.  */
int
parse_fen(struct game *g, const char *fen)
{
    if (!fen) return -1;

    memset(g->board, EMPTY, sizeof(g->board));

    const char *p = fen;

    int rank = 7;
    int file = 0;
    for (; *p && *p != ' '; ++p) {
        char c = *p;

        if (c == '/') {
            if (file != 8 || rank == 0) return -1;
            --rank;
            file = 0;
            continue;
        }

        if (c >= '1' && c <= '8') {
            file += c - '0';
            if (file > 8) return -1;
            continue;
        }

        enum color      color;
        enum piece_type type;

        if      (c >= 'A' && c <= 'Z') color = COLOR_WHITE;
        else if (c >= 'a' && c <= 'z') { color = COLOR_BLACK; c = (char)(c - 32); }
        else return -1;

        if (!piece_from_letter(c, &type, 1, 1)) return -1;

        if (file >= 8) return -1;
        g->board[make_sq(rank, file)] = encode_piece(color, type);
        ++file;
    }
    if (rank != 0 || file != 8) return -1;
    if (*p++ != ' ') return -1;

    if (*p == 'w')      g->turn = COLOR_WHITE;
    else if (*p == 'b') g->turn = COLOR_BLACK;
    else return -1;
    ++p;
    if (*p++ != ' ') return -1;

    g->castling = 0;
    if (*p == '-') {
        ++p;
    } else {
        while (*p && *p != ' ') {
            switch (*p) {
                case 'K': g->castling |= CASTLE_WK; break;
                case 'Q': g->castling |= CASTLE_WQ; break;
                case 'k': g->castling |= CASTLE_BK; break;
                case 'q': g->castling |= CASTLE_BQ; break;
                default: return -1;
            }
            ++p;
        }
    }
    if (*p++ != ' ') return -1;

    if (*p == '-') {
        g->ep_target = EP_NONE;
        ++p;
    } else {
        if (p[0] < 'a' || p[0] > 'h') return -1;
        if (p[1] < '1' || p[1] > '8') return -1;
        g->ep_target = (uint8_t)make_sq(p[1] - '1', p[0] - 'a');
        p += 2;
    }
    if (*p++ != ' ') return -1;

    char *end = NULL;
    long  hm  = strtol(p, &end, 10);
    if (end == p || hm < 0 || hm > 255) return -1;
    g->halfmove = (uint8_t)hm;
    p = end;
    if (*p++ != ' ') return -1;

    long fm = strtol(p, &end, 10);
    if (end == p || fm < 1 || fm > 65535) return -1;
    g->fullmove = (uint16_t)fm;

    zobrist_init(0);
    g->hash = zobrist_compute(g);
    compute_eval_state(g);

    return 0;
}
