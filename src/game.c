#include <stdio.h>
#include <string.h>

#include "board.h"
#include "game.h"
#include "parser.h"

void
game_init(struct game *g)
{
    board_init(g->board);
    g->turn      = COLOR_WHITE;
    g->castling  = CASTLE_ALL;
    g->ep_target = EP_NONE;
    g->halfmove  = 0;
    g->fullmove  = 1;
}

int
game_step(struct game *g)
{
    char buf[32];
    size_t nread;

    for (;;) {
        board_print(g->board);
        printf("%s to move > ", g->turn == COLOR_WHITE ? "white" : "black");
        fflush(stdout);

        memset(buf, 0, sizeof(buf));

        // taking 31 to leave a space for NUL
        nread = get_line(buf, 31, stdin);

        if (nread == 0 && feof(stdin))
            return 0;

        if (nread == 0) {
            puts("Please enter a move.");
            continue;
        }

        if (!parse_san(buf, nread)) {
            printf("'%s' - invalid SAN syntax\n", buf);
            continue;
        }

        break;
    }

    g->turn = (g->turn == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;

    if (g->turn == COLOR_WHITE)
        g->fullmove++;

    return 1;
}
