// Theoretical maximum possible legal moves in any game state
// https://www.chessprogramming.com/Chess#Chess_Maxima
#define MAX_MOVES 218

enum move_flag {
    MOVE_QUIET      = 0,        // plain move, no capture
    MOVE_CAPTURE    = 1 << 0,   // destination has a piece
    MOVE_ENP        = 1 << 1,   // en passant capture
    MOVE_CASTLE_K   = 1 << 2,   // king-side castle
    MOVE_CASTLE_Q   = 1 << 3,   // queen-side castle
    MOVE_PROMO      = 1 << 4,   // pawn promotion
};

struct move {
    uint8_t from;
    uint8_t to;

    enum move_flag flags;
    enum type promo; // only relevant if MOVE_PROMO is set, felt convenient to reuse the type enum
};

struct move_list {
    struct move moves[MAX_MOVES];
    size_t count;
};
