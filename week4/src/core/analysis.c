/* src/core/analysis.c - see analysis.h */
#include <string.h>
#include "analysis.h"
#include "bankers.h"    /* bankers_is_safe  (Student 2) */
#include "detection.h"  /* detect_deadlock  (Student 2) */

StateClass analysis_classify(const SystemState *s)
{
    if (s == NULL)
        return STATE_UNSAFE;           /* nothing to analyse: be cautious */

    /* Step 1: deadlock is the most serious condition, check it first. */
    DetectionResult det = detect_deadlock(s);
    if (det.deadlocked)
        return STATE_DEADLOCKED;

    /* Step 2: no deadlock -> is there at least one safe sequence? */
    SafetyResult safety = bankers_is_safe(s);
    return safety.is_safe ? STATE_SAFE : STATE_UNSAFE;
}

const char *analysis_class_name(StateClass c)
{
    switch (c) {
    case STATE_SAFE:       return "SAFE";
    case STATE_UNSAFE:     return "UNSAFE";
    case STATE_DEADLOCKED: return "DEADLOCKED";
    }
    return "UNKNOWN";                  /* only for out-of-range values */
}

Utilization analysis_utilization(const SystemState *s)
{
    Utilization u;
    memset(&u, 0, sizeof u);
    if (s == NULL)
        return u;

    int n = s->n_res;
    if (n < 0) n = 0;
    if (n > MAX_RES) n = MAX_RES;
    u.n_res = n;

    int avail[MAX_RES] = {0};
    state_available(s, avail);         /* total - sum of allocation */

    long used_sum = 0, total_sum = 0;
    for (int j = 0; j < n; j++) {
        int used = s->total[j] - avail[j];
        if (used < 0) used = 0;        /* defensive: never report negative */
        u.total[j] = s->total[j];
        u.used[j]  = used;
        /* total == 0 -> 0 % (avoid dividing by zero) */
        u.percent[j] = (s->total[j] > 0) ? 100.0 * used / s->total[j] : 0.0;
        used_sum  += used;
        total_sum += (s->total[j] > 0) ? s->total[j] : 0;
    }
    u.overall = (total_sum > 0) ? 100.0 * (double)used_sum / (double)total_sum
                                : 0.0;
    return u;
}
