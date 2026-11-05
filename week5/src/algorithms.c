/* Small test program for algorithms.c
 * Prints the step table for test case 1 and the hits/faults of all totals. */

#include <stdio.h>
#include <stdlib.h>
#include "common.h"

typedef void (*algo_fn)(const int *, int, int, step_t *);

/* Print the step table of one algorithm. */
static void print_table(const char *name, algo_fn fn,
                        const int *pages, int n, int frames)
{
    step_t steps[MAX_PAGES];
    fn(pages, n, frames, steps);

    printf("%s\n", name);
    for (int i = 0; i < n; i++) {
        printf("%d | ", steps[i].page);
        for (int f = 0; f < frames; f++) {
            if (steps[i].frames[f] == EMPTY) printf("- ");
            else                              printf("%d ", steps[i].frames[f]);
        }
        printf("| %-5s | ", steps[i].hit ? "Hit" : "Fault");
        if (steps[i].replaced == EMPTY) printf("-\n");
        else                            printf("%d\n", steps[i].replaced);
    }
}

/* Count hits for one algorithm run. */
static int count_hits(algo_fn fn, const int *pages, int n, int frames)
{
    step_t steps[MAX_PAGES];
    int hits = 0;
    fn(pages, n, frames, steps);
    for (int i = 0; i < n; i++)
        hits += steps[i].hit;
    return hits;
}

/* Run all three algorithms and compare with the expected hits. */
static int check(const char *label, const int *pages, int n, int frames,
                 const int exp_hits[3])
{
    algo_fn fns[3] = { fifo, lru, optimal };
    int ok = 1;

    printf("%s (%d frames)\n", label, frames);
    for (int a = 0; a < 3; a++) {
        int h = count_hits(fns[a], pages, n, frames);
        int pass = (h == exp_hits[a]);
        printf("  %-8s hits/faults = %d / %d   expected %d / %d   %s\n",
               ALGO_NAMES[a], h, n - h, exp_hits[a], n - exp_hits[a],
               pass ? "OK" : "WRONG");
        if (!pass) ok = 0;
    }
    return ok;
}

int main(void)
{
    int p1[] = {7, 0, 1, 2, 0, 3, 0, 4, 2, 3};
    int p2[] = {7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2, 1, 2, 0, 1, 7, 0, 1};
    int p3[] = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
    int e1[3] = {1, 2, 4};
    int e2[3] = {5, 8, 11};
    int e3a[3] = {3, 2, 5};
    int e3b[3] = {2, 4, 6};
    int all_ok = 1;

    /* Step tables for the first example. */
    print_table("FIFO",    fifo,    p1, 10, 3);
    print_table("LRU",     lru,     p1, 10, 3);
    print_table("Optimal", optimal, p1, 10, 3);
    printf("\n");

    /* Totals. */
    all_ok &= check("Test 1", p1, 10, 3, e1);
    all_ok &= check("Test 2", p2, 20, 3, e2);
    all_ok &= check("Test 3", p3, 12, 3, e3a);
    all_ok &= check("Test 3", p3, 12, 4, e3b);

    printf("\n%s\n", all_ok ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return all_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
