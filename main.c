#include "pgrep.h"
#include "threads/thread_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>

#define CHUNK_SIZE (1024 * 1024 * 100)
#define POOL_SIZE 2
#define POOL_QUEUE_CAPACITY 2

typedef struct
{
    const char *chunk;
    size_t chunk_len;
    size_t base_line;
    const char *pattern;

    size_t num_matches;
    match_t *matches;
} GrepJob;

void grep_task_run(void *arg)
{
    GrepJob *job = (GrepJob *)arg;
    job->num_matches = 0;
    job->matches = grep_search(job->chunk, job->chunk_len, job->base_line, job->pattern, &job->num_matches);
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <pattern> <filename>\n", argv[0]);
        return 1;
    }

    const char *pattern = argv[1];
    const char *filename = argv[2];

    int fd = -1;
    const char *buf = MAP_FAILED;
    Pool *pool = NULL;
    GrepJob **jobs = NULL;
    size_t cap = 0, njobs = 0;
    size_t file_size = 0;
    int exit_code = 0;
    int had_failure = 0;

    fd = open(filename, O_RDONLY);
    if (fd == -1)
    {
        perror("open");
        return 1;
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1)
    {
        perror("fstat");
        exit_code = 1;
        goto cleanup;
    }

    file_size = sb.st_size;
    if (file_size == 0)
        goto cleanup;

    buf = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (buf == MAP_FAILED)
    {
        perror("mmap");
        exit_code = 1;
        goto cleanup;
    }

    cap = 16;
    jobs = malloc(cap * sizeof(*jobs));
    if (!jobs) {
        fprintf(stderr, "Error: Failed to malloc grepjobs\n");
        exit_code = 1;
        goto cleanup;
    }
    
    size_t base_line = 1;
    size_t offset = 0;
    pool = Pool_new(POOL_SIZE, POOL_QUEUE_CAPACITY, POOL_SHUTDOWN_GRACEFUL);
    if(!pool) {
        fprintf(stderr, "Error: Failed to initialize thread pool\n");
        exit_code = 1;
        goto cleanup;
    }

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
                exit_code = 1;
                goto cleanup;
            }
            end = j;
        }

        size_t chunk_len = end - offset;

        GrepJob *job = malloc(sizeof *job);
        if (!job)
        {
            fprintf(stderr, "Error: failed to malloc job\n");
            exit_code = 1;
            goto cleanup;
        }
        if (njobs == cap)
        {
            size_t new_cap = cap * 2;
            GrepJob **new_jobs = realloc(jobs, new_cap * sizeof(*jobs));
            if (!new_jobs)
            {
                free(job);
                fprintf(stderr, "Error: failed to reallocate grepjobs\n");
                exit_code = 1;
                goto cleanup;
            }
            jobs = new_jobs;
            cap = new_cap;
        }
        jobs[njobs++] = job;

        job->chunk = buf + offset;
        job->chunk_len = chunk_len;
        job->base_line = base_line;
        job->pattern = pattern;
        job->num_matches = 0;
        job->matches = NULL;

        if (Pool_submit(pool, grep_task_run, job) != 0)
        {
            fprintf(stderr, "Error: Pool submission failed\n");
            exit_code = 1;
            goto cleanup;
        }

        size_t newlines = 0;
        for (size_t t = 0; t < chunk_len; t++)
        {
            if (buf[offset + t] == '\n')
                newlines++;
        }
        base_line += newlines;

        offset = end;
    }

    Pool_shutdown(pool);
    Pool_free(pool);
    pool = NULL;

    for (size_t i = 0; i < njobs; i++)
    {
        if(!jobs[i]->matches) {
            had_failure = 1;
            continue;
        }
        for (size_t k = 0; k < jobs[i]->num_matches; k++)
        {
            printf("%zu:%.*s\n",
                  jobs[i]->matches[k].line_num,
                  (int)jobs[i]->matches[k].line_len,
                  jobs[i]->matches[k].line);
        }
    }
    if(had_failure) {
        fprintf(stderr, "Warning: at least one chunk failed (OOM?)\n");
    }

cleanup:
    if (pool) {
        Pool_shutdown(pool);
        Pool_free(pool);
    }
    if (jobs) {
        for (size_t i = 0; i < njobs; i++) {
            if (!jobs[i])
                continue;
            free(jobs[i]->matches);
            free(jobs[i]);
        }
        free(jobs);
    }
    if (buf != MAP_FAILED && file_size > 0)
        munmap((void *)buf, file_size);
    if (fd != -1)
        close(fd);

    return exit_code;
}
