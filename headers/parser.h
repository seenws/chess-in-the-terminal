#ifndef CITT_HEADERS_PARSER_H_
#define CITT_HEADERS_PARSER_H_

#include <stddef.h>
#include <stdio.h>

size_t get_line(char *buffer, size_t bufsz, FILE *file);
int parse_san(char const *buffer, size_t bufsz);


#endif
