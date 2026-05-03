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

    board_print(g->board);
    printf("%s to move > ", g->turn == COLOR_WHITE ? "white" : "black");
    fflush(stdout);

    memset(buf, 0, sizeof(buf));
    nread = get_line(buf, sizeof(buf), stdin);

    if (nread == 0 && feof(stdin))
        return 0;

    if (strcmp("quit", buf) == 0)
        return 0;

    // TODO: parse SAN, validate against legal moves, apply via make_move.
    printf("'%.*s' (%zu chars)\n", (int)nread, buf, nread);

    g->turn = (g->turn == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
    if (g->turn == COLOR_WHITE)
        g->fullmove++;

    return 1;
}
