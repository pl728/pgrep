#ifndef PGREP_H
#define PGREP_H

#include <stddef.h>

typedef struct {
    size_t line_num;
    const char* line;
    size_t line_len;
} match_t;

match_t* grep_search(
    const char* buf, 
    size_t buf_len, 
    size_t line_num, 
    const char* pattern,
    size_t *num_matches
);

#endif