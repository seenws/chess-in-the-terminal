/* nnue_ctypes.c -- C ABI shim for the Python trainer (loaded via ctypes).

   Exposes the engine's exact HalfKP feature indexing and the quantized
   evaluator so the trainer and the parity check never reimplement either in
   Python. Compiled into libcittnnue.so alongside nnue.c and accumulator.c.

   All positions cross the boundary as a 64-byte mailbox using the engine's
   piece encoding (0 = empty, else color<<3 | type), matching the datagen
   record format once its nibbles are unpacked.  */

#include <stdint.h>
#include <string.h>

#include "board.h"
#include "game.h"
#include "nnue.h"

/* Locates both kings on a mailbox; needed because HalfKP indices and the
   evaluator are relative to each side's king square.  */
static void
find_kings(const uint8_t board[64], int king_sq[2])
{
    king_sq[COLOR_WHITE] = king_sq[COLOR_BLACK] = 0;
    for (int sq = 0; sq < 64; ++sq) {
        uint8_t p = board[sq];
        if (!is_empty(p) && piece_type(p) == PIECE_KING)
            king_sq[piece_color(p)] = sq;
    }
}

/* Batch HalfKP feature extraction. For each of `n` positions (boards is
   n*64 bytes), writes the active feature indices for both perspectives into
   white_idx / black_idx (row stride `max_feat`, >= 30) and the active count
   into counts[i]. Piece count is identical across perspectives, so one count
   serves both. Entries past counts[i] are left untouched; the caller reads
   only the first counts[i] of each row.  */
void
citt_extract_batch(const uint8_t *boards, int n, int max_feat,
                   int32_t *white_idx, int32_t *black_idx, int32_t *counts)
{
    for (int i = 0; i < n; ++i) {
        const uint8_t *board = boards + (size_t)i * 64;
        int king_sq[2];
        int c = 0;

        find_kings(board, king_sq);

        for (int sq = 0; sq < 64 && c < max_feat; ++sq) {
            uint8_t p = board[sq];
            if (is_empty(p) || piece_type(p) == PIECE_KING)
                continue;

            size_t base = (size_t)i * max_feat + c;
            white_idx[base] = nnue_feature_index(COLOR_WHITE, king_sq[COLOR_WHITE], sq, p);
            black_idx[base] = nnue_feature_index(COLOR_BLACK, king_sq[COLOR_BLACK], sq, p);
            ++c;
        }
        counts[i] = c;
    }
}

/* Evaluates one position with the currently loaded net; returns centipawns
   from `stm`'s perspective (matching nnue_evaluate). Builds the minimal
   struct game the evaluator needs: board, both king squares, and the turn.  */
int
citt_eval_board(const uint8_t board[64], int stm)
{
    struct game g;
    int king_sq[2];

    memset(&g, 0, sizeof g);
    memcpy(g.board, board, 64);
    find_kings(board, king_sq);
    g.king_sq[COLOR_WHITE] = (uint8_t)king_sq[COLOR_WHITE];
    g.king_sq[COLOR_BLACK] = (uint8_t)king_sq[COLOR_BLACK];
    g.turn = (stm == 0) ? COLOR_WHITE : COLOR_BLACK;

    return nnue_evaluate(&g);
}
