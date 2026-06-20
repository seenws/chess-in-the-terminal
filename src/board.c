/* board.c -- starting-position setup and ASCII rendering for the 8x8 board.  */

#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

#include "bits.h"
#include "board.h"

void
board_init(uint8_t board[64])
{
    /* Piece type on each file (a..h) of the starting back rank.  */
    static const enum piece_type back_rank[8] = {
        PIECE_ROOK, PIECE_KNIGHT, PIECE_BISHOP, PIECE_QUEEN,
        PIECE_KING, PIECE_BISHOP, PIECE_KNIGHT, PIECE_ROOK
    };

    memset(board, 0, 64 * sizeof(uint8_t));

    for (int file = 0; file < 8; ++file) {
        board[make_sq(0, file)] = encode_piece(COLOR_WHITE, back_rank[file]);
        board[make_sq(1, file)] = encode_piece(COLOR_WHITE, PIECE_PAWN);
        board[make_sq(6, file)] = encode_piece(COLOR_BLACK, PIECE_PAWN);
        board[make_sq(7, file)] = encode_piece(COLOR_BLACK, back_rank[file]);
    }
}

/* FEN glyph for each (color, piece_type); type 0 is unused.  */
static const char piece_symbols[2][7] = {
    {' ', 'P', 'B', 'N', 'R', 'Q', 'K'},
    {' ', 'p', 'b', 'n', 'r', 'q', 'k'}
};

char
piece_to_letter(uint8_t piece)
{
    return piece_symbols[piece_color(piece)][piece_type(piece)];
}

void
board_print(const uint8_t board[64])
{
    for (int rank = 7; rank >= 0; --rank) {
        printf("%d  ", rank + 1);

        for (int file = 0; file < 8; ++file) {
            uint8_t sq = board[make_sq(rank, file)];
            char sym = is_empty(sq) ? '.' : piece_to_letter(sq);

            printf("%c ", sym);
        }

        printf("\n");
    }

    printf("\n   a b c d e f g h\n\n");
}
