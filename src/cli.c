/* cli.c -- interactive terminal play loop: board display, terminal-state
   reporting, and human/engine turn handling for one game step.  */

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "cli.h"
#include "debug.h"
#include "game.h"
#include "movegen.h"
#include "parser.h"
#include "search.h"
#include "zobrist.h"

/* Prints a result line and returns 1 when the game has ended.  */
static int
report_terminal(struct game *g)
{
    struct move_list legal = { 0 };
    append_legal_moves(g, &legal);

    if (legal.count == 0) {
        if (king_in_check(g, g->turn)) {
            const char *loser  = (g->turn == COLOR_WHITE) ? "White"  : "Black";
            const char *winner = (g->turn == COLOR_WHITE) ? "Black"  : "White";
            printf("Checkmate. %s wins. (%s is in check with no legal reply.)\n",
                   winner, loser);
        } else {
            printf("Stalemate. Draw.\n");
        }
        return 1;
    }

    if (g->halfmove >= 100) {
        printf("Draw by 50-move rule.\n");
        return 1;
    }

    return 0;
}

int
game_step(struct game *g, const struct ui_config *cfg)
{
    char               buf[32];
    size_t             nread;
    struct move_list   list   = { 0 };
    struct san_move    sm;
    const struct move *chosen = NULL;

    board_print(g->board);

    DBG_ASSERT(g->hash == zobrist_compute(g));

    if (report_terminal(g))
        return 0;

    int ai_turn = (g->turn == COLOR_WHITE) ? cfg->ai_white : cfg->ai_black;

    if (ai_turn) {
        struct move       m;
        struct undo_state _unused;
        char              uci[6];
        int               score;

        score = search_root(g, AI_DEFAULT_DEPTH, &m);
        move_to_uci(&m, uci);

        printf("%s (engine) plays %s  [score = %d]\n",
               g->turn == COLOR_WHITE ? "white" : "black", uci, score);

        make_move(g, &m, &_unused);
        return 1;
    }

    append_legal_moves(g, &list);

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

    {
        struct undo_state _unused;

        make_move(g, chosen, &_unused);
    }

    return 1;
}
