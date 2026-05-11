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

static int
parse_piece_letter(char c, enum piece_type *out)
{
    switch (c) {
        case 'N': *out = PIECE_KNIGHT; return 1;
        case 'B': *out = PIECE_BISHOP; return 1;
        case 'R': *out = PIECE_ROOK;   return 1;
        case 'Q': *out = PIECE_QUEEN;  return 1;
        case 'K': *out = PIECE_KING;   return 1;
        
        default: return 0;
    }
}

static int
encode_square_0x88(char file_ch, char rank_ch, uint8_t *out)
{
    if (file_ch < 'a' || file_ch > 'h' || rank_ch < '1' || rank_ch > '8')
        return 0;
    
    *out = ((uint8_t)(rank_ch - '1') << 4) | (uint8_t)(file_ch - 'a');
    
    return 1;
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

    // Castling tokens are matched whole; the longer one is checked first to avoid prefix conflict.
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

    // Consume the optional promotion suffix "=X" before further parsing, so the promotion
    // letter cannot later be mistaken for the moving-piece prefix.
    if (len >= 2 && buffer[len-2] == '=') {
        if (!parse_promo_piece(buffer[len-1], &out->promo))
            return 0;
        
        len -= 2;
    }

    // The remainder is the core move text: an optional piece letter, an optional
    // disambiguator, and a destination square. Copy it into a working buffer with any
    // 'x' capture marker removed so the remaining fields can be indexed positionally.
    //
    // Layout after the copy:
    //   [piece letter?] [disambiguator: 0, 1, or 2 chars] [target file] [target rank]
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

    // Optional leading piece letter; absence means a pawn move (already set above).
    size_t cursor = 0;
    
    if (parse_piece_letter(move[0], &out->type))
        cursor = 1;

    // The target square always occupies the final two characters.
    if (move_len < cursor + 2)
        return 0;
    
    if (!encode_square_0x88(move[move_len - 2], move[move_len - 1], &out->to))
        return 0;

    // Anything sitting between the piece letter and the target square is a disambiguator.
    size_t disambig_len = move_len - cursor - 2;

    if (disambig_len == 0)
        return 1;

    if (disambig_len == 1) {
        char disambig = move[cursor];
        
        if (disambig >= 'a' && disambig <= 'h')
            out->from_file = disambig - 'a';
        
        else if (disambig >= '1' && disambig <= '8')
            out->from_rank = disambig - '1';
        
        else return 0;
        
        return 1;
    }

    if (disambig_len == 2) {
        char from_file_ch = move[cursor];
        char from_rank_ch = move[cursor + 1];
        
        if (from_file_ch < 'a' || from_file_ch > 'h' || from_rank_ch < '1' || from_rank_ch > '8')
            return 0;
        
        out->from_file = from_file_ch - 'a';
        out->from_rank = from_rank_ch - '1';
        
        return 1;
    }

    return 0;
}

// Walks the pseudolegal move list and returns the first move matching the parsed SAN, or NULL.
// Promotion handling: if SAN specified =X the match requires MOVE_PROMO with that piece;
// if SAN omitted =X on a promoting pawn move, we default to queen.
const struct move *
match_san(const struct move_list *list, const struct san_move *sm, const uint8_t board[128])
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
