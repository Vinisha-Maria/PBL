/* src/core/analysis.h
 * State classification (SAFE / UNSAFE / DEADLOCKED) and resource utilization.
 * Pure logic: this file and analysis.c must NEVER include GTK headers.
 */
#ifndef ANALYSIS_H
#define ANALYSIS_H

#include <stdbool.h>
#include <stddef.h>
#include "state.h"
#include "results.h"

/* The three possible classifications of a system state. */
typedef enum { STATE_SAFE, STATE_UNSAFE, STATE_DEADLOCKED } StateClass;

/* Classify the state:
 *   1. detect_deadlock() finds deadlocked processes  -> STATE_DEADLOCKED
 *   2. otherwise bankers_is_safe() has a safe sequence -> STATE_SAFE
 *   3. otherwise                                       -> STATE_UNSAFE
 * The state is only read, never modified. */
StateClass analysis_classify(const SystemState *s);

/* "SAFE", "UNSAFE" or "DEADLOCKED" (never NULL). */
const char *analysis_class_name(StateClass c);

/* How much of each resource type is currently in use. */
typedef struct {
    int    n_res;            /* number of resource types                    */
    int    used[MAX_RES];    /* used[j]    = total[j] - available[j]        */
    int    total[MAX_RES];   /* total[j]   = copy of s->total[j]            */
    double percent[MAX_RES]; /* percent[j] = used/total*100 (0 if total==0) */
    double overall;          /* sum(used) / sum(total) * 100 (0 if no inst.) */
} Utilization;

/* Compute utilization. "overall" is the TOTAL used instances divided by the
 * TOTAL instances of all resources (so a resource with 10 instances counts
 * more than one with 1). Division by zero is avoided: 0.0 is returned. */
Utilization analysis_utilization(const SystemState *s);

#endif /* ANALYSIS_H */
