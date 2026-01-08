#include "pgrep.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>

#define CHUNK_SIZE (1024 * 1024 * 100)

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <pattern> <filename>\n", argv[0]);
        return 1;
    }

    const char *pattern = argv[1];
    const char *filename = argv[2];

    int fd = open(filename, O_RDONLY);
    if (fd == -1)
    {
        perror("open");
        return 1;
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1)
    {
        perror("fstat");
        close(fd);
        return 1;
    }

    size_t file_size = sb.st_size;
    if (file_size == 0)
        return 0;

    const char *buf = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (buf == MAP_FAILED)
    {
        perror("mmap");
        close(fd);
        return 1;
    }

    // todo: process chunks and call grep_search
    size_t base_line = 1;
    size_t offset = 0;

    while (offset < file_size)
    {
        size_t end = offset + CHUNK_SIZE;
        if (end > file_size)
            end = file_size;

        if (end < file_size)
        {
            size_t j = end;
            while (j > offset && buf[j - 1] != '\n')
            {
                j--;
            }
            if (j == offset)
            {
                fprintf(stderr, "Error: line exceeds CHUNK_SIZE / MAX_LINE_LENGTH\n");
                return 1;
            }
            end = j;
        }

        size_t chunk_len = end - offset;

        size_t num_matches = 0;
        match_t *matches = grep_search(buf + offset, chunk_len, base_line, pattern, &num_matches);
        if (!matches && num_matches == 0)
        {
            fprintf(stderr, "Error: grep_search failed (OOM?)\n");
            return 1;
        }

        for (size_t k = 0; k < num_matches; k++)
        {
            printf("%zu:%.*s\n",
                   matches[k].line_num,
                   (int)matches[k].line_len,
                   matches[k].line);
        }
        free(matches);

        size_t newlines = 0;
        for (size_t t = 0; t < chunk_len; t++)
        {
            if (buf[offset + t] == '\n')
                newlines++;
        }
        base_line += newlines;

        offset = end;
    }

    munmap((void *)buf, file_size);
    close(fd);

    return 0;
}