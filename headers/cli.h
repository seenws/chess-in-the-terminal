#ifndef CITT_HEADERS_CLI_H_
#define CITT_HEADERS_CLI_H_

#include "game.h"

/* Session-level CLI/UI configuration; held outside `struct game` so a
   position snapshot stays self-contained.  */
struct ui_config {
    uint8_t ai_white;
    uint8_t ai_black;
};

/* Runs one turn of interactive terminal play on `g`: prints the board,
   reports a terminal result, then either has the engine move or prompts
   the human for SAN input. Returns 0 when the game is over or input ends,
   1 when play should continue.  */
int game_step(struct game *g, const struct ui_config *cfg);

#endif /* CITT_HEADERS_CLI_H_ */
