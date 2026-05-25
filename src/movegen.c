/* movegen.c -- bitboard-driven pseudolegal move generation and attack queries. */

#include <stddef.h>

#include "attacks.h"
#include "bits.h"
#include "board.h"
#include "game.h"
#include "movegen.h"

static inline void
emit_targets(struct move_list *list, int from, uint64_t targets, uint64_t enemy)
{
    uint64_t caps  = targets & enemy;
    uint64_t quiet = targets & ~enemy;

    while (caps) {
        int to = pop_lsb(&caps);
        move_list_push(list, from, to, MOVE_CAPTURE, PIECE_NONE);
    }
    while (quiet) {
        int to = pop_lsb(&quiet);
        move_list_push(list, from, to, MOVE_QUIET, PIECE_NONE);
    }
}

int
square_attacked(const struct game *g, int target_sq, enum color by_color)
{
    const enum color us = (by_color == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;

    if (pawn_attacks(target_sq, us) & g->pieces[by_color][PIECE_PAWN])   return 1;
    if (knight_attacks(target_sq)   & g->pieces[by_color][PIECE_KNIGHT]) return 1;
    if (king_attacks(target_sq)     & g->pieces[by_color][PIECE_KING])   return 1;

    const uint64_t diag  = g->pieces[by_color][PIECE_BISHOP] | g->pieces[by_color][PIECE_QUEEN];
    if (bishop_attacks(target_sq, g->occ_all) & diag) return 1;

    const uint64_t ortho = g->pieces[by_color][PIECE_ROOK] | g->pieces[by_color][PIECE_QUEEN];
    if (rook_attacks(target_sq, g->occ_all) & ortho) return 1;

    return 0;
}

int
king_in_check(const struct game *g, enum color side)
{
    const enum color enemy = (side == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
    
    return square_attacked(g, g->king_sq[side], enemy);
}

static void
append_pawn_moves(const struct game *g, struct move_list *list)
{
    const enum color us    = g->turn;
    const enum color them  = (us == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;

    const uint64_t   pawns = g->pieces[us][PIECE_PAWN];
    const uint64_t   empty = ~g->occ_all;
    const uint64_t   enemy = g->occ[them];
    const uint64_t   ep_bb = (g->ep_target != EP_NONE) ? bit_of(g->ep_target) : 0;

    const int      push      = (us == COLOR_WHITE) ?  8 : -8;
    const uint64_t promo_rk  = (us == COLOR_WHITE) ? rank_bb[RANK_8] : rank_bb[RANK_1];
    const uint64_t double_rk = (us == COLOR_WHITE) ? rank_bb[RANK_3] : rank_bb[RANK_6];
    const int      left_off  = (us == COLOR_WHITE) ?  7 : -9;
    const int      right_off = (us == COLOR_WHITE) ?  9 : -7;

    /* Single pushes */
    uint64_t pushes = (us == COLOR_WHITE)
        ? (pawns << 8) & empty
        : (pawns >> 8) & empty;

    uint64_t pushes_quiet = pushes & ~promo_rk;
    uint64_t pushes_promo = pushes &  promo_rk;

    /* Double pushes */
    uint64_t doubles = (us == COLOR_WHITE)
        ? ((pushes & double_rk) << 8) & empty
        : ((pushes & double_rk) >> 8) & empty;

    /* Diagonal captures (including EP square). File-edge mask prevents
       wraparound from the a/h files.  */
    uint64_t caps_left, caps_right;
    if (us == COLOR_WHITE) {
        caps_left  = ((pawns & ~file_bb[FILE_A]) << 7) & (enemy | ep_bb);
        caps_right = ((pawns & ~file_bb[FILE_H]) << 9) & (enemy | ep_bb);
    } else {
        caps_left  = ((pawns & ~file_bb[FILE_A]) >> 9) & (enemy | ep_bb);
        caps_right = ((pawns & ~file_bb[FILE_H]) >> 7) & (enemy | ep_bb);
    }

    uint64_t caps_left_promo  = caps_left  &  promo_rk;
    uint64_t caps_left_other  = caps_left  & ~promo_rk;
    uint64_t caps_right_promo = caps_right &  promo_rk;
    uint64_t caps_right_other = caps_right & ~promo_rk;

    /* Promotion choices, ordered most-likely-best first.  */
    static const enum piece_type promos[4] = {
        PIECE_QUEEN, PIECE_ROOK, PIECE_BISHOP, PIECE_KNIGHT
    };

    while (pushes_quiet) {
        int to = pop_lsb(&pushes_quiet);
        move_list_push(list, to - push, to, MOVE_QUIET, PIECE_NONE);
    }
    
    while (pushes_promo) {
        int to   = pop_lsb(&pushes_promo);
        int from = to - push;
        for (int i = 0; i < 4; ++i)
            move_list_push(list, from, to, MOVE_PROMO, promos[i]);
    }
    
    while (doubles) {
        int to = pop_lsb(&doubles);
        move_list_push(list, to - 2 * push, to, MOVE_QUIET, PIECE_NONE);
    }

    while (caps_left_other) {
        int            to    = pop_lsb(&caps_left_other);
        enum move_flag flags = MOVE_CAPTURE;
        if (bit_of(to) & ep_bb) flags |= MOVE_ENP;
        move_list_push(list, to - left_off, to, flags, PIECE_NONE);
    }
    
    while (caps_right_other) {
        int            to    = pop_lsb(&caps_right_other);
        enum move_flag flags = MOVE_CAPTURE;
        if (bit_of(to) & ep_bb) flags |= MOVE_ENP;
        move_list_push(list, to - right_off, to, flags, PIECE_NONE);
    }
    
    while (caps_left_promo) {
        int to   = pop_lsb(&caps_left_promo);
        int from = to - left_off;
        for (int i = 0; i < 4; ++i)
            move_list_push(list, from, to, MOVE_CAPTURE | MOVE_PROMO, promos[i]);
    }

    while (caps_right_promo) {
        int to   = pop_lsb(&caps_right_promo);
        int from = to - right_off;
        for (int i = 0; i < 4; ++i)
            move_list_push(list, from, to, MOVE_CAPTURE | MOVE_PROMO, promos[i]);
    }
}

static void
append_castle_moves(const struct game *g, struct move_list *list)
{
    const enum color me    = g->turn;
    const enum color enemy = (me == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;

    const int king_home = (me == COLOR_WHITE) ? 4 : 60;
    const int f_sq      = king_home + 1;
    const int g_sq      = king_home + 2;
    const int d_sq      = king_home - 1;
    const int c_sq      = king_home - 2;
    const int b_sq      = king_home - 3;

    const uint8_t right_k = (me == COLOR_WHITE) ? CASTLE_WK : CASTLE_BK;
    const uint8_t right_q = (me == COLOR_WHITE) ? CASTLE_WQ : CASTLE_BQ;

    if (square_attacked(g, king_home, enemy))
        return;

    if ((g->castling & right_k)
        && !(g->occ_all & (bit_of(f_sq) | bit_of(g_sq)))
        && !square_attacked(g, f_sq, enemy)
        && !square_attacked(g, g_sq, enemy))
    {
        move_list_push(list, king_home, g_sq, MOVE_CASTLE_K, PIECE_NONE);
    }

    if ((g->castling & right_q)
        && !(g->occ_all & (bit_of(d_sq) | bit_of(c_sq) | bit_of(b_sq)))
        && !square_attacked(g, d_sq, enemy)
        && !square_attacked(g, c_sq, enemy))
    {
        move_list_push(list, king_home, c_sq, MOVE_CASTLE_Q, PIECE_NONE);
    }
}

void
append_pseudolegal_moves(const struct game *g, struct move_list *list)
{
    list->count = 0;

    const enum color us    = g->turn;
    const enum color them  = (us == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
    const uint64_t   own   = g->occ[us];
    const uint64_t   enemy = g->occ[them];
    const uint64_t   occ   = g->occ_all;

    uint64_t knights = g->pieces[us][PIECE_KNIGHT];
    while (knights) {
        int from = pop_lsb(&knights);
        emit_targets(list, from, knight_attacks(from) & ~own, enemy);
    }

    uint64_t kings = g->pieces[us][PIECE_KING];
    while (kings) {
        int from = pop_lsb(&kings);
        emit_targets(list, from, king_attacks(from) & ~own, enemy);
    }

    uint64_t bishops = g->pieces[us][PIECE_BISHOP];
    while (bishops) {
        int from = pop_lsb(&bishops);
        emit_targets(list, from, bishop_attacks(from, occ) & ~own, enemy);
    }

    uint64_t rooks = g->pieces[us][PIECE_ROOK];
    while (rooks) {
        int from = pop_lsb(&rooks);
        emit_targets(list, from, rook_attacks(from, occ) & ~own, enemy);
    }

    uint64_t queens = g->pieces[us][PIECE_QUEEN];
    while (queens) {
        int from = pop_lsb(&queens);
        emit_targets(list, from, queen_attacks(from, occ) & ~own, enemy);
    }

    append_pawn_moves(g, list);
    append_castle_moves(g, list);
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
