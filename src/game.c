#include <stdio.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "game.h"
#include "movegen.h"
#include "parser.h"
#include "search.h"
#include "zobrist.h"

void
game_init(struct game *g)
{
    board_init(g->board);
    g->turn      = COLOR_WHITE;
    g->castling  = CASTLE_ALL;
    g->ep_target = EP_NONE;
    g->halfmove  = 0;
    g->fullmove  = 1;
    g->ai_white  = 0;
    g->ai_black  = 0;

    zobrist_init(0);
    g->hash = zobrist_compute(g);
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

    g->turn = (g->turn == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;

    if (g->turn == COLOR_WHITE)
        g->fullmove++;

    // First cut: recompute the hash from scratch. Cheap (one 0x88 walk) and
    // bug-resistant. Replace with incremental XORs inside this function once
    // castling-rights and halfmove updates land here too.
    g->hash = zobrist_compute(g);
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

    DBG_ASSERT(g->hash == zobrist_compute(g));

    int ai_turn = (g->turn == COLOR_WHITE) ? g->ai_white : g->ai_black;

    if (ai_turn) {
        struct move m;
        int score = search_root(g, AI_DEFAULT_DEPTH, &m);
        char uci[6];
        move_to_uci(&m, uci);
        printf("%s (engine) plays %s  [score = %d]\n",
               g->turn == COLOR_WHITE ? "white" : "black", uci, score);
        make_move(g, &m);
        return 1;
    }

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

    return 1;
}
