/* main.c -- CLI entry point and game loop driver.  */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "attacks.h"
#include "game.h"

static void
print_usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [-w] [-b] [-s] [-n PLIES]\n"
            "  -v, --version       CITT version and information\n"
            "  -w, --ai-white      engine plays white\n"
            "  -b, --ai-black      engine plays black\n"
            "  -s, --selfplay      engine plays both sides (alias for -w -b)\n"
            "  -n, --max-plies N   stop after N plies (0 = unlimited; debug aid for self-play)\n"
            "default: human vs human\n",
            prog);
}

static void
print_version_info(void)
{
    puts("Chess in the Terminal (CITT) - A shell-interactive chess engine written in C99.");
    puts("Copyright (C) 2026 Sinan Olsson-Pasic");
    puts("Version: 1.1"); /* Officially 1.1 once bitboard implementation is finished */
    puts("License: MIT License");
    puts("This is free software: you are free to change and redistribute it.");
}

int
main(int argc, char **argv)
{
    struct game      g;
    struct ui_config cfg       = { 0 };
    int              max_plies = 0;

    attacks_init();
    game_init(&g);

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--ai-white") == 0)
            cfg.ai_white = 1;

        else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--ai-black") == 0)
            cfg.ai_black = 1;

        else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--selfplay") == 0) {
            cfg.ai_white = 1;
            cfg.ai_black = 1;
        }

        else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--max-plies") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "%s requires an argument\n", argv[i - 1]);
                print_usage(argv[0]);
                return 1;
            }
            max_plies = atoi(argv[i]);
            if (max_plies < 0) {
                fprintf(stderr, "max-plies must be non-negative\n");
                return 1;
            }
        }

        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);

            return 0;
        }

        else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version_info();

            return 0;
        }

        else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            print_usage(argv[0]);

            return 1;
        }
    }

    int plies = 0;
    while (game_step(&g, &cfg)) {
        if (max_plies && ++plies >= max_plies) {
            printf("Ply limit (%d) reached. Stopping.\n", max_plies);
            break;
        }
    }

    return 0;
}
