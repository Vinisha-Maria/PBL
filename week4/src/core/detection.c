#include "detection.h"
#include <string.h>
#include <stdio.h>

DetectionResult detect_deadlock(const SystemState *s) {
    DetectionResult res;
    memset(&res, 0, sizeof(res));
    res.deadlocked = false;

    int work[MAX_RES];
    state_available(s, work);

    bool finish[MAX_PROC];
    for (int i = 0; i < s->n_proc; i++) {
        if (s->terminated[i]) {
            finish[i] = true;
            continue;
        }
        bool has_alloc = false;
        for (int j = 0; j < s->n_res; j++) {
            if (s->allocation[i][j] > 0) {
                has_alloc = true;
                break;
            }
        }
        finish[i] = !has_alloc;
    }

    if (res.n_steps < MAX_LINES) {
        char w_str[64] = "[";
        for (int j = 0; j < s->n_res; j++) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d%s", work[j], (j == s->n_res - 1) ? "" : ",");
            strcat(w_str, buf);
        }
        strcat(w_str, "]");
        snprintf(res.steps[res.n_steps++], LINE_LEN, "Initial Work = %s", w_str);
    }

    bool progress = true;
    while (progress) {
        progress = false;
        for (int i = 0; i < s->n_proc; i++) {
            if (!finish[i]) {
                bool can_run = true;
                for (int j = 0; j < s->n_res; j++) {
                    if (s->requests[i][j] > work[j]) {
                        can_run = false;
                        break;
                    }
                }
                if (can_run) {
                    for (int j = 0; j < s->n_res; j++) {
                        work[j] += s->allocation[i][j];
                    }
                    finish[i] = true;
                    progress = true;

                    if (res.n_steps < MAX_LINES) {
                        char p_name[8];
                        state_proc_name(i, p_name);
                        snprintf(res.steps[res.n_steps++], LINE_LEN, 
                                 "Process %s can proceed. Request satisfied. Work updated.", p_name);
                    }
                } else {
                    if (res.n_steps < MAX_LINES) {
                        char p_name[8];
                        state_proc_name(i, p_name);
                        snprintf(res.steps[res.n_steps++], LINE_LEN, 
                                 "Process %s cannot proceed: Request exceeds available work.", p_name);
                    }
                }
            }
        }
    }

    res.n_deadlocked = 0;
    for (int i = 0; i < s->n_proc; i++) {
        if (!finish[i] && !s->terminated[i]) {
            res.deadlocked_procs[res.n_deadlocked++] = i;
        }
    }

    if (res.n_deadlocked > 0) {
        res.deadlocked = true;
        if (res.n_steps < MAX_LINES) {
            snprintf(res.steps[res.n_steps++], LINE_LEN, "DEADLOCK DETECTED! %d process(es) stuck.", res.n_deadlocked);
        }
    } else {
        if (res.n_steps < MAX_LINES) {
            snprintf(res.steps[res.n_steps++], LINE_LEN, "No deadlock detected. System is safe.");
        }
    }

    return res;
}

