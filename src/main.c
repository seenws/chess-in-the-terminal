#include <stdio.h>
#include <string.h>

#include "game.h"

static void
print_usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [-w] [-b]\n"
            "  -w, --ai-white    engine plays white\n"
            "  -b, --ai-black    engine plays black\n"
            "default: human vs human\n",
            prog);
}

int
main(int argc, char **argv)
{
    struct game g;

    game_init(&g);

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--ai-white") == 0)
            g.ai_white = 1;
        else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--ai-black") == 0)
            g.ai_black = 1;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    while (game_step(&g))
        ;

    return 0;
}
