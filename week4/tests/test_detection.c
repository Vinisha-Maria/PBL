#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "../src/core/state.h"
#include "../src/core/detection.h"
#include "../src/core/recovery.h"



void test_no_deadlock() {
    SystemState s;
    state_init(&s);
    s.n_proc = 2; s.n_res = 2;
    s.total[0] = 5; s.total[1] = 5;
    s.allocation[0][0] = 1; s.allocation[0][1] = 1;
    DetectionResult res = detect_deadlock(&s);
    assert(res.deadlocked == false);
    printf("PASS: test_no_deadlock\n");
}

void test_circular_wait() {
    SystemState s;
    state_init(&s);
    s.n_proc = 2; s.n_res = 2;
    s.total[0] = 1; s.total[1] = 1;
    s.allocation[0][0] = 1; s.allocation[1][1] = 1;
    s.requests[0][1] = 1; s.requests[1][0] = 1;
    DetectionResult res = detect_deadlock(&s);
    assert(res.deadlocked == true);
    assert(res.n_deadlocked == 2);
    printf("PASS: test_circular_wait\n");
}

void test_partial_deadlock() {
    SystemState s;
    state_init(&s);
    s.n_proc = 3; s.n_res = 2;
    s.total[0] = 1; s.total[1] = 1;
    s.allocation[0][0] = 1; s.requests[0][1] = 1;
    s.allocation[1][1] = 1; s.requests[1][0] = 1;
    /* P2 has nothing - not involved in deadlock */
    DetectionResult res = detect_deadlock(&s);
    assert(res.deadlocked == true);
    assert(res.n_deadlocked == 2);
    printf("PASS: test_partial_deadlock\n");
}

void test_terminate_all() {
    SystemState s;
    state_init(&s);
    s.n_proc = 2; s.n_res = 2;
    s.total[0] = 1; s.total[1] = 1;
    s.allocation[0][0] = 1; s.allocation[1][1] = 1;
    s.requests[0][1] = 1; s.requests[1][0] = 1;
    RecoveryResult rec = recover_terminate_all(&s);
    assert(rec.success == true);
    assert(detect_deadlock(&s).deadlocked == false);
    printf("PASS: test_terminate_all\n");
}

void test_terminate_one_by_one() {
    SystemState s;
    state_init(&s);
    s.n_proc = 2; s.n_res = 2;
    s.total[0] = 1; s.total[1] = 1;
    s.allocation[0][0] = 1; s.allocation[1][1] = 1;
    s.requests[0][1] = 1; s.requests[1][0] = 1;
    RecoveryResult rec = recover_terminate_one_by_one(&s);
    assert(rec.success == true);
    assert(detect_deadlock(&s).deadlocked == false);
    printf("PASS: test_terminate_one_by_one\n");
}

void test_recover_preempt() {
    SystemState s;
    state_init(&s);
    s.n_proc = 2; s.n_res = 2;
    s.total[0] = 1; s.total[1] = 1;
    s.allocation[0][0] = 1; s.allocation[1][1] = 1;
    s.requests[0][1] = 1; s.requests[1][0] = 1;
    RecoveryResult rec = recover_preempt(&s, 0);
    assert(rec.success == true);
    assert(detect_deadlock(&s).deadlocked == false);
    printf("PASS: test_recover_preempt\n");
}

int main() {
    test_no_deadlock();
    test_circular_wait();
    test_partial_deadlock();
    test_terminate_all();
    test_terminate_one_by_one();
    test_recover_preempt();
    printf("ALL TESTS PASSED SUCCESSFULLY!\n");
    return 0;
}

