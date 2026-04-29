#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

#define EMPTY 0

enum color {
    white = 0,
    black
};

enum type {
    pawn = 1,
    bishop,
    knight,
    rook,
    queen,
    king
};

struct piece {
	enum color color;
    enum type type;
};

// A piece is packed into a single byte as: 0b00000CRR R
// where C is the color bit and RRR are the 3 rank bits.
// 
//      bit: 7 6 5 4 3 2 1 0
//           0 0 0 0 C R R R
//
// e.g. black rook  = (1 << 3) | 3 = 0b00001011 = 0x0B
//      white queen = (0 << 3) | 5 = 0b00000101 = 0x05
//
// &1 and &0x7 are both just masks that isolate as many bits as needed for each field, e.g. 1 for color, 3 for rank.
// The shift in piece_color moves the field down to bit 0 first so the mask lines up, piece_rank doesn't need a shift
// because the rank already lives at the bottom of the byte.

// Packs a color and type into a single byte.
//
// e.g. encode_piece(black, queen):
//      (1 << 3) = 0b00001000
//            4  = 0b00000100
//                 ----------
//            OR   0b00001100
static inline uint8_t
encode_piece(enum color c, enum type t)
{
    return ((uint8_t)c << 3) | (uint8_t)t;
}

// Extracts the color bit from a packed piece byte.
//
// e.g. piece_color(0b00001100):
//      >> 3 -> 0b00000001
//      & 1  -> 0b00000001 (black)
static inline enum color
piece_color(uint8_t p)
{
    assert(p != EMPTY);
    return (p >> 3) & 1;
}

// Extracts the rank from a packed piece byte.
//
// e.g. piece_rank(0b00001100):
//      & 0x7 -> 0b00000100 (queen)
static inline enum type
piece_type(uint8_t p)
{
    assert(p != EMPTY);
    return p & 0x7;
}

static inline int
is_empty(uint8_t p)
{
    return p == EMPTY;
}

static void
board_init(uint8_t board[128])
{
    memset(board, 0, 128 * sizeof(uint8_t));

    enum type back_rank[8] = {rook, knight, bishop, queen, king, bishop, knight, rook};

    for (int file = 0; file < 8; ++file)
        board[0x00 + file] = encode_piece(white, back_rank[file]);

    for (int file = 0; file < 8; ++file)
        board[0x10 + file] = encode_piece(white, pawn);

    for (int file = 0; file < 8; ++file)
        board[0x60 + file] = encode_piece(black, pawn);

    for (int file = 0; file < 8; ++file)
        board[0x70 + file] = encode_piece(black, back_rank[file]);
}

static void
board_print(uint8_t board[128])
{
    static const char symbols[2][7] = {
        {' ', 'p', 'b', 'n', 'r', 'q', 'k'}, // white (lowercase)
        {' ', 'P', 'B', 'N', 'R', 'Q', 'K'}  // black (uppercase)
    };

    uint8_t sq;
    char sym;

    for (int rank = 7; rank >= 0; --rank) {
        printf("%d  ", rank + 1);

        for (int file = 0; file < 8; ++file) {
            sq = board[(rank << 4) | file];
            sym = is_empty(sq) ? '.' : symbols[piece_color(sq)][piece_type(sq)];

            printf("%c ", sym);
        }

        printf("\n");
    }

    printf("\n   a b c d e f g h\n");
}

int
main(int argc, char **argv)
{
    // The board is encoded as a one-dimensional array of size 16x8 = 128, functioning as two adjacent
    // 8x8 boards where the board on the left represents the game state and the board on the right containing
    // illegal moves.
    //
    // https://en.wikipedia.org/wiki/0x88
    uint8_t board[128] = {0};

    board_init(board);
    board_print(board);

	return 0;
}
