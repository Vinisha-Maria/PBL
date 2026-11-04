#ifndef COMMON_H
#define COMMON_H
#include <stddef.h>

#define MAX_PAGES        100
#define MAX_PAGE_NUMBER  999
#define MAX_FRAMES       10
#define EMPTY            (-1)      /* empty frame, or nothing replaced */
#define JSON_BUF_SIZE    65536

typedef enum { ALGO_FIFO = 0, ALGO_LRU = 1, ALGO_OPTIMAL = 2 } algo_id_t;
#define NUM_ALGOS 3
static const char *const ALGO_NAMES[NUM_ALGOS] = { "FIFO", "LRU", "Optimal" };
#define ALGO_BIT(a) (1 << (a))     /* algorithm mask: FIFO=1, LRU=2, Optimal=4, all=7 */

typedef struct {
    int page;                      /* requested page */
    int frames[MAX_FRAMES];        /* frame contents AFTER this request; EMPTY = empty frame.
                                      Only the first 'frames' entries are used, the rest are EMPTY */
    int hit;                       /* 1 = hit, 0 = fault */
    int replaced;                  /* page removed on a fault when all frames were full, else EMPTY */
} step_t;

typedef struct {
    algo_id_t algo;
    int nsteps;                    /* number of steps = number of page requests */
    int hits, faults;              /* filled by summarize() */
    double hit_ratio, fault_ratio; /* percent, rounded to 2 decimals, filled by summarize() */
    step_t steps[MAX_PAGES];
} result_t;

typedef struct {
    int best[NUM_ALGOS];           /* indexes (into the results array) of the algorithms with the fewest faults */
    int nbest;
    int min_faults;
} comparison_t;

/* algorithms.c (Rajath): write one step per page request into steps[0..n-1] */
void fifo   (const int *pages, int n, int frames, step_t *steps);
void lru    (const int *pages, int n, int frames, step_t *steps);
void optimal(const int *pages, int n, int frames, step_t *steps);

/* validation.c (Student 3): return 0 if ok, -1 on error with the message in err */
int parse_reference_string(const char *text, int *pages, int *n, char *err, size_t errsize);
int validate_frames(const char *text, int *frames, char *err, size_t errsize);
int validate_algorithm(const char *text, int *mask, char *err, size_t errsize);

/* stats.c (Student 3) */
void summarize(result_t *r);                                   /* hits, faults and ratios from r->steps */
void compare(const result_t *results, int count, comparison_t *c);

/* ipc.c (Vinisha): the IPC part. Uses pipes and fork. */
int ipc_run_algorithms(const int *pages, int n, int frames, int mask,
                       result_t *results, int *count, char *err, size_t errsize);

/* json.c (Rajath): return the length written, or -1 if buf is too small */
int build_success_json(char *buf, size_t size, const int *pages, int n, int frames,
                       const result_t *results, int count, const comparison_t *cmp);
int build_error_json(char *buf, size_t size, const char *message);

#endif

