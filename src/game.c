#include <stdio.h>
#include <string.h>

#include "board.h"
#include "game.h"
#include "movegen.h"
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

void
make_move(struct game *g, const struct move *m)
{
    uint8_t piece = g->board[m->from];
    enum color c  = piece_color(piece);

    g->board[m->to]   = piece;
    g->board[m->from] = EMPTY;

    if (m->flags & MOVE_PROMO)
        g->board[m->to] = encode_piece(c, m->promo);

    if (m->flags & MOVE_ENP) {
        int captured = (c == COLOR_WHITE) ? m->to - 16 : m->to + 16;
        g->board[captured] = EMPTY;
    }

    // Castling: the king has already moved above; reposition the rook.
    if (m->flags & MOVE_CASTLE_K) {
        g->board[m->to - 1] = g->board[m->to + 1];
        g->board[m->to + 1] = EMPTY;
    }
    if (m->flags & MOVE_CASTLE_Q) {
        g->board[m->to + 1] = g->board[m->to - 2];
        g->board[m->to - 2] = EMPTY;
    }

    g->ep_target = EP_NONE;
    if (piece_type(piece) == PIECE_PAWN) {
        int df = square_rank(m->to) - square_rank(m->from);
        if (df == 2 || df == -2)
            g->ep_target = (m->from + m->to) / 2;
    }
}

int
game_step(struct game *g)
{
    char buf[32];
    size_t nread;
    struct move_list list = { 0 };
    struct san_move sm;
    const struct move *chosen = NULL;

    board_print(g->board);
    append_pseudolegal_moves(g, &list);

    for (;;) {
        printf("%s to move > ", g->turn == COLOR_WHITE ? "white" : "black");
        fflush(stdout);

        memset(buf, 0, sizeof(buf));

        nread = get_line(buf, sizeof(buf) - 1, stdin);
        buf[nread] = '\0';

        if (nread == 0 && feof(stdin))
            return 0;

        if (nread == 0) {
            puts("Please enter a move.");
            continue;
        }

        if (!parse_san(buf, nread, &sm)) {
            puts("Invalid notation.");
            continue;
        }

        chosen = match_san(&list, &sm, g->board);
        if (chosen == NULL) {
            puts("Illegal move.");
            continue;
        }

        break;
    }

    make_move(g, chosen);

    g->turn = (g->turn == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;

    if (g->turn == COLOR_WHITE)
        g->fullmove++;

    return 1;
}
