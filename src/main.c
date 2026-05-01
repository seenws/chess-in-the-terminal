#include <stdio.h>
#include <string.h>

#include "../headers/board.h"
#include "../headers/parser.h"

int
main(void)
{
    uint8_t board[128] = {0};
    
    char buf[32];
    size_t nread;

    board_init(board);
    board_print(board);

    // TODO: move into a game_step function later in game.c
    for (;;) {
        memset(buf, 0, sizeof(buf));

        nread = get_line(buf, 32, stdin);
        if (strcmp("quit", buf) == 0)
            break;

        printf("'%.*s', %lu\n", (int)nread, buf, nread);
    }

	return 0;
}
