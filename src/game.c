/* game.c -- game state, make/unmake, legal-move filter, interactive step.  */

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "debug.h"
#include "game.h"
#include "movegen.h"
#include "parser.h"
#include "search.h"
#include "zobrist.h"

/* (Re)builds every incremental field on `g` from board[]. Callers that
   mutate board[] directly must call this before searching/evaluating.  */
void
compute_eval_state(struct game *g)
{
    memset(g->material, 0, sizeof(g->material));
    memset(g->psqt_mg,  0, sizeof(g->psqt_mg));
    memset(g->psqt_eg,  0, sizeof(g->psqt_eg));
    memset(g->bishops,  0, sizeof(g->bishops));
    memset(g->king_sq,  0, sizeof(g->king_sq));
    g->phase     = 0;
    g->pawn_hash = 0;

    for (int sq = 0; sq < 128; ++sq) {
        if (!on_board(sq))
            continue;

        uint8_t p = g->board[sq];
        if (is_empty(p))
            continue;

        enum color      c = piece_color(p);
        enum piece_type t = piece_type(p);

        if (t != PIECE_KING)
            g->material[c] += (int16_t)piece_value[t];

        struct pst_pair pp = pst_lookup(c, t, sq);
        g->psqt_mg[c] += pp.mg;
        g->psqt_eg[c] += pp.eg;

        g->phase += (int16_t)phase_weight[t];

        if (t == PIECE_BISHOP) g->bishops[c]++;
        if (t == PIECE_KING)   g->king_sq[c] = (uint8_t)sq;
        if (t == PIECE_PAWN)   g->pawn_hash ^= z_piece[p][sq];
    }
}

void
game_init(struct game *g)
{
    board_init(g->board);

    g->turn      = COLOR_WHITE;
    g->castling  = CASTLE_ALL;
    g->ep_target = EP_NONE;
    g->halfmove  = 0;
    g->fullmove  = 1;

    zobrist_init(0);
    g->hash = zobrist_compute(g);
    compute_eval_state(g);
}

/* Squares whose change of occupancy revokes castling rights: a king or
   rook leaving its home, or a capture landing on a rook home.  */
static const struct castle_rights_clear castle_rights_clears[6] = {
    { 0x04, CASTLE_WK | CASTLE_WQ },
    { 0x00, CASTLE_WQ              },
    { 0x07, CASTLE_WK              },
    { 0x74, CASTLE_BK | CASTLE_BQ },
    { 0x70, CASTLE_BQ              },
    { 0x77, CASTLE_BK              },
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
make_move(struct game *g, const struct move *m, struct undo_state *undo)
{
    undo->hash      = g->hash;
    undo->phase     = g->phase;
    undo->pawn_hash = g->pawn_hash;
    undo->captured  = g->board[m->to];
    undo->ep_target = g->ep_target;
    undo->castling  = g->castling;
    undo->halfmove  = g->halfmove;
    memcpy(undo->material, g->material, sizeof(g->material));
    memcpy(undo->psqt_mg,  g->psqt_mg,  sizeof(g->psqt_mg));
    memcpy(undo->psqt_eg,  g->psqt_eg,  sizeof(g->psqt_eg));
    memcpy(undo->bishops,  g->bishops,  sizeof(g->bishops));
    memcpy(undo->king_sq,  g->king_sq,  sizeof(g->king_sq));

    const uint8_t    piece   = g->board[m->from];
    const enum color color   = piece_color(piece);
    const enum color opp     = (color == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
    const int        is_pawn = piece_type(piece) == PIECE_PAWN;
    const uint8_t    victim  = g->board[m->to];
    const int        is_capt = !is_empty(victim) || (m->flags & MOVE_ENP);

    uint64_t h = g->hash;

    if (!is_empty(victim)) {
        enum piece_type vt = piece_type(victim);

        h ^= z_piece[victim][m->to];

        if (vt != PIECE_KING)
            g->material[opp] -= (int16_t)piece_value[vt];

        struct pst_pair pp = pst_lookup(opp, vt, m->to);
        g->psqt_mg[opp] -= pp.mg;
        g->psqt_eg[opp] -= pp.eg;

        g->phase -= (int16_t)phase_weight[vt];

        if (vt == PIECE_BISHOP)
            g->bishops[opp]--;
        if (vt == PIECE_PAWN)
            g->pawn_hash ^= z_piece[victim][m->to];
    }

    const enum piece_type moved_type  = piece_type(piece);
    const enum piece_type placed_type = (m->flags & MOVE_PROMO) ? m->promo : moved_type;

    h ^= z_piece[piece][m->from];

    g->board[m->from] = EMPTY;
    {
        struct pst_pair pp = pst_lookup(color, moved_type, m->from);
        g->psqt_mg[color] -= pp.mg;
        g->psqt_eg[color] -= pp.eg;
    }

    if (moved_type == PIECE_KING)
        g->king_sq[color] = m->to;

    if (is_pawn)
        g->pawn_hash ^= z_piece[piece][m->from];

    uint8_t placed = piece;

    if (m->flags & MOVE_PROMO) {
        placed = encode_piece(color, m->promo);

        g->material[color] += (int16_t)(piece_value[m->promo] - piece_value[PIECE_PAWN]);
        g->phase           += (int16_t)(phase_weight[m->promo] - phase_weight[PIECE_PAWN]);

        if (m->promo == PIECE_BISHOP)
            g->bishops[color]++;
    }

    g->board[m->to] = placed;
    h ^= z_piece[placed][m->to];
    if (is_pawn && !(m->flags & MOVE_PROMO))
        g->pawn_hash ^= z_piece[placed][m->to];
    {
        struct pst_pair pp = pst_lookup(color, placed_type, m->to);
        g->psqt_mg[color] += pp.mg;
        g->psqt_eg[color] += pp.eg;
    }

    if (m->flags & MOVE_ENP) {
        int     captured_sq = (color == COLOR_WHITE) ? m->to - 16 : m->to + 16;
        uint8_t victim_pawn = g->board[captured_sq];

        h ^= z_piece[victim_pawn][captured_sq];
        g->pawn_hash ^= z_piece[victim_pawn][captured_sq];

        g->board[captured_sq] = EMPTY;
        g->material[opp] -= (int16_t)piece_value[PIECE_PAWN];

        struct pst_pair pp = pst_lookup(opp, PIECE_PAWN, captured_sq);
        g->psqt_mg[opp] -= pp.mg;
        g->psqt_eg[opp] -= pp.eg;
    }

    if (m->flags & (MOVE_CASTLE_K | MOVE_CASTLE_Q)) {
        const int     ks        = m->flags & MOVE_CASTLE_K;
        const int     rook_from = ks ? m->to + 1 : m->to - 2;
        const int     rook_to   = ks ? m->to - 1 : m->to + 1;
        const uint8_t rook      = g->board[rook_from];

        h ^= z_piece[rook][rook_from] ^ z_piece[rook][rook_to];

        g->board[rook_to]   = rook;
        g->board[rook_from] = EMPTY;

        struct pst_pair pf = pst_lookup(color, PIECE_ROOK, rook_from);
        struct pst_pair pt = pst_lookup(color, PIECE_ROOK, rook_to);
        g->psqt_mg[color] += (int16_t)(pt.mg - pf.mg);
        g->psqt_eg[color] += (int16_t)(pt.eg - pf.eg);
    }

    const uint8_t cast_before = g->castling;
    update_castling_rights(g, m);
    if (g->castling != cast_before) {
        h ^= z_castle[cast_before & 0xF];
        h ^= z_castle[g->castling  & 0xF];
    }

    const uint8_t ep_before = g->ep_target;
    g->ep_target = EP_NONE;
    if (is_pawn) {
        int rank_delta = square_rank(m->to) - square_rank(m->from);
        if (rank_delta == 2 || rank_delta == -2)
            g->ep_target = (m->from + m->to) / 2;
    }

    if (ep_before    != EP_NONE) h ^= z_ep_file[square_file(ep_before)];
    if (g->ep_target != EP_NONE) h ^= z_ep_file[square_file(g->ep_target)];

    if (is_pawn || is_capt)
        g->halfmove = 0;
    else
        g->halfmove++;

    g->turn = (g->turn == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
    h ^= z_side;

    if (g->turn == COLOR_WHITE)
        g->fullmove++;

    g->hash = h;
}

/* `m` and `undo` must be the pair handed to the matching make_move.  */
void
unmake_move(struct game *g, const struct move *m, const struct undo_state *undo)
{
    if (g->turn == COLOR_WHITE)
        g->fullmove--;

    g->turn = (g->turn == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;

    const enum color color = g->turn;
    const enum color opp   = (color == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;

    g->hash      = undo->hash;
    g->phase     = undo->phase;
    g->pawn_hash = undo->pawn_hash;
    g->ep_target = undo->ep_target;
    g->castling  = undo->castling;
    g->halfmove  = undo->halfmove;
    memcpy(g->material, undo->material, sizeof(g->material));
    memcpy(g->psqt_mg,  undo->psqt_mg,  sizeof(g->psqt_mg));
    memcpy(g->psqt_eg,  undo->psqt_eg,  sizeof(g->psqt_eg));
    memcpy(g->bishops,  undo->bishops,  sizeof(g->bishops));
    memcpy(g->king_sq,  undo->king_sq,  sizeof(g->king_sq));

    /* Un-castle the rook before the king is restored below.  */
    if (m->flags & (MOVE_CASTLE_K | MOVE_CASTLE_Q)) {
        const int ks   = m->flags & MOVE_CASTLE_K;
        const int from = ks ? m->to + 1 : m->to - 2;
        const int to   = ks ? m->to - 1 : m->to + 1;
        g->board[from] = g->board[to];
        g->board[to]   = EMPTY;
    }

    g->board[m->from] = (m->flags & MOVE_PROMO)
        ? encode_piece(color, PIECE_PAWN)
        : g->board[m->to];

    g->board[m->to] = undo->captured;

    if (m->flags & MOVE_ENP) {
        int captured_sq = (color == COLOR_WHITE) ? m->to - 16 : m->to + 16;
        g->board[captured_sq] = encode_piece(opp, PIECE_PAWN);
    }
}

void
append_legal_moves(struct game *g, struct move_list *list)
{
    append_pseudolegal_moves(g, list);

    size_t kept = 0;
    for (size_t i = 0; i < list->count; ++i) {
        struct undo_state undo;
        make_move(g, &list->moves[i], &undo);

        enum color moved = (g->turn == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
        if (!king_in_check(g, moved))
            list->moves[kept++] = list->moves[i];

        unmake_move(g, &list->moves[i], &undo);
    }
    list->count = kept;
}

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
    char buf[32];
    size_t nread;
    struct move_list list = { 0 };
    struct san_move sm;
    const struct move *chosen = NULL;

    board_print(g->board);

    DBG_ASSERT(g->hash == zobrist_compute(g));

    if (report_terminal(g))
        return 0;

    int ai_turn = (g->turn == COLOR_WHITE) ? cfg->ai_white : cfg->ai_black;

    if (ai_turn) {
        struct move m;
        int score = search_root(g, AI_DEFAULT_DEPTH, &m);
        char uci[6];
        move_to_uci(&m, uci);
        printf("%s (engine) plays %s  [score = %d]\n",
               g->turn == COLOR_WHITE ? "white" : "black", uci, score);
        struct undo_state _unused;
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
