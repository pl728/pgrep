#include "pgrep.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

bool check_line(const char *line, size_t line_len, const char *pattern, size_t pattern_len);

match_t* grep_search(
    const char *buf, 
    size_t buf_len, 
    size_t line_num, 
    const char *pattern,
    size_t *num_matches
) {
    size_t capacity = 16;
    match_t *matches = malloc(capacity * sizeof(match_t));
    if(!matches) {
        *num_matches = 0;
        return NULL;
    }
    size_t pattern_length = strlen(pattern);

    *num_matches = 0;

    const char *curr_line = buf;
    for(size_t i = 0; i < buf_len; i++) {
        if(buf[i] == '\n') {
            size_t length = &buf[i] - curr_line;

            if(length > 0 && curr_line[length - 1] == '\r') {
                length--;
            }
            
            if(check_line(curr_line, length, pattern, pattern_length)) {
                if(*num_matches >= capacity) {
                    // realloc
                    capacity *= 2;
                    match_t *new_matches = realloc(matches, capacity * sizeof(match_t));
                    if(!new_matches) {
                        free(matches);
                        *num_matches = 0;
                        return NULL;
                    }

                    matches = new_matches;
                }

                matches[*num_matches].line_num = line_num;
                matches[*num_matches].line = curr_line;
                matches[*num_matches].line_len = length;
                (*num_matches)++;
            }

            curr_line = &buf[i+1];
            line_num++;
        }
    }

    if(curr_line < buf + buf_len) {
        size_t length = (buf + buf_len) - curr_line;
        if (length > 0 && curr_line[length - 1] == '\r') {
            length--;
        }

        if(check_line(curr_line, length, pattern, pattern_length)) {
            if(*num_matches >= capacity) {
                // realloc
                capacity *= 2;
                match_t *new_matches = realloc(matches, capacity * sizeof(match_t));
                if(!new_matches) {
                    free(matches);
                    *num_matches = 0;
                    return NULL;
                }

                matches = new_matches;
            }

            matches[*num_matches].line_num = line_num;
            matches[*num_matches].line = curr_line;
            matches[*num_matches].line_len = length;
            (*num_matches)++;
        }
    }

    return matches;
}

bool check_line(const char *line, size_t line_len, const char *pattern, size_t pattern_len) {
    if(pattern_len > line_len) {
        return false;
    }

    for(size_t i = 0; i <= line_len - pattern_len; i++) {
        if(memcmp(pattern, &line[i], pattern_len) == 0) {
            return true;
        }
    }

    return false;
}
