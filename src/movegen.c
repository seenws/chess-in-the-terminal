#include <stddef.h>

#include "../headers/board.h"
#include "../headers/movegen.h"

// Leaping pieces
static const int8_t knight_offsets[8] = { -33, -31, -18, -14, 14, 18, 31, 33 };
static const int8_t king_offsets[8] =   { -17, -16, -15,  -1,  1, 15, 16, 17 };

// Sliding pieces
static const int8_t bishop_offsets[4] = { -17, -15, 15, 17 };
static const int8_t rook_offsets[4]   = { -16, -1,   1, 16 };
static const int8_t queen_offsets[8]  = { -17, -16, -15, -1, 1, 15, 16, 17 };

static void
gen_leaper_moves(uint8_t board[128], uint8_t from, const int8_t *offsets, size_t n_offsets, struct move_list *list)
{
    enum color c = piece_color(board[from]);

    for (size_t i = 0; i < n_offsets; ++i) {
        int to = from + offsets[i];
        
        if (!on_board(to))
            continue;

        uint8_t target = board[to];

        if (is_empty(target))
            list->moves[list->count++] = (struct move){
                .from = from, .to = to, .flags = MOVE_QUIET
            };

        else if (piece_color(target) != c)
            list->moves[list->count++] = (struct move){
                .from = from, .to = to, .flags = MOVE_CAPTURE
            };

        // Own piece, can't move there.
    }
}

static void
gen_slider_moves(uint8_t board[128], uint8_t from, const int8_t *offsets, size_t n_offsets, struct move_list *list)
{
    enum color c = piece_color(board[from]);

    for (size_t i = 0; i < n_offsets; ++i) {
        int to = from + offsets[i];

        while (on_board(to)) {
            uint8_t target = board[to];

            if (is_empty(target))
                list->moves[list->count++] = (struct move){
                    .from = from, .to = to, .flags = MOVE_QUIET
                };

            else {
                if (piece_color(target) != c)
                    list->moves[list->count++] = (struct move){
                        .from = from, .to = to, .flags = MOVE_CAPTURE
                    };

                break;
            }

            to += offsets[i];
        }
    }
}
