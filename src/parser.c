#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "parser.h"

static char const *san_chars = "abcdefghNBRQK12345678+-=#";

// Takes a single line of input from stdin and writes it to a buffer[0..31], where buffer[32] is reserved for the
// null terminator. .
// Discards trailing characters outside the buffer.
size_t
get_line(char *buffer, size_t bufsz, FILE *file)
{
    char ch = 0;
    size_t nread;
    size_t buflen = 0;

    if (file == NULL || feof(file))
        return 0;

    if (ferror(file))
        return 0;

    while (buflen < bufsz) {
        if ((nread = fread(&ch, sizeof ch, 1, file)) == 0 || ch == '\n')
            break;

        buffer[buflen++] = ch;
    }

    if (buflen >= bufsz)
        while ((nread = fread(&ch, sizeof ch, 1, file)) > 0 && ch != '\n')
            ;

    return buflen;
}

// https://en.wikipedia.org/wiki/Algebraic_notation_(chess)
// returns 1 if given input is a valid notation
// returns 0 otherwise
int
parse_san(char const *buffer, size_t bufsz)
{
    for (size_t i = 0; i < bufsz; ++i) {
        const char c = buffer[i];
        const char *ps = memchr(san_chars, c, strlen(san_chars));

        if (!ps)
            return 0;
    }

    return 1;
}
