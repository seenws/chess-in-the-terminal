#include <stdio.h>

// Takes a single line of input from stdin and writes it to a buffer.
size_t
get_line(char *buffer, size_t bufsz)
{
    static char ch;
    size_t nread;
    size_t buflen = 0;

    while (buflen < bufsz) {
        if ((nread = fread(&ch, sizeof ch, 1, stdin)) == 0 || ch == '\n')
            break;

        buffer[buflen++] = ch;
    }

    return buflen;
}
