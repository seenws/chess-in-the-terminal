#include <stddef.h>

#include "board.h"
#include "game.h"
#include "movegen.h"

// Direction offsets in 0x88 coordinates. +16 is one rank up, +1 one file right.
static const int8_t knight_offsets[8] = { -33, -31, -18, -14, 14, 18, 31, 33 };
static const int8_t king_offsets[8]   = { -17, -16, -15,  -1,  1, 15, 16, 17 };
static const int8_t bishop_offsets[4] = { -17, -15, 15, 17 };
static const int8_t rook_offsets[4]   = { -16,  -1,  1, 16 };
static const int8_t queen_offsets[8]  = { -17, -16, -15, -1, 1, 15, 16, 17 };

// Returns 1 if `target_sq` is attacked by any piece of `by_color`, 0 otherwise.
// Walks outward from the target along each attack direction rather than
// generating the attacker's full move list: this is dramatically cheaper
// (a handful of rays vs. ~30 pseudolegal moves) and avoids the recursion
// hazard of having attack detection invoke move generation.
int
square_attacked(const uint8_t board[128], int target_sq, enum color by_color)
{
    const uint8_t pawn_byte   = encode_piece(by_color, PIECE_PAWN);
    const uint8_t knight_byte = encode_piece(by_color, PIECE_KNIGHT);
    const uint8_t bishop_byte = encode_piece(by_color, PIECE_BISHOP);
    const uint8_t rook_byte   = encode_piece(by_color, PIECE_ROOK);
    const uint8_t queen_byte  = encode_piece(by_color, PIECE_QUEEN);
    const uint8_t king_byte   = encode_piece(by_color, PIECE_KING);

    const int pawn_step = (by_color == COLOR_WHITE) ? -16 : 16;
    const int pawn_left  = target_sq + pawn_step - 1;
    const int pawn_right = target_sq + pawn_step + 1;

    if (on_board(pawn_left)  && board[pawn_left]  == pawn_byte) return 1;
    if (on_board(pawn_right) && board[pawn_right] == pawn_byte) return 1;

    for (int i = 0; i < 8; ++i) {
        int from = target_sq + knight_offsets[i];
        if (on_board(from) && board[from] == knight_byte) return 1;
    }

    for (int i = 0; i < 8; ++i) {
        int from = target_sq + king_offsets[i];
        if (on_board(from) && board[from] == king_byte) return 1;
    }

    for (int i = 0; i < 4; ++i) {
        int from = target_sq + bishop_offsets[i];
        
        while (on_board(from)) {
            uint8_t b = board[from];
            
            if (!is_empty(b)) {
                if (b == bishop_byte || b == queen_byte) return 1;        
                break;
            }
            
            from += bishop_offsets[i];
        }
    }

    for (int i = 0; i < 4; ++i) {
        int from = target_sq + rook_offsets[i];
        
        while (on_board(from)) {
            uint8_t b = board[from];
            
            if (!is_empty(b)) {
                if (b == rook_byte || b == queen_byte) return 1;
                break;
            }
            
            from += rook_offsets[i];
        }
    }

    return 0;
}

int
find_king(const uint8_t board[128], enum color c)
{
    const uint8_t target = encode_piece(c, PIECE_KING);

    for (int sq = 0; sq < 128; ++sq)
        if (on_board(sq) && board[sq] == target)
            return sq;

    return -1;
}

int
king_in_check(const struct game *g, enum color side)
{
    const int king_sq = find_king(g->board, side);
    
    if (king_sq < 0)
        return 0;

    const enum color enemy = (side == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
    
    return square_attacked(g->board, king_sq, enemy);
}

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

        // Own piece: blocked, no move emitted.
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
append_pawn_push_or_promote(struct move_list *list,
                            uint8_t from,
                            uint8_t to,
                            enum move_flag flags,
                            int promo_rank)
{
    if (square_rank(to) != promo_rank) {
        move_list_push(list, from, to, flags, PIECE_NONE);
        return;
    }

    static const enum piece_type promos[4] = {
        PIECE_QUEEN, PIECE_ROOK, PIECE_BISHOP, PIECE_KNIGHT
    };

    for (int i = 0; i < 4; ++i)
        move_list_push(list, from, to, flags | MOVE_PROMO, promos[i]);
}

static void
append_pawn_moves(const uint8_t board[128],
                  const uint8_t from,
                  uint8_t ep_target,
                  struct move_list *list)
{
    enum color c = piece_color(board[from]);

    const int forward    = (c == COLOR_WHITE) ?  16 : -16;
    const int start_rank = (c == COLOR_WHITE) ?   1 :   6;
    const int promo_rank = (c == COLOR_WHITE) ?   7 :   0;

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

        if (capture_target == ep_target) {
            move_list_push(list, from, capture_target,
                           MOVE_CAPTURE | MOVE_ENP, PIECE_NONE);
            continue;
        }

        uint8_t target_square = board[capture_target];

        if (is_empty(target_square) || piece_color(target_square) == c)
            continue;

        append_pawn_push_or_promote(list, from, capture_target,
                                    MOVE_CAPTURE, promo_rank);
    }
}

// The squares-empty check uses the file squares between king and rook (b1/c1/d1
// on the queenside; f1/g1 on the kingside) — b1 must be empty for the rook to
// slide through but is not a king-transit square, so it is checked for
// emptiness only, not for attack.
static void
append_castle_moves(const struct game *g, struct move_list *list)
{
    const enum color me    = g->turn;
    const enum color enemy = (me == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;

    // Per-side square layout. e/f/g/d/c are the king-transit squares (or
    // adjacent), and b is the additional empty-only square on the queenside.
    const int king_home = (me == COLOR_WHITE) ? 0x04 : 0x74;
    const int f_sq      = king_home + 1;   // f1 / f8
    const int g_sq      = king_home + 2;   // g1 / g8
    const int d_sq      = king_home - 1;   // d1 / d8
    const int c_sq      = king_home - 2;   // c1 / c8
    const int b_sq      = king_home - 3;   // b1 / b8

    const uint8_t right_k = (me == COLOR_WHITE) ? CASTLE_WK : CASTLE_BK;
    const uint8_t right_q = (me == COLOR_WHITE) ? CASTLE_WQ : CASTLE_BQ;

    int king_in_check_now = square_attacked(g->board, king_home, enemy);
    if (king_in_check_now)
        return;

    if ((g->castling & right_k)
        && is_empty(g->board[f_sq])
        && is_empty(g->board[g_sq])
        && !square_attacked(g->board, f_sq, enemy)
        && !square_attacked(g->board, g_sq, enemy))
    {
        move_list_push(list, king_home, g_sq, MOVE_CASTLE_K, PIECE_NONE);
    }

    if ((g->castling & right_q)
        && is_empty(g->board[d_sq])
        && is_empty(g->board[c_sq])
        && is_empty(g->board[b_sq])
        && !square_attacked(g->board, d_sq, enemy)
        && !square_attacked(g->board, c_sq, enemy))
    {
        move_list_push(list, king_home, c_sq, MOVE_CASTLE_Q, PIECE_NONE);
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

    append_castle_moves(g, list);
}