/* tests/test_analysis.c - plain C tests for src/core/analysis.c
 *
 * Build (from the project root), compiling every .c file of src/core:
 *   gcc -std=c11 -Wall -Wextra -fsanitize=address -g
 *       tests/test_analysis.c (all src/core .c files) -o test_analysis -lm
 *   ./test_analysis
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../src/core/state.h"
#include "../src/core/analysis.h"

/* Copy a flat row-major array (rows x cols) into a MAX_PROC x MAX_RES matrix. */
static void fill(int dst[MAX_PROC][MAX_RES], int rows, int cols, const int *src)
{
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++)
            dst[i][j] = src[i * cols + j];
}

/* Build a state from flat arrays. alloc/maxm/req may be NULL (= all zeros). */
static void make_state(SystemState *s, int np, int nr, const int *total,
                       const int *alloc, const int *maxm, const int *req)
{
    state_init(s);
    s->n_proc = np;
    s->n_res = nr;
    for (int j = 0; j < nr; j++) s->total[j] = total[j];
    if (alloc) fill(s->allocation, np, nr, alloc);
    if (maxm)  fill(s->maximum,    np, nr, maxm);
    if (req)   fill(s->requests,   np, nr, req);
}

static bool close_to(double a, double b) { return fabs(a - b) < 1e-9; }

/* The classic textbook example (Silberschatz). */
static void make_textbook(SystemState *s)
{
    const int total[]  = {10, 5, 7};
    const int alloc[]  = {0,1,0,  2,0,0,  3,0,2,  2,1,1,  0,0,2};
    const int maxm[]   = {7,5,3,  3,2,2,  9,0,2,  2,2,2,  4,3,3};
    make_state(s, 5, 3, total, alloc, maxm, NULL);
}

static void test_safe_textbook(void)
{
    SystemState s;
    make_textbook(&s);
    /* Available = {3,3,2}; safe sequence <P1,P3,P4,P0,P2> exists. */
    assert(analysis_classify(&s) == STATE_SAFE);
    printf("PASS: textbook example is SAFE\n");
}

static void test_unsafe(void)
{
    /* One resource type with 2 instances, two processes.
     * Each holds 1 instance and may still need 1 more (max = 2), no pending
     * requests.  Available = 2 - 2 = 0, Need = {1},{1}.
     * Neither process can get its Need -> no safe sequence (UNSAFE), but
     * nobody is waiting yet, so it is not a deadlock. */
    const int total[] = {2};
    const int alloc[] = {1, 1};
    const int maxm[]  = {2, 2};
    SystemState s;
    make_state(&s, 2, 1, total, alloc, maxm, NULL);
    assert(analysis_classify(&s) == STATE_UNSAFE);
    printf("PASS: no-safe-sequence state is UNSAFE (not deadlocked)\n");
}

static void test_deadlocked(void)
{
    /* P0 holds R0 and waits for R1; P1 holds R1 and waits for R0. */
    const int total[] = {1, 1};
    const int alloc[] = {1,0,  0,1};
    const int maxm[]  = {1,1,  1,1};
    const int req[]   = {0,1,  1,0};
    SystemState s;
    make_state(&s, 2, 2, total, alloc, maxm, req);
    assert(analysis_classify(&s) == STATE_DEADLOCKED);
    printf("PASS: circular wait is DEADLOCKED\n");
}

static void test_utilization_percent(void)
{
    SystemState s;
    make_textbook(&s);
    /* used = {7,2,5} of {10,5,7}; overall = 14/22 */
    Utilization u = analysis_utilization(&s);
    assert(u.n_res == 3);
    assert(u.used[0] == 7 && u.used[1] == 2 && u.used[2] == 5);
    assert(close_to(u.percent[0], 70.0));
    assert(close_to(u.percent[1], 40.0));
    assert(close_to(u.percent[2], 500.0 / 7.0));
    assert(close_to(u.overall, 100.0 * 14.0 / 22.0));
    printf("PASS: utilization percentages are correct\n");
}

static void test_utilization_zero_total(void)
{
    /* R0 has 0 instances (must not divide by zero); R1 has 4, 1 in use. */
    const int total[] = {0, 4};
    const int alloc[] = {0, 1};
    SystemState s;
    make_state(&s, 1, 2, total, alloc, NULL, NULL);
    Utilization u = analysis_utilization(&s);
    assert(close_to(u.percent[0], 0.0));
    assert(close_to(u.percent[1], 25.0));
    assert(close_to(u.overall, 25.0));

    /* Every resource has total 0 -> overall must be 0, not NaN. */
    const int zero[] = {0, 0};
    make_state(&s, 1, 2, zero, NULL, NULL, NULL);
    u = analysis_utilization(&s);
    assert(close_to(u.overall, 0.0));
    printf("PASS: utilization handles total == 0\n");
}

static void test_class_names(void)
{
    assert(strcmp(analysis_class_name(STATE_SAFE), "SAFE") == 0);
    assert(strcmp(analysis_class_name(STATE_UNSAFE), "UNSAFE") == 0);
    assert(strcmp(analysis_class_name(STATE_DEADLOCKED), "DEADLOCKED") == 0);
    printf("PASS: analysis_class_name strings\n");
}

int main(void)
{
    test_safe_textbook();
    test_unsafe();
    test_deadlocked();
    test_utilization_percent();
    test_utilization_zero_total();
    test_class_names();
    printf("All analysis tests passed.\n");
    return 0;
}
