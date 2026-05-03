#include <stddef.h>

#include "board.h"
#include "game.h"
#include "movegen.h"

// Leaping pieces
static const int8_t knight_offsets[8] = { -33, -31, -18, -14, 14, 18, 31, 33 };
static const int8_t king_offsets[8] =   { -17, -16, -15,  -1,  1, 15, 16, 17 };

// Sliding pieces
static const int8_t bishop_offsets[4] = { -17, -15, 15, 17 };
static const int8_t rook_offsets[4]   = { -16, -1,   1, 16 };
static const int8_t queen_offsets[8]  = { -17, -16, -15, -1, 1, 15, 16, 17 };

static void
append_leaper_moves(const uint8_t      board[128],
                    uint8_t            from,
                    const int8_t      *offsets,
                    size_t             n_offsets,
                    struct move_list  *list)
{
    enum color c = piece_color(board[from]);

    for (size_t i = 0; i < n_offsets; ++i) {
        int to = from + offsets[i];
        
        if (!on_board(to))
            continue;

        uint8_t target = board[to];

        if (is_empty(target))
            move_list_push(list, from, to, MOVE_QUIET, PIECE_NONE);

        else if (piece_color(target) != c)
            move_list_push(list, from, to, MOVE_CAPTURE, PIECE_NONE);

        // Own piece, can't move there.
    }
}

static void
append_slider_moves(const uint8_t      board[128],
                    const uint8_t      from,
                    const int8_t      *offsets,
                    const size_t       n_offsets,
                    struct move_list  *list)
{
    enum color c = piece_color(board[from]);

    for (size_t i = 0; i < n_offsets; ++i) {
        int to = from + offsets[i];

        while (on_board(to)) {
            uint8_t target = board[to];

            if (is_empty(target))
                move_list_push(list, from, to, MOVE_QUIET, PIECE_NONE);

            else {
                if (piece_color(target) != c)
                    move_list_push(list, from, to, MOVE_CAPTURE, PIECE_NONE);

                break;
            }

            to += offsets[i];
        }
    }
}

static void
append_pawn_push_or_promote(struct move_list *list, uint8_t from, uint8_t to, enum move_flag flags, int promo_rank)
{
    if (square_rank(to) != promo_rank) {
        move_list_push(list, from, to, flags, PIECE_NONE);

        return;
    }

    static const enum piece_type promos[4] = { PIECE_QUEEN, PIECE_ROOK, PIECE_BISHOP, PIECE_KNIGHT };

    for (int i = 0; i < 4; ++i)
        move_list_push(list, from, to, flags | MOVE_PROMO, promos[i]);
}

static void
append_pawn_moves(const uint8_t board[128], const uint8_t from, uint8_t ep_target, struct move_list *list)
{
    enum color c = piece_color(board[from]);

    const int forward       = (c == COLOR_WHITE) ? 16 : -16;
    const int start_rank    = (c == COLOR_WHITE) ? 1 : 6;
    const int promo_rank    = (c == COLOR_WHITE) ? 7 : 0;
    
    const int diag_capture_offsets[2] = { forward - 1, forward + 1 };

    int to = from + forward;

    if (on_board(to) && is_empty(board[to])) {
        append_pawn_push_or_promote(list, from, to, MOVE_QUIET, promo_rank);

        if (square_rank(from) == start_rank) {
            int two_forward = from + 2 * forward;

            if (is_empty(board[two_forward]))
                move_list_push(list, from, two_forward, MOVE_QUIET, PIECE_NONE);
        }
    }

    for (int i = 0; i < 2; ++i) {
        int capture_target = from + diag_capture_offsets[i];

        if (!on_board(capture_target))
            continue;

        if(capture_target == ep_target) {
            move_list_push(list, from, capture_target, MOVE_CAPTURE | MOVE_ENP, PIECE_NONE);
            continue;
        }

        uint8_t target_square = board[capture_target];

        if (is_empty(target_square) || piece_color(target_square) == c)
            continue;

        append_pawn_push_or_promote(list, from, capture_target, MOVE_CAPTURE, promo_rank);
    }
}

void
append_pseudolegal_moves(const struct game *g, struct move_list *list)
{
    list->count = 0;

    for (int sq = 0; sq < 128; ++sq) {
        if (!on_board(sq))
            continue;

        uint8_t p = g->board[sq];

        if (is_empty(p) || piece_color(p) != g->turn)
            continue;

        switch (piece_type(p)) {
            case PIECE_PAWN:
                append_pawn_moves(g->board, sq, g->ep_target, list);
                break;

            case PIECE_BISHOP:
                append_slider_moves(g->board, sq, bishop_offsets, 4, list);
                break;

            case PIECE_KNIGHT:
                append_leaper_moves(g->board, sq, knight_offsets, 8, list);
                break;

            case PIECE_ROOK:
                append_slider_moves(g->board, sq, rook_offsets, 4, list);
                break;

            case PIECE_QUEEN:
                append_slider_moves(g->board, sq, queen_offsets, 8, list);
                break;

            case PIECE_KING:
                append_leaper_moves(g->board, sq, king_offsets, 8, list);
                break;

            case PIECE_NONE:
                break;
        }
    }
}
