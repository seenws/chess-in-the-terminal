#include "../headers/board.h"

int
main(void)
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
