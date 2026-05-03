#include <assert.h>
#include <stdio.h>

/* Pulls in static helpers; do not link src/movegen.o (would duplicate append_pseudolegal_moves). */
#include "../src/movegen.c"

static void
test_white_knight_center_has_8_moves(void)
{
    uint8_t board[128] = { 0 };
    struct move_list list = { 0 };

    board[0x33] = encode_piece(COLOR_WHITE, PIECE_KNIGHT);

    append_leaper_moves(board, 0x33, knight_offsets, 8, &list);

    assert(list.count == 8);
}

int
main(void)
{
    test_white_knight_center_has_8_moves();
    printf("OK\n");
    return 0;
}
