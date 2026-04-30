#include <stdio.h>
#include <stddef.h>
#include <string.h>

// Takes a single line of input from stdin and writes it to a buffer.
size_t
get_line(char *buffer, size_t bufsz)
{
    if (bufsz == 0)
        return 0;

    char ch;
    size_t buflen = 0;

    while (buflen + 1 < bufsz) {
        if (fread(&ch, 1, 1, stdin) == 0 || ch == '\n')
            break;
        buffer[buflen++] = ch;
    }

    buffer[buflen] = '\0';

    if (ch != '\n' && buflen + 1 == bufsz) {
        while (fread(&ch, 1, 1, stdin) == 1 && ch != '\n')
            ;
    }

    return buflen;
}
