/* src/core/bankers.c - Banker's Algorithm safety check.
 * Determines whether there exists a safe sequence of process execution. */

#include "bankers.h"
#include <string.h>
#include <stdio.h>

SafetyResult bankers_is_safe(const SystemState *s)
{
    SafetyResult res;
    memset(&res, 0, sizeof(res));
    res.is_safe = false;
    res.seq_len = 0;
    res.n_steps = 0;

    if (s == NULL) {
        add_line(res.steps, &res.n_steps, "Error: NULL state pointer.");
        return res;
    }

    int np = s->n_proc;
    int nr = s->n_res;

    /* Work = Available */
    int work[MAX_RES];
    state_available(s, work);

    /* Need = Maximum - Allocation */
    int need[MAX_PROC][MAX_RES];
    state_need(s, need);

    bool finish[MAX_PROC];
    memset(finish, false, sizeof(finish));

    /* Mark terminated processes as already finished */
    for (int i = 0; i < np; i++)
        if (s->terminated[i])
            finish[i] = true;

    /* Log initial Work vector */
    {
        char w_str[128] = "[";
        for (int j = 0; j < nr; j++) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d%s", work[j], (j == nr - 1) ? "" : ",");
            strcat(w_str, buf);
        }
        strcat(w_str, "]");
        add_line(res.steps, &res.n_steps, "Initial Work = %s", w_str);
    }

    /* Main loop: find processes whose Need <= Work */
    int completed = 0;
    for (int i = 0; i < np; i++)
        if (finish[i])
            completed++;

    bool progress = true;
    while (progress) {
        progress = false;
        for (int i = 0; i < np; i++) {
            if (finish[i])
                continue;

            /* Check if Need[i] <= Work */
            bool can_run = true;
            for (int j = 0; j < nr; j++) {
                if (need[i][j] > work[j]) {
                    can_run = false;
                    break;
                }
            }

            if (can_run) {
                /* Simulate: Work = Work + Allocation[i] */
                char p_name[8];
                state_proc_name(i, p_name);

                for (int j = 0; j < nr; j++)
                    work[j] += s->allocation[i][j];

                finish[i] = true;
                res.safe_sequence[res.seq_len++] = i;
                completed++;
                progress = true;

                /* Log the step */
                char w_str[128] = "[";
                for (int j = 0; j < nr; j++) {
                    char buf[16];
                    snprintf(buf, sizeof(buf), "%d%s", work[j], (j == nr - 1) ? "" : ",");
                    strcat(w_str, buf);
                }
                strcat(w_str, "]");
                add_line(res.steps, &res.n_steps,
                         "%s can finish. Work updated to %s", p_name, w_str);
            }
        }
    }

    if (completed == np) {
        res.is_safe = true;
        /* Log the safe sequence */
        char seq_str[128] = "<";
        for (int k = 0; k < res.seq_len; k++) {
            char p_name[8];
            state_proc_name(res.safe_sequence[k], p_name);
            strcat(seq_str, p_name);
            if (k < res.seq_len - 1)
                strcat(seq_str, ", ");
        }
        strcat(seq_str, ">");
        add_line(res.steps, &res.n_steps, "System is SAFE. Safe sequence: %s", seq_str);
    } else {
        add_line(res.steps, &res.n_steps, "System is UNSAFE. No safe sequence exists.");
    }

    return res;
}
