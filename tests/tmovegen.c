#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Pulls in static helpers; do not link src/movegen.o (would duplicate append_pseudolegal_moves). */
#include "../src/movegen.c"

/* board_init / board_print are linked from src/board.c by the Makefile. */
void board_init(uint8_t board[64]);
void board_print(const uint8_t board[64]);

/* ---------- debug helpers ----------
 * These are non-static so you can call them from gdb on any move list, e.g.:
 *
 *     (gdb) b test_pawn_capture
 *     (gdb) r
 *     (gdb) n            # step until list is populated
 *     (gdb) call print_move_list(&list)
 *     (gdb) call board_print(g.board)
 */

static void
square_to_algebraic(int sq, char out[3])
{
    out[0] = 'a' + file_of(sq);
    out[1] = '1' + rank_of(sq);
    out[2] = '\0';
}

void
print_move(const struct move *m)
{
    char from[3], to[3];
    square_to_algebraic(m->from, from);
    square_to_algebraic(m->to, to);

    char tag = '.';
    if      (m->flags & MOVE_CASTLE_K) tag = 'K';
    else if (m->flags & MOVE_CASTLE_Q) tag = 'Q';
    else if (m->flags & MOVE_ENP)      tag = 'e';
    else if (m->flags & MOVE_CAPTURE)  tag = 'x';

    printf("  %s -> %s  [%c]", from, to, tag);

    if (m->flags & MOVE_PROMO) {
        static const char promo_chars[] = ".pbnrqk";
        printf(" =%c", promo_chars[m->promo]);
    }

    putchar('\n');
}

void
print_move_list(const struct move_list *list)
{
    printf("  (%zu moves)\n", list->count);
    for (size_t i = 0; i < list->count; ++i)
        print_move(&list->moves[i]);
}

/* ---------- shared setup ---------- */

static void
setup_empty(struct game *g)
{
    memset(g, 0, sizeof(*g));
    g->turn      = COLOR_WHITE;
    g->ep_target = EP_NONE;
}

/* Recomputes the piece bitboards from board[]; the tests write pieces
   into the mailbox directly, so we sync before calling movegen.  */
static void
sync_bitboards(struct game *g)
{
    memset(g->pieces, 0, sizeof(g->pieces));
    g->occ[0] = g->occ[1] = 0;
    g->occ_all = 0;

    for (int sq = 0; sq < 64; ++sq) {
        uint8_t p = g->board[sq];
        if (is_empty(p))
            continue;
        uint64_t b = bit_of(sq);
        g->pieces[piece_color(p)][piece_type(p)] |= b;
        g->occ[piece_color(p)]                   |= b;
        g->occ_all                               |= b;
    }
}

/* ---------- tests ----------
 * Each test is independent. Set a breakpoint on the test function in gdb and
 * step into append_pseudolegal_moves to watch the move list grow.
 */

/* Lone white knight on d4 — 8 squares reachable, all empty. */
static void
test_lone_knight_d4(void)
{
    printf("\n=== test_lone_knight_d4 (expect 8) ===\n");
    struct game g;
    struct move_list list = { 0 };
    setup_empty(&g);

    g.board[27] = encode_piece(COLOR_WHITE, PIECE_KNIGHT);  /* d4 */
    board_print(g.board);
    sync_bitboards(&g);
    append_pseudolegal_moves(&g, &list);
    print_move_list(&list);

    assert(list.count == 8);
}

/* Lone white bishop on d4 — 4 + 3 + 3 + 3 = 13 squares along the diagonals. */
static void
test_lone_bishop_d4(void)
{
    printf("\n=== test_lone_bishop_d4 (expect 13) ===\n");
    struct game g;
    struct move_list list = { 0 };
    setup_empty(&g);

    g.board[27] = encode_piece(COLOR_WHITE, PIECE_BISHOP);  /* d4 */
    board_print(g.board);
    sync_bitboards(&g);
    append_pseudolegal_moves(&g, &list);
    print_move_list(&list);

    assert(list.count == 13);
}

/* Lone white rook on d4 — 7 along d-file + 7 along 4-rank = 14. */
static void
test_lone_rook_d4(void)
{
    printf("\n=== test_lone_rook_d4 (expect 14) ===\n");
    struct game g;
    struct move_list list = { 0 };
    setup_empty(&g);

    g.board[27] = encode_piece(COLOR_WHITE, PIECE_ROOK);  /* d4 */
    board_print(g.board);
    sync_bitboards(&g);
    append_pseudolegal_moves(&g, &list);
    print_move_list(&list);

    assert(list.count == 14);
}

/* Lone white queen on d4 — bishop + rook = 13 + 14 = 27. */
static void
test_lone_queen_d4(void)
{
    printf("\n=== test_lone_queen_d4 (expect 27) ===\n");
    struct game g;
    struct move_list list = { 0 };
    setup_empty(&g);

    g.board[27] = encode_piece(COLOR_WHITE, PIECE_QUEEN);  /* d4 */
    board_print(g.board);
    sync_bitboards(&g);
    append_pseudolegal_moves(&g, &list);
    print_move_list(&list);

    assert(list.count == 27);
}

/* Lone white king on d4 — 8 adjacent squares. */
static void
test_lone_king_d4(void)
{
    printf("\n=== test_lone_king_d4 (expect 8) ===\n");
    struct game g;
    struct move_list list = { 0 };
    setup_empty(&g);

    g.board[27] = encode_piece(COLOR_WHITE, PIECE_KING);  /* d4 */
    board_print(g.board);
    sync_bitboards(&g);
    append_pseudolegal_moves(&g, &list);
    print_move_list(&list);

    assert(list.count == 8);
}

/* White pawn on its start rank (e2): single push e3 + double push e4 = 2 moves. */
static void
test_pawn_e2_start(void)
{
    printf("\n=== test_pawn_e2_start (expect 2) ===\n");
    struct game g;
    struct move_list list = { 0 };
    setup_empty(&g);

    g.board[12] = encode_piece(COLOR_WHITE, PIECE_PAWN);  /* e2 */
    board_print(g.board);
    sync_bitboards(&g);
    append_pseudolegal_moves(&g, &list);
    print_move_list(&list);

    assert(list.count == 2);
}

/* White pawn on e2, black pawn on d3: push e3, double-push e4, capture d3 = 3 moves. */
static void
test_pawn_capture(void)
{
    printf("\n=== test_pawn_capture (expect 3) ===\n");
    struct game g;
    struct move_list list = { 0 };
    setup_empty(&g);

    g.board[12] = encode_piece(COLOR_WHITE, PIECE_PAWN);  /* e2 */
    g.board[19] = encode_piece(COLOR_BLACK, PIECE_PAWN);  /* d3 */
    board_print(g.board);
    sync_bitboards(&g);
    append_pseudolegal_moves(&g, &list);
    print_move_list(&list);

    assert(list.count == 3);
}

/* White pawn on e7 pushes to e8 — promotes to Q/R/B/N. 4 moves, all flagged MOVE_PROMO. */
static void
test_pawn_promo_e7(void)
{
    printf("\n=== test_pawn_promo_e7 (expect 4) ===\n");
    struct game g;
    struct move_list list = { 0 };
    setup_empty(&g);

    g.board[52] = encode_piece(COLOR_WHITE, PIECE_PAWN);  /* e7 */
    board_print(g.board);
    sync_bitboards(&g);
    append_pseudolegal_moves(&g, &list);
    print_move_list(&list);

    assert(list.count == 4);
    for (size_t i = 0; i < list.count; ++i)
        assert(list.moves[i].flags & MOVE_PROMO);
}

/* White pawn on e5, black just played d7-d5 (ep_target = d6).
 * Push e5 -> e6, plus en-passant capture e5 -> d6 = 2 moves. */
static void
test_pawn_en_passant(void)
{
    printf("\n=== test_pawn_en_passant (expect 2) ===\n");
    struct game g;
    struct move_list list = { 0 };
    setup_empty(&g);

    g.board[36] = encode_piece(COLOR_WHITE, PIECE_PAWN);  /* e5 */
    g.board[35] = encode_piece(COLOR_BLACK, PIECE_PAWN);  /* d5 (the pawn we'd capture) */
    g.ep_target = 43;                                     /* d6 */
    board_print(g.board);
    sync_bitboards(&g);
    append_pseudolegal_moves(&g, &list);
    print_move_list(&list);

    assert(list.count == 2);

    int found_ep = 0;
    for (size_t i = 0; i < list.count; ++i)
        if (list.moves[i].flags & MOVE_ENP)
            found_ep = 1;
    assert(found_ep);
}

/* Standard starting position: 16 pawn moves (8 single + 8 double) + 4 knight moves = 20. */
static void
test_initial_position(void)
{
    printf("\n=== test_initial_position (expect 20) ===\n");
    struct game g;
    struct move_list list = { 0 };
    setup_empty(&g);

    board_init(g.board);
    board_print(g.board);
    sync_bitboards(&g);
    append_pseudolegal_moves(&g, &list);
    print_move_list(&list);

    assert(list.count == 20);
}

int
main(void)
{
    attacks_init();

    test_lone_knight_d4();
    test_lone_bishop_d4();
    test_lone_rook_d4();
    test_lone_queen_d4();
    test_lone_king_d4();
    test_pawn_e2_start();
    test_pawn_capture();
    test_pawn_promo_e7();
    test_pawn_en_passant();
    test_initial_position();

    printf("\nOK\n");
    return 0;
}
