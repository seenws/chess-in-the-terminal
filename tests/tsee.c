#include <stdio.h>
#include <string.h>

#include "attacks.h"
#include "board.h"
#include "game.h"
#include "movegen.h"
#include "parser.h"
#include "search.h"

struct see_case {
    const char *fen;
    const char *uci;   /* "e2e4", "e7e8q", etc. */
    int         expect;
    const char *desc;
};

/* Build a move struct from a UCI string against the position in g. We
   derive flags by inspecting board[] + ep_target so the test driver
   doesn't depend on the engine's pseudolegal list.  */
static int
parse_uci_move(const struct game *g, const char *uci, struct move *out)
{
    if (!uci || strlen(uci) < 4) return -1;

    int ff = uci[0] - 'a';
    int fr = uci[1] - '1';
    int tf = uci[2] - 'a';
    int tr = uci[3] - '1';

    if (ff < 0 || ff > 7 || fr < 0 || fr > 7 ||
        tf < 0 || tf > 7 || tr < 0 || tr > 7) return -1;

    int from = make_sq(fr, ff);
    int to   = make_sq(tr, tf);

    out->from  = (uint8_t)from;
    out->to    = (uint8_t)to;
    out->flags = MOVE_QUIET;
    out->promo = PIECE_NONE;

    uint8_t mover = g->board[from];
    if (is_empty(mover)) return -1;

    if (!is_empty(g->board[to]))
        out->flags |= MOVE_CAPTURE;

    if (piece_type(mover) == PIECE_PAWN && to == g->ep_target)
        out->flags |= MOVE_CAPTURE | MOVE_ENP;

    if (uci[4]) {
        out->flags |= MOVE_PROMO;
        switch (uci[4]) {
            case 'q': out->promo = PIECE_QUEEN;  break;
            case 'r': out->promo = PIECE_ROOK;   break;
            case 'b': out->promo = PIECE_BISHOP; break;
            case 'n': out->promo = PIECE_KNIGHT; break;
            default: return -1;
        }
    }

    return 0;
}

/* Test cases. Expected values come from hand-evaluation of the swap-off.
   Keep cases simple and unambiguous; complex positions where authorities
   disagree only add noise.  */
static const struct see_case cases[] = {
    /* Plain undefended pawn capture: gains a pawn outright.  */
    { "4k3/8/4p3/3P4/8/8/8/4K3 w - - 0 1", "d5e6",  100,
      "PxP undefended" },

    /* Pawn x pawn defended by pawn → equal trade.  */
    { "4k3/3p4/4p3/3P4/8/8/8/4K3 w - - 0 1", "d5e6",   0,
      "PxP defended by P" },

    /* Knight captures defended pawn: lose 320, win 100 = -220.  */
    { "4k3/3p4/4p3/8/3N4/8/8/4K3 w - - 0 1", "d4e6", -220,
      "NxP defended by P" },

    /* NxP defended by N. Use a d8 knight (which actually defends e6 —
       d5 doesn't). Sequence: NxP +100, nxN -320 → -220.  */
    { "3nk3/8/4p3/8/3N4/8/8/4K3 w - - 0 1", "d4e6", -220,
      "NxP defended by N (d8 knight)" },

    /* Rook captures defended pawn: -400 (R for P, then black recaps).  */
    { "4k3/3p4/4p3/8/8/8/4R3/4K3 w - - 0 1", "e2e6", -400,
      "RxP defended by P" },

    /* Q captures pawn defended by king (king can capture freely).  */
    { "4k3/8/4p3/8/8/8/4Q3/4K3 w - - 0 1", "e2e6", 100,
      "QxP undefended (e6 pawn is the only piece in line)" },

    /* Q captures pawn defended by king with the king actually reaching:  */
    { "8/4k3/4p3/8/8/8/4Q3/4K3 w - - 0 1", "e2e6", -800,
      "QxP defended by king → lose queen" },

    /* X-ray: W rook captures pawn defended by pawn, second W rook x-rays
       behind. RxP (+100), pxR (-400), Rxp (-300). W=-300.  */
    { "4k3/3p4/4p3/8/8/8/4R3/4R1K1 w - - 0 1", "e2e6", -300,
      "RxP defended by p, R x-rays behind R" },

    /* X-ray reveal via own pawn: W Bishop a1 attacks c3 through the W
       pawn on b2 once the pawn vacates. PxN (+320), pxP (-100, W net
       +220), Bxp (+100, W net +320). SEE = +320.  */
    { "4k3/8/8/8/3p4/2n5/1P6/B3K3 w - - 0 1", "b2c3", 320,
      "PxN with bishop x-ray support via own pawn" },

    /* BxP defended by rook with W rook x-ray behind defender. Black
       chooses NOT to recapture because doing so loses MORE material
       through the x-ray chain. SEE = +100.  */
    { "4k3/8/8/4p3/8/4r3/4R3/B3K3 w - - 0 1", "a1e5", 100,
      "BxP, defender's recap is dissuaded by x-ray rook behind" },

    /* King as 2nd attacker with x-ray defender. QxP +100, nxQ -900
       (W net -800). King-x-ray fix correctly suppresses the K recap.
       Even without the fix, negamax happens to return the same -800
       because the king's enormous value dominates the swap; the fix is
       still correct in principle but doesn't perturb the score here.  */
    { "4k3/4n3/8/3p4/2K5/1b6/8/3Q4 w - - 0 1", "d1d5", -800,
      "King as 2nd attacker, bishop x-rays through king square" },

    /* K captures pawn but is then attacked by an enemy rook behind the
       pawn. The move is pseudo-legal (movegen accepts it) but illegal
       (puts king in check); see() reports the "loses the king" value
       of -19900. This is fine for ordering — the negative score sinks
       it below quiets and the legality filter rejects it during search.  */
    { "4r3/8/8/4p3/4K3/8/8/4k3 w - - 0 1", "e4e5", -19900,
      "K captures pawn into check (pseudo-legal); loses king" },

    /* En passant: undefended.  */
    { "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", "e5d6", 100,
      "en passant undefended" },

    /* Promotion-capture, undefended: PxR + promo to Q
       gain = 500 (rook) + (900 - 100) = 1300.  */
    { "3r4/4P3/8/8/8/8/8/4K1k1 w - - 0 1", "e7d8q", 1300,
      "PxR with promotion to Q, undefended" },

    /* Quiet move: SEE returns 0.  */
    { "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1", "e2e4", 0,
      "quiet pawn push (non-capture)" },
};

static const char *
square_name(int sq, char buf[3])
{
    buf[0] = 'a' + file_of(sq);
    buf[1] = '1' + rank_of(sq);
    buf[2] = '\0';
    return buf;
}

int
main(void)
{
    attacks_init();

    int pass = 0, fail = 0;
    size_t n = sizeof(cases) / sizeof(cases[0]);

    for (size_t i = 0; i < n; ++i) {
        const struct see_case *c = &cases[i];
        struct game g;
        if (parse_fen(&g, c->fen) != 0) {
            printf("FEN PARSE FAIL  [%s]\n", c->desc);
            fail++;
            continue;
        }

        struct move m;
        if (parse_uci_move(&g, c->uci, &m) != 0) {
            printf("MOVE PARSE FAIL [%s] (%s)\n", c->desc, c->uci);
            fail++;
            continue;
        }

        int got = see(g.board, &m);
        const char *tag = (got == c->expect) ? "PASS" : "FAIL";
        if (got == c->expect) pass++; else fail++;

        char fb[3], tb[3];
        printf("%s  %-44s  %s%s  expected %5d  got %5d\n",
               tag, c->desc,
               square_name(m.from, fb), square_name(m.to, tb),
               c->expect, got);
    }

    printf("\n%d pass, %d fail\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
