
#include "recovery.h"
#include "detection.h"
#include <string.h>
#include <stdio.h>

RecoveryResult recover_terminate_all(SystemState *s) {
    RecoveryResult res;
    memset(&res, 0, sizeof(res));
    res.success = false;

    DetectionResult det = detect_deadlock(s);
    if (!det.deadlocked) {
        snprintf(res.actions[res.n_actions++], LINE_LEN, "No deadlock to recover from.");
        res.success = true;
        return res;
    }

    snprintf(res.actions[res.n_actions++], LINE_LEN, "Initiating Terminate-All Recovery Strategy...");
    for (int k = 0; k < det.n_deadlocked; k++) {
        int p = det.deadlocked_procs[k];
        s->terminated[p] = true;
        for (int j = 0; j < s->n_res; j++) {
            s->allocation[p][j] = 0;
            s->requests[p][j] = 0;
        }
        char p_name[8];
        state_proc_name(p, p_name);
        if (res.n_actions < MAX_LINES) {
            snprintf(res.actions[res.n_actions++], LINE_LEN, "Terminated process %s. Released resources.", p_name);
        }
    }

    DetectionResult post_det = detect_deadlock(s);
    if (!post_det.deadlocked) {
        res.success = true;
        if (res.n_actions < MAX_LINES) {
            snprintf(res.actions[res.n_actions++], LINE_LEN, "Deadlock successfully resolved.");
        }
    }

    state_notify();
    return res;
}

RecoveryResult recover_terminate_one_by_one(SystemState *s) {
    RecoveryResult res;
    memset(&res, 0, sizeof(res));
    res.success = false;

    int iterations = 0;
    while (iterations < MAX_PROC) {
        DetectionResult det = detect_deadlock(s);
        if (!det.deadlocked) {
            res.success = true;
            if (res.n_actions < MAX_LINES) {
                snprintf(res.actions[res.n_actions++], LINE_LEN, "Deadlock successfully resolved.");
            }
            break;
        }

        int best_victim = -1;
        int min_resources = 999999;

        for (int k = 0; k < det.n_deadlocked; k++) {
            int p = det.deadlocked_procs[k];
            int total_held = 0;
            for (int j = 0; j < s->n_res; j++) {
                total_held += s->allocation[p][j];
            }
            if (total_held < min_resources) {
                min_resources = total_held;
                best_victim = p;
            } else if (total_held == min_resources) {
                if (best_victim == -1 || p < best_victim) {
                    best_victim = p;
                }
            }
        }

        if (best_victim == -1) break;

        s->terminated[best_victim] = true;
        for (int j = 0; j < s->n_res; j++) {
            s->allocation[best_victim][j] = 0;
            s->requests[best_victim][j] = 0;
        }

        char p_name[8];
        state_proc_name(best_victim, p_name);
        if (res.n_actions < MAX_LINES) {
            snprintf(res.actions[res.n_actions++], LINE_LEN, 
                     "Terminated %s (holds %d instance(s)). Re-running detection...", p_name, min_resources);
        }

        iterations++;
    }

    DetectionResult final_det = detect_deadlock(s);
    if (!final_det.deadlocked) {
        res.success = true;
    }

    state_notify();
    return res;
}

RecoveryResult recover_preempt(SystemState *s, int victim) {
    RecoveryResult res;
    memset(&res, 0, sizeof(res));
    res.success = false;

    if (victim < 0 || victim >= s->n_proc) {
        snprintf(res.actions[res.n_actions++], LINE_LEN, "Error: Invalid victim index %d.", victim);
        return res;
    }

    char p_name[8];
    state_proc_name(victim, p_name);
    snprintf(res.actions[res.n_actions++], LINE_LEN, "Preempting resources from process %s...", p_name);

    for (int j = 0; j < s->n_res; j++) {
        s->allocation[victim][j] = 0;
    }

    snprintf(res.actions[res.n_actions++], LINE_LEN, "Process %s rolled back (allocation reclaimed).", p_name);

    DetectionResult post_det = detect_deadlock(s);
    if (!post_det.deadlocked) {
        res.success = true;
        snprintf(res.actions[res.n_actions++], LINE_LEN, "Deadlock resolved by preemption on %s.", p_name);
    } else {
        snprintf(res.actions[res.n_actions++], LINE_LEN, "Deadlock persists after preemption.");
    }

    state_notify();
    return res;
}
