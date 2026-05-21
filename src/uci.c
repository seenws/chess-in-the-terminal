/* uci.c -- UCI protocol implementation. Built as a separate `citt-uci`
   binary; the main `citt` binary keeps its SAN game loop unchanged.
   Single-threaded: `stop` is honored at the next abort poll inside the
   search; for fixed-time / fixed-depth matches (the common case under
   cutechess-cli) this is sufficient. The search deadline does all the
   real work for clock-based time controls.  */

#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "game.h"
#include "movegen.h"
#include "parser.h"
#include "search.h"
#include "uci.h"

#define UCI_LINE_MAX  8192
#define ENGINE_NAME   "CITT"
#define ENGINE_AUTHOR "Sinan Olsson-Pasic"
#define UCI_HASH_DEFAULT_MB 16
#define UCI_HASH_MIN_MB     1
#define UCI_HASH_MAX_MB     4096

static struct game g_pos;
static size_t      g_hash_mb = UCI_HASH_DEFAULT_MB;

static char *
skip_ws(char *p)
{
    while (*p == ' ' || *p == '\t') ++p;
    return p;
}

static void
rstrip(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'
                  || s[n - 1] == ' '  || s[n - 1] == '\t'))
        s[--n] = '\0';
}

/* Case-insensitive token equality (for option names like "Hash"/"hash").  */
static int
ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        ++a; ++b;
    }
    return *a == '\0' && *b == '\0';
}

/* Apply a UCI-format move ("e2e4", "a7a8q") to `g`. Returns 0 on success,
   -1 on malformed string or no matching legal move.  */
static int
apply_uci_move(struct game *g, const char *uci)
{
    if (strlen(uci) < 4) return -1;

    int ff = uci[0] - 'a', fr = uci[1] - '1';
    int tf = uci[2] - 'a', tr = uci[3] - '1';
    if (ff < 0 || ff > 7 || fr < 0 || fr > 7
     || tf < 0 || tf > 7 || tr < 0 || tr > 7)
        return -1;

    uint8_t from = (uint8_t)((fr << 4) | ff);
    uint8_t to   = (uint8_t)((tr << 4) | tf);

    enum piece_type promo = PIECE_NONE;
    if (uci[4] != '\0') {
        switch (uci[4]) {
            case 'q': promo = PIECE_QUEEN;  break;
            case 'r': promo = PIECE_ROOK;   break;
            case 'b': promo = PIECE_BISHOP; break;
            case 'n': promo = PIECE_KNIGHT; break;
            default:  return -1;
        }
    }

    struct move_list ml = { 0 };
    append_legal_moves(g, &ml);

    for (size_t i = 0; i < ml.count; ++i) {
        const struct move *m = &ml.moves[i];
        if (m->from != from || m->to != to) continue;
        if (promo != PIECE_NONE) {
            if (!(m->flags & MOVE_PROMO) || m->promo != promo) continue;
        } else if (m->flags & MOVE_PROMO) {
            continue;
        }
        struct undo_state _u;
        make_move(g, m, &_u);
        return 0;
    }
    return -1;
}

/* "position [startpos | fen <6-field FEN>] [moves m1 m2 ...]"  */
static void
cmd_position(char *args)
{
    char *p = skip_ws(args);

    if (strncmp(p, "startpos", 8) == 0
        && (p[8] == '\0' || p[8] == ' ' || p[8] == '\t')) {
        game_init(&g_pos);
        p = skip_ws(p + 8);
    } else if (strncmp(p, "fen", 3) == 0
               && (p[3] == ' ' || p[3] == '\t')) {
        p = skip_ws(p + 3);
        /* FEN ends at " moves" or end-of-string. parse_fen reads up to 6
           space-separated fields; cap with a NUL so it can't run past.  */
        char *moves_kw = strstr(p, " moves");
        char saved = 0;
        if (moves_kw) { saved = *moves_kw; *moves_kw = '\0'; }
        if (parse_fen(&g_pos, p) != 0) {
            if (moves_kw) *moves_kw = saved;
            return;
        }
        if (moves_kw) {
            *moves_kw = saved;
            p = skip_ws(moves_kw + 1);
        } else {
            p += strlen(p);
        }
    } else {
        return;
    }

    if (strncmp(p, "moves", 5) == 0
        && (p[5] == '\0' || p[5] == ' ' || p[5] == '\t')) {
        p = skip_ws(p + 5);
        while (*p) {
            char *end = p;
            while (*end && *end != ' ' && *end != '\t') ++end;
            char saved = *end;
            *end = '\0';
            if (apply_uci_move(&g_pos, p) != 0) {
                /* Silently stop on the first unparseable move; the GUI
                   sees the resulting bestmove from whatever we have.  */
                *end = saved;
                return;
            }
            *end = saved;
            p = skip_ws(end);
        }
    }
}

/* Score: report mate distance separately so GUIs render "M5", "-M3" etc.  */
static void
print_score(int score)
{
    if (score >=  SEARCH_MATE - 1000) {
        int plies_to_mate = SEARCH_MATE - score;
        printf("mate %d", (plies_to_mate + 1) / 2);
    } else if (score <= -SEARCH_MATE + 1000) {
        int plies_to_mate = SEARCH_MATE + score;
        printf("mate -%d", (plies_to_mate + 1) / 2);
    } else {
        printf("cp %d", score);
    }
}

static void
info_print(int depth, int score, unsigned long long nodes,
           unsigned long long time_ms,
           const struct move *pv, int pv_len, void *ctx)
{
    (void)ctx;

    unsigned long long nps = time_ms > 0 ? (nodes * 1000ULL / time_ms) : nodes;

    printf("info depth %d score ", depth);
    print_score(score);
    printf(" nodes %llu nps %llu time %llu", nodes, nps, time_ms);

    if (pv_len > 0) {
        printf(" pv");
        for (int i = 0; i < pv_len; ++i) {
            char uci[6];
            move_to_uci(&pv[i], uci);
            printf(" %s", uci);
        }
    }
    putchar('\n');
    fflush(stdout);
}

/* "go [wtime X] [btime X] [winc X] [binc X] [movestogo N] [movetime X]
       [depth D] [nodes N] [infinite]"  */
static void
cmd_go(char *args)
{
    struct search_limits lim = { 0 };

    char *p = skip_ws(args);
    while (*p) {
        char *end = p;
        while (*end && *end != ' ' && *end != '\t') ++end;
        size_t tok_len = (size_t)(end - p);

        #define IS(s) (tok_len == sizeof(s) - 1 && memcmp(p, s, sizeof(s) - 1) == 0)

        if (IS("infinite")) {
            lim.infinite = 1;
            p = skip_ws(end);
            continue;
        }

        /* All remaining keywords take a numeric argument.  */
        if (IS("wtime") || IS("btime") || IS("winc") || IS("binc")
         || IS("movetime") || IS("depth") || IS("nodes") || IS("movestogo")) {

            char *vstart = skip_ws(end);
            char *vend   = NULL;
            long  v      = strtol(vstart, &vend, 10);
            if (vend == vstart) { p = skip_ws(end); continue; }

            if      (IS("wtime"))     lim.wtime_ms    = (uint64_t)(v > 0 ? v : 0);
            else if (IS("btime"))     lim.btime_ms    = (uint64_t)(v > 0 ? v : 0);
            else if (IS("winc"))      lim.winc_ms     = (uint64_t)(v > 0 ? v : 0);
            else if (IS("binc"))      lim.binc_ms     = (uint64_t)(v > 0 ? v : 0);
            else if (IS("movetime"))  lim.movetime_ms = (uint64_t)(v > 0 ? v : 0);
            else if (IS("depth"))     lim.max_depth   = (int)(v > 0 ? v : 0);
            else if (IS("nodes"))     lim.node_limit  = (uint64_t)(v > 0 ? v : 0);
            else if (IS("movestogo")) lim.moves_to_go = (int)(v > 0 ? v : 0);

            p = skip_ws(vend);
            continue;
        }

        /* Unknown token: skip past it so we don't loop.  */
        p = skip_ws(end);
        #undef IS
    }

    /* Nothing specified → bounded depth so we don't run forever.  */
    if (lim.max_depth == 0 && !lim.infinite
        && lim.movetime_ms == 0 && lim.node_limit == 0
        && lim.wtime_ms == 0 && lim.btime_ms == 0)
        lim.max_depth = AI_DEFAULT_DEPTH;

    struct move best = { 0 };
    search_run(&g_pos, &lim, &best, info_print, NULL);

    char uci[6];
    if (best.from == 0 && best.to == 0) {
        /* No legal move (terminal position). UCI convention is "0000".  */
        printf("bestmove 0000\n");
    } else {
        move_to_uci(&best, uci);
        printf("bestmove %s\n", uci);
    }
    fflush(stdout);
}

/* "setoption name <NAME> [value <VALUE>]" — only Hash is honored.  */
static void
cmd_setoption(char *args)
{
    char *p = skip_ws(args);
    if (strncmp(p, "name", 4) != 0) return;
    p = skip_ws(p + 4);

    char *value_kw = strstr(p, " value");
    char  name[64];
    size_t nlen = value_kw ? (size_t)(value_kw - p) : strlen(p);
    if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
    memcpy(name, p, nlen);
    name[nlen] = '\0';

    /* Right-trim the name token (the keyword may be followed by spaces).  */
    while (nlen > 0 && (name[nlen - 1] == ' ' || name[nlen - 1] == '\t'))
        name[--nlen] = '\0';

    if (!value_kw) return;
    char *value = skip_ws(value_kw + 6);

    if (ieq(name, "Hash")) {
        long mb = strtol(value, NULL, 10);
        if (mb >= UCI_HASH_MIN_MB && mb <= UCI_HASH_MAX_MB) {
            g_hash_mb = (size_t)mb;
            tt_init(g_hash_mb);
        }
    }
}

static void
cmd_uci(void)
{
    printf("id name %s\n",   ENGINE_NAME);
    printf("id author %s\n", ENGINE_AUTHOR);
    printf("option name Hash type spin default %d min %d max %d\n",
           UCI_HASH_DEFAULT_MB, UCI_HASH_MIN_MB, UCI_HASH_MAX_MB);
    printf("uciok\n");
    fflush(stdout);
}

static void
cmd_ucinewgame(void)
{
    tt_init(g_hash_mb);
    search_reset_state();
    game_init(&g_pos);
}

void
uci_loop(void)
{
    char line[UCI_LINE_MAX];

    /* GUIs want immediate visibility of our replies.  */
    setvbuf(stdout, NULL, _IONBF, 0);

    game_init(&g_pos);
    tt_init(g_hash_mb);

    while (fgets(line, sizeof(line), stdin)) {
        rstrip(line);
        char *p = skip_ws(line);
        if (*p == '\0') continue;

        if (strcmp(p, "uci") == 0)             { cmd_uci(); continue; }
        if (strcmp(p, "isready") == 0)         { printf("readyok\n"); fflush(stdout); continue; }
        if (strcmp(p, "ucinewgame") == 0)      { cmd_ucinewgame(); continue; }
        if (strcmp(p, "quit") == 0)            break;
        if (strcmp(p, "stop") == 0)            { search_signal_stop(); continue; }

        if (strncmp(p, "position", 8) == 0 && (p[8] == ' ' || p[8] == '\t'))
            { cmd_position(p + 8); continue; }
        if (strcmp(p, "position") == 0)        { cmd_position(""); continue; }

        if (strncmp(p, "go", 2) == 0 && (p[2] == '\0' || p[2] == ' ' || p[2] == '\t'))
            { cmd_go(p + 2); continue; }

        if (strncmp(p, "setoption", 9) == 0 && (p[9] == ' ' || p[9] == '\t'))
            { cmd_setoption(p + 9); continue; }

        /* UCI spec: silently ignore unrecognized commands.  */
    }
}

int
main(void)
{
    uci_loop();
    return 0;
}
