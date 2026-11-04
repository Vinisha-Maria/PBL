/* ipc.c - the IPC part: pipes and worker processes (owner: Vinisha)
 *
 * STUB: returns an error. Vinisha replaces it.
 *
 * Plan: for every algorithm in mask (order FIFO, LRU, Optimal) create a pipe and
 * fork(). The child runs the algorithm (fifo/lru/optimal from algorithms.c), writes
 * its whole result_t into the pipe and calls _exit(0). The parent reads exactly
 * sizeof(result_t) bytes from each pipe, calls waitpid() for each child and stores
 * the results in order. On any error: close everything, wait for the children,
 * write a message into err and return -1.
 */
#include <stdio.h>
#include "common.h"

int ipc_run_algorithms(const int *pages, int n, int frames, int mask,
                       result_t *results, int *count, char *err, size_t errsize)
{
    (void)pages; (void)n; (void)frames; (void)mask; (void)results;
    if (count) *count = 0;
    snprintf(err, errsize, "not implemented");
    return -1;
}

