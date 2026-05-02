#include "game.h"

int
main(void)
{
    struct game g;

    game_init(&g);
    while (game_step(&g))
        ;

    return 0;
}
