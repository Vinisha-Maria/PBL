
/*
 * stats.c - counts hits and faults and compares the algorithms.
 */

#include "common.h"

/* Round a non-negative number to 2 decimals (no math library needed). */
static double round2(double x)
{
    /* x * 100 + 0.5 is never negative here, so the cast works like floor. */
    return (double)(long)(x * 100.0 + 0.5) / 100.0;
}

/* Fill hits, faults, hit_ratio and fault_ratio from r->steps. */
void summarize(result_t *r)
{
    int n;
    int i;
    int hits = 0;

    if (r == NULL) {
        return;
    }

    /* Never read past the steps array. */
    n = r->nsteps;
    if (n < 0) {
        n = 0;
    }
    if (n > MAX_PAGES) {
        n = MAX_PAGES;
    }

    for (i = 0; i < n; i++) {
        if (r->steps[i].hit == 1) {
            hits++;
        }
    }

    r->nsteps = n;                      /* keep it inside the array for the others */
    r->hits = hits;
    r->faults = n - hits;

    if (n == 0) {
        r->hit_ratio = 0.0;
        r->fault_ratio = 0.0;
    } else {
        r->hit_ratio = round2(hits * 100.0 / n);
        r->fault_ratio = round2(r->faults * 100.0 / n);
    }
}

/* Find the algorithms with the fewest faults. */
void compare(const result_t *results, int count, comparison_t *c)
{
    int i;

    if (c == NULL) {
        return;
    }

    c->nbest = 0;
    c->min_faults = 0;
    for (i = 0; i < NUM_ALGOS; i++) {
        c->best[i] = 0;
    }

    if (results == NULL || count <= 0) {
        return;
    }
    if (count > NUM_ALGOS) {
        count = NUM_ALGOS;              /* best[] has room for NUM_ALGOS */
    }

    /* First pass: the smallest number of faults. */
    c->min_faults = results[0].faults;
    for (i = 1; i < count; i++) {
        if (results[i].faults < c->min_faults) {
            c->min_faults = results[i].faults;
        }
    }

    /* Second pass: every entry with that number, in order. */
    for (i = 0; i < count; i++) {
        if (results[i].faults == c->min_faults) {
            c->best[c->nbest] = i;
            c->nbest++;
        }
    }
}
