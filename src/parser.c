#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "parser.h"

// Takes a single line of input from stdin and writes it to a buffer.
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
