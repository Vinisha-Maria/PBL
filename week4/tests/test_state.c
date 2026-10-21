/* Tests for core/state.c (owner: Team Lead). Run with: make test */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "state.h"

static int g_calls = 0;
static void listener(void *ud) { g_calls += *(int *)ud; }

int main(void)
{
    SystemState s;
    char err[200];
    int av[MAX_RES], need[MAX_PROC][MAX_RES];

    /* empty state is invalid */
    state_init(&s);
    assert(state_validate(&s, err, sizeof err) == 1);
    printf("  ok: empty state rejected (%s)\n", err);

    /* sample data: valid, available {3,3,2}, need of P0 = {7,4,3} */
    state_load_sample(&s);
    assert(state_validate(&s, err, sizeof err) == 0);
    state_available(&s, av);
    assert(av[0] == 3 && av[1] == 3 && av[2] == 2);
    state_need(&s, need);
    assert(need[0][0] == 7 && need[0][1] == 4 && need[0][2] == 3);
    assert(need[1][0] == 1 && need[1][1] == 2 && need[1][2] == 2);
    printf("  ok: sample data valid, available and need correct\n");

    /* allocation above maximum is rejected */
    SystemState bad = s;
    bad.allocation[3][0] = 5;            /* P3 max for R0 is 2 */
    assert(state_validate(&bad, err, sizeof err) == 1);
    printf("  ok: %s\n", err);

    /* column sum above total is rejected */
    bad = s;
    bad.allocation[0][1] = 5;            /* R1 total is 5, others already hold 2 */
    assert(state_validate(&bad, err, sizeof err) == 1);
    printf("  ok: %s\n", err);

    /* negative value rejected */
    bad = s;
    bad.requests[2][2] = -1;
    assert(state_validate(&bad, err, sizeof err) == 1);

    /* allocation can be entered before maximum (maximum row all zero) */
    state_init(&bad);
    bad.n_proc = 2; bad.n_res = 2;
    bad.total[0] = 3; bad.total[1] = 3;
    bad.allocation[0][0] = 1; bad.allocation[1][1] = 2;
    assert(state_validate(&bad, err, sizeof err) == 0);
    printf("  ok: allocation can be entered before maximum\n");

    /* copy is independent */
    SystemState copy = s;
    copy.allocation[0][0] = 9;
    assert(s.allocation[0][0] == 0);
    (void)copy;

    /* names */
    char name[8];
    state_proc_name(3, name); assert(strcmp(name, "P3") == 0);
    state_res_name(1, name);  assert(strcmp(name, "R1") == 0);

    /* listeners */
    int one = 1, ten = 10;
    state_subscribe(listener, &one);
    state_subscribe(listener, &ten);
    state_notify();
    assert(g_calls == 11);
    printf("  ok: listeners notified\n");

    return 0;
}
