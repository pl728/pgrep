# pgrep (parallel grep)
## Part 1 - Single Threaded
### Preliminary Design
I am tasked with building pgrep, a command line program that builds on my pthreads-kit consisting of a bounded queue and a thread pool. The program will be run as `pgrep <pattern> <filename>`. My program will use the mmap syscall to map the file into read-only memory and search for `<pattern>`, outputting all matching lines alongside their line numbers. The program must be able to handle files larger than RAM, using chunked mmap or streaming - a design choice not yet made. 

The project will be structured as a main() CLI application with a library core. The library will be reused in part B, where I will parallelize the parallel grep using the core grep function `grep_search()`.

In the command line tool, I will define `main()` that parses args, mmaps file to read file contents in memory, then calls the grep library function `grep_search()` to get the results, and prints the results. 

This seems to cover most of the requirements. However, we have to consider how to handle files larger than RAM. To solve this, we explore chunked mmap. Instead of mapping the entire file into memory, we could define a maximum block size, and then process block by block so that the entire file never exceeds total memory capacity. However, the tricky part here revolves around separating line boundaries properly. 

This problem is trickier to solve than it seems. The specification asks for line boundaries and mmap chunking. If we encounter a file that can't fit in memory, and is one gigantic line, this actually completely breaks our assumptions for parallelization in part B. Thus, to implement this, we have to make some strong assumptions that, if broken, cause program execution to raise an error. 

We define the `MAX_LINE_LENGTH` as 100MiB. If a line is larger than 100MiB, the program stops execution and raises and error. 

Using this, we can "safely" implement our mmap chunking strategy. We define `CHUNK_SIZE` as 100MiB, splitting the file. We find the last instance of `\n`. If it doesn't exist, then the line is larger than 100MiB, and thus raises Error. Otherwise, process the first line up until the last `\n`. Let x be the "MiB" chunk where we find the last `\n`. Then the next chunk would be from `x + min(EOF, CHUNK_SIZE)`.

So to recap: we have a file. We split into chunks using mmap. We have a function, grep_search, that takes in the file descriptor and the chunk start/end to process. The end should be either the end of the file, or the last `\n` in the chunk. The start should be the beginning of the file or the first character of a new line. In the main() producer, we sequentially find [offset, offset + CHUNK_SIZE - y] with y such that we find the last `\n` and pass it to grep_search to search. At the same time, we also keep a running tab of line numbers. So for some chunk [offset, offset + CHUNK_SIZE - y], we should also pass in the line number of the first line `base_line` into grep_search(). Then, once grep_search finds a line that matches the pattern, it prints (line number, line contents) immediately. REVISION: return match_t* using dynamically growing results array.

### Project Structure
```
pgrep/
├── main.c
├── pgrep.c
└── pgrep.h
```

### API
```c
// pgrep.h
typedef struct {
    size_t line_num;
    const char* line;
    size_t line_len;
} match_t;

match_t* grep_search(const char* buf, size_t buf_len, size_t line_num, char* pattern);
```

For grep_search, we call malloc/realloc for a dynamically growing array. 

```
// main.c - single threaded
1. parse args
2. open file, fstat
3. mmap entire file 
4. pass chunks to grep_search
5. collect results and print
6. munmap
```

This implementation accepts out of order printing of pattern matched results when there are more than 1 worker threads.

### Final Design (revised)
In main, which acts as an orchestrator/coordinator, we take in command line arguments, including pattern and filename. We grab the file descriptor and mmap the entire file, allocates virtual address space without loading yet. This returns a pointer to the file buffer. We determine the length of this current chunk we want to pass into grep_search, until EOF, while processing line numbers to pass in the starting line number of the chunk. The chunk always ends on the last \n of the chunk (this program assumes lines don't exceed the CHUNK_SIZE). For each chunk, process line by line. If line matches pattern, add to the match_t array. Finally, return pointer to match_t array. After each chunk, print all matches to stdout and free the matches array. `munmap()` the buffer and close the file. 

## Part 2 - Multithreading using Thread Pool 
Using the thread pool that we implemented in `/pthreads-kit`, we can easily turn our part 1 design into a multithreaded application. Recall that our pthreads-kit implemented a blocking queue and a thread pool with some number of worker_threads that automatically pull tasks from the queue and execute their enclosed function pointers. In the last section, we implemented the program in a single-threaded manner, with chunks ending on `\n` breaks being passed sequentially into our grep_search function. To make this multithreaded, we will instantiate a Pool struct and pass tasks into the pool queue instead - the Pool will handle the rest. 

### Timing Test (single vs parallel)
`st_main.c` is the single-threaded version. The test file `/tmp/big_sparse.txt` contains 1,500,000 lines where only 25 lines include the keyword `special_keyword`. The rest are filler lines.

Create the file:
```
awk 'BEGIN{for(i=1;i<=1500000;i++){if(i%60000==0 && i/60000<=25){printf("special_keyword line %d\n",i)} else {printf("random line %d\n",i)}}}' > /tmp/big_sparse.txt
```

Build single-threaded and parallel binaries:
```
gcc -Wall -Wextra -O2 -pthread -I. st_main.c pgrep.c -o pgrep_st
gcc -Wall -Wextra -O2 -pthread -I. main.c pgrep.c threads/blocking_queue.c threads/thread_pool.c -o pgrep
```

Run the timing test:
```
time ./pgrep_st special_keyword /tmp/big_sparse.txt > /dev/null
time ./pgrep special_keyword /tmp/big_sparse.txt > /dev/null
```

Sample results on this machine:
```
real    0m0.074s
user    0m0.062s
sys     0m0.010s

real    0m0.040s
user    0m0.043s
sys     0m0.008s
```
