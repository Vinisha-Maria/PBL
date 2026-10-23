
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bankers.h"

/* ---------- a tiny listener so we can count state_notify() calls ---------- */
static int g_notifications = 0;
static void count_notification(void *user_data)
{
    (void)user_data;
    g_notifications++;
}

/* ---------- helpers ---------- */
static void load_textbook(SystemState *s)
{
    static const int total[3] = {10, 5, 7};
    static const int alloc[5][3] = {{0, 1, 0}, {2, 0, 0}, {3, 0, 2}, {2, 1, 1}, {0, 0, 2}};
    static const int maxi[5][3] = {{7, 5, 3}, {3, 2, 2}, {9, 0, 2}, {2, 2, 2}, {4, 3, 3}};

    state_init(s);
    s->n_proc = 5;
    s->n_res = 3;
    for (int j = 0; j < 3; j++) s->total[j] = total[j];
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 3; j++) {
            s->allocation[i][j] = alloc[i][j];
            s->maximum[i][j] = maxi[i][j];
        }
}

static void expect_vec(const int *got, const int *want, int n)
{
    for (int j = 0; j < n; j++) assert(got[j] == want[j]);
}

static void expect_sequence(const SafetyResult *r, const int *want, int n)
{
    assert(r->seq_len == n);
    for (int k = 0; k < n; k++) assert(r->safe_sequence[k] == want[k]);
}


static void assert_sequence_replays(const SystemState *s, const SafetyResult *r)
{
    int work[MAX_RES], need[MAX_PROC][MAX_RES];
    state_available(s, work);
    state_need(s, need);
    for (int k = 0; k < r->seq_len; k++) {
        int p = r->safe_sequence[k];
        for (int j = 0; j < s->n_res; j++) {
            assert(need[p][j] <= work[j]);
            work[j] += s->allocation[p][j];
        }
    }
}

static bool any_line_contains(const SafetyResult *r, const char *text)
{
    for (int k = 0; k < r->n_steps; k++)
        if (strstr(r->steps[k], text) != NULL) return true;
    return false;
}


static void load_after_p1(SystemState *s)
{
    const int req[MAX_RES] = {1, 0, 2};
    load_textbook(s);
    RequestResult r = bankers_request(s, 1, req);
    assert(r.granted);
}



static void test_textbook_is_safe(void)
{
    SystemState s;
    load_textbook(&s);

    SafetyResult r = bankers_is_safe(&s);
    assert(r.is_safe);
    const int want[5] = {1, 3, 4, 0, 2}; /* P1, P3, P4, P0, P2 */
    expect_sequence(&r, want, 5);
    assert_sequence_replays(&s, &r);
    assert(r.n_steps >= 5);
    assert(strstr(r.steps[1], "Step 1: Work=[3,3,2]. P1 can run because Need=[1,2,2] <= Work. New Work=[5,3,2]") != NULL);
    printf("PASS  textbook state is SAFE, sequence P1,P3,P4,P0,P2\n");
}

static void test_is_safe_does_not_modify_state(void)
{
    SystemState s, before;
    load_textbook(&s);
    before = s;
    int n0 = g_notifications;

    (void)bankers_is_safe(&s);

    assert(memcmp(&s, &before, sizeof s) == 0);
    assert(g_notifications == n0); /* looking is not changing */
    printf("PASS  bankers_is_safe leaves the state untouched\n");
}

static void test_request_p1_granted(void)
{
    SystemState s;
    load_textbook(&s);
    const int req[MAX_RES] = {1, 0, 2};
    int n0 = g_notifications;

    RequestResult r = bankers_request(&s, 1, req);

    assert(r.granted);
    assert(strcmp(r.message, BANKERS_MSG_GRANTED) == 0);
    assert(bankers_outcome(&r) == BANKERS_GRANTED);
    /* By hand: P1 allocation {2,0,0}+{1,0,2} = {3,0,2}; Available {3,3,2}-{1,0,2} = {2,3,0}; Need P1 {1,2,2}-{1,0,2} = {0,2,0} */
    const int want_alloc[3] = {3, 0, 2}, want_avail[3] = {2, 3, 0}, want_need[3] = {0, 2, 0};
    expect_vec(s.allocation[1], want_alloc, 3);
    int avail[MAX_RES], need[MAX_PROC][MAX_RES];
    state_available(&s, avail);
    state_need(&s, need);
    expect_vec(avail, want_avail, 3);
    expect_vec(need[1], want_need, 3);
    for (int j = 0; j < 3; j++) assert(s.requests[1][j] == 0); /* pending request cleared */
    /* the textbook says the new state is still safe with the same sequence */
    assert(r.safety.is_safe);
    const int want_seq[5] = {1, 3, 4, 0, 2};
    expect_sequence(&r.safety, want_seq, 5);
    assert(g_notifications == n0 + 1);
    printf("PASS  P1 requests {1,0,2}: GRANTED, Available becomes {2,3,0}\n");
}

static void test_request_p4_must_wait(void)
{
    SystemState s;
    load_after_p1(&s); /* Available is now {2,3,0} */
    const int req[MAX_RES] = {3, 3, 0};
    SystemState before = s;
    int n0 = g_notifications;

    RequestResult r = bankers_request(&s, 4, req);

    /* Need P4 = {4,3,1} so {3,3,0} is within its claim, but 3 > Available[0]=2. */
    assert(!r.granted);
    assert(strcmp(r.message, BANKERS_MSG_WAIT) == 0);
    assert(bankers_outcome(&r) == BANKERS_WAIT);
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 3; j++) assert(s.allocation[i][j] == before.allocation[i][j]);
    const int want_pending[3] = {3, 3, 0};
    expect_vec(s.requests[4], want_pending, 3);
    assert(g_notifications == n0 + 1);
    printf("PASS  P4 requests {3,3,0}: must WAIT (only {2,3,0} available)\n");
}

static void test_request_p0_denied_unsafe(void)
{
    SystemState s;
    load_after_p1(&s); /* Available {2,3,0} */
    const int req[MAX_RES] = {0, 2, 0};
    SystemState before = s;
    int n0 = g_notifications;

    RequestResult r = bankers_request(&s, 0, req);

    
    assert(!r.granted);
    assert(strcmp(r.message, BANKERS_MSG_UNSAFE) == 0);
    assert(bankers_outcome(&r) == BANKERS_DENIED);
    assert(!r.safety.is_safe);
    assert(r.safety.seq_len == 0);
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 3; j++) assert(s.allocation[i][j] == before.allocation[i][j]); /* real allocation unchanged */
    const int want_pending[3] = {0, 2, 0};
    expect_vec(s.requests[0], want_pending, 3);
    assert(g_notifications == n0 + 1);
    printf("PASS  P0 requests {0,2,0}: DENIED, it would make the state UNSAFE\n");
}

static void test_request_exceeds_need(void)
{
    SystemState s;
    load_textbook(&s);
    const int req[MAX_RES] = {2, 0, 0}; /* Need P1 = {1,2,2}, so 2 > 1 in R0 */
    SystemState before = s;
    int n0 = g_notifications;

    RequestResult r = bankers_request(&s, 1, req);

    assert(!r.granted);
    assert(strcmp(r.message, BANKERS_MSG_EXCEEDED) == 0);
    assert(bankers_outcome(&r) == BANKERS_ERROR);
    assert(memcmp(&s, &before, sizeof s) == 0); /* nothing at all changed */
    assert(g_notifications == n0);              /* ...so nobody was notified */
    printf("PASS  request above Need gives an error and changes nothing\n");
}

static void test_hand_made_unsafe_state(void)
{
   
    SystemState s;
    state_init(&s);
    s.n_proc = 2;
    s.n_res = 1;
    s.total[0] = 2;
    s.allocation[0][0] = 1;
    s.allocation[1][0] = 1;
    s.maximum[0][0] = 2;
    s.maximum[1][0] = 2;

    SafetyResult r = bankers_is_safe(&s);
    assert(!r.is_safe);
    assert(r.seq_len == 0);
    assert(r.n_steps >= 1);
    assert(strstr(r.steps[r.n_steps - 1], "Cannot finish: P0, P1") != NULL);

    
    SystemState t;
    load_after_p1(&t);
    t.allocation[0][1] += 2;
    SafetyResult r2 = bankers_is_safe(&t);
    assert(!r2.is_safe);
    assert(r2.seq_len == 0);
    assert(any_line_contains(&r2, "Cannot finish: P0, P1, P2, P3, P4"));
    printf("PASS  hand-made unsafe states give is_safe == false\n");
}

static void test_invalid_requests(void)
{
    SystemState s;
    load_textbook(&s);
    SystemState before = s;
    int n0 = g_notifications;
    const int ok[MAX_RES] = {0, 0, 0};
    const int neg[MAX_RES] = {1, -1, 0};

    assert(bankers_outcome(&(RequestResult){.granted = false}) == BANKERS_DENIED); /* sanity of the classifier */

    RequestResult a = bankers_request(&s, -1, ok);
    RequestResult b = bankers_request(&s, 5, ok); /* only P0..P4 exist */
    RequestResult c = bankers_request(&s, 0, neg);
    RequestResult d = bankers_request(NULL, 0, ok);
    RequestResult e = bankers_request(&s, 0, NULL);
    assert(!a.granted && bankers_outcome(&a) == BANKERS_ERROR);
    assert(!b.granted && bankers_outcome(&b) == BANKERS_ERROR);
    assert(!c.granted && bankers_outcome(&c) == BANKERS_ERROR);
    assert(!d.granted && bankers_outcome(&d) == BANKERS_ERROR);
    assert(!e.granted && bankers_outcome(&e) == BANKERS_ERROR);
    assert(memcmp(&s, &before, sizeof s) == 0);
    assert(g_notifications == n0);
    printf("PASS  bad pid / negative amount / NULL are rejected safely\n");
}

static void test_terminated_process(void)
{
   
    SystemState s;
    load_textbook(&s);
    for (int j = 0; j < 3; j++) s.allocation[1][j] = 0;
    s.terminated[1] = true;

    SafetyResult r = bankers_is_safe(&s);
    assert(r.is_safe);
    const int want[4] = {3, 4, 0, 2};
    expect_sequence(&r, want, 4);
    assert_sequence_replays(&s, &r);

    const int req[MAX_RES] = {0, 0, 0};
    RequestResult q = bankers_request(&s, 1, req);
    assert(!q.granted && bankers_outcome(&q) == BANKERS_ERROR); /* a terminated process cannot request */
    printf("PASS  terminated process is skipped (sequence P3,P4,P0,P2)\n");
}

static void test_everything_terminated_or_empty(void)
{
    SystemState s;
    state_init(&s); /* 0 processes, 0 resources */
    SafetyResult r = bankers_is_safe(&s);
    assert(r.is_safe && r.seq_len == 0);
    printf("PASS  empty system is trivially SAFE\n");
}

int main(void)
{
    state_subscribe(count_notification, NULL);

    test_textbook_is_safe();
    test_is_safe_does_not_modify_state();
    test_request_p1_granted();
    test_request_p4_must_wait();
    test_request_p0_denied_unsafe();
    test_request_exceeds_need();
    test_hand_made_unsafe_state();
    test_invalid_requests();
    test_terminated_process();
    test_everything_terminated_or_empty();

    printf("\nAll 10 tests passed.\n");
    return 0;
}
