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

// A king or rook moving from its starting square,  or any piece moving onto
// a rook's starting square (which can only happen when the rook is captured
// in place) invalidates the matching castling rights.
//
// Indexing by square keeps the logic branchless beyond the loop.
struct castle_rights_clear {
    uint8_t sq;
    uint8_t mask;
};

static const struct castle_rights_clear castle_rights_clears[6] = {
    { 0x04, CASTLE_WK | CASTLE_WQ },  // e1: white king
    { 0x00, CASTLE_WQ              }, // a1: white queenside rook
    { 0x07, CASTLE_WK              }, // h1: white kingside rook
    { 0x74, CASTLE_BK | CASTLE_BQ },  // e8: black king
    { 0x70, CASTLE_BQ              }, // a8: black queenside rook
    { 0x77, CASTLE_BK              }, // h8: black kingside rook
};

static void
update_castling_rights(struct game *g, const struct move *m)
{
    for (size_t i = 0; i < sizeof(castle_rights_clears) / sizeof(castle_rights_clears[0]); ++i) {
        const struct castle_rights_clear *c = &castle_rights_clears[i];

        if (m->from == c->sq || m->to == c->sq)
            g->castling &= ~c->mask;
    }
}

void
make_move(struct game *g, const struct move *m)
{
    const uint8_t    piece   = g->board[m->from];
    const enum color color   = piece_color(piece);
    const int        is_pawn = piece_type(piece) == PIECE_PAWN;
    const int        is_capt = !is_empty(g->board[m->to]) || (m->flags & MOVE_ENP);

    g->board[m->to]   = piece;
    g->board[m->from] = EMPTY;

    if (m->flags & MOVE_PROMO)
        g->board[m->to] = encode_piece(color, m->promo);

    if (m->flags & MOVE_ENP) {
        // The captured pawn sits behind the destination square (one rank
        // toward the capturing pawn's start rank).
        int captured_sq = (color == COLOR_WHITE) ? m->to - 16 : m->to + 16;
        g->board[captured_sq] = EMPTY;
    }

    // Castling: the king has already moved above; reposition the rook to
    // bracket the king on the inside square.
    if (m->flags & MOVE_CASTLE_K) {
        g->board[m->to - 1] = g->board[m->to + 1];
        g->board[m->to + 1] = EMPTY;
    }
    if (m->flags & MOVE_CASTLE_Q) {
        g->board[m->to + 1] = g->board[m->to - 2];
        g->board[m->to - 2] = EMPTY;
    }

    update_castling_rights(g, m);

    g->ep_target = EP_NONE;
    if (is_pawn) {
        int rank_delta = square_rank(m->to) - square_rank(m->from);
        if (rank_delta == 2 || rank_delta == -2)
            g->ep_target = (m->from + m->to) / 2;
    }

    if (is_pawn || is_capt)
        g->halfmove = 0;
    else
        g->halfmove++;

    g->turn = (g->turn == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;

    if (g->turn == COLOR_WHITE)
        g->fullmove++;

    // If after benchmarking I decide it matters I might replace this with incremental XORs.
    g->hash = zobrist_compute(g);
}

// Castling moves are already legality-checked by the pseudolegal generator, but they survive the filter
// regardless (their post-move king position is verified safe at generation).
//
// Defined here, in the same TU as make_move, so movegen.c stays free of any
// dependency on the move-application machinery. The test harness compiles movegen.c standalone.
void
append_legal_moves(const struct game *g, struct move_list *list)
{
    append_pseudolegal_moves(g, list);

    size_t kept = 0;
    for (size_t i = 0; i < list->count; ++i) {
        struct game child = *g;
        make_move(&child, &list->moves[i]);

        // After make_move the side-to-move has flipped; the player whose king
        // must not be in check is the one that just moved (== g->turn).
        if (!king_in_check(&child, g->turn))
            list->moves[kept++] = list->moves[i];
    }
    list->count = kept;
}

// Reports terminal positions on stdout once both sides have no legal reply.
// Returns 1 if the game has ended (and a message was printed), 0 otherwise.
static int
report_terminal(const struct game *g)
{
    struct move_list legal = { 0 };
    append_legal_moves(g, &legal);

    if (legal.count > 0)
        return 0;

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

    if (report_terminal(g))
        return 0;

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

    make_move(g, chosen);

    return 1;
}
