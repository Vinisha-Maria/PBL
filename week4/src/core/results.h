#ifndef RESULTS_H
#define RESULTS_H

/* Result structs shared by all modules. Owner: Team Lead. No GTK here. */

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include "state.h"

#define MAX_LINES 128
#define LINE_LEN  160

typedef struct {
    bool is_safe;
    int  safe_sequence[MAX_PROC];  int seq_len;
    char steps[MAX_LINES][LINE_LEN]; int n_steps;
} SafetyResult;

typedef struct {
    bool         granted;          /* true only if the request was applied to the state */
    char         message[LINE_LEN];
    SafetyResult safety;           /* result of the safety check run for this request   */
} RequestResult;

typedef struct {
    bool deadlocked;
    int  deadlocked_procs[MAX_PROC]; int n_deadlocked;
    char steps[MAX_LINES][LINE_LEN]; int n_steps;
} DetectionResult;

typedef struct {
    bool success;
    char actions[MAX_LINES][LINE_LEN]; int n_actions;
} RecoveryResult;

/* Appends a printf-style line to a log array (bounds-checked; silently ignored when full).
 * Usage:  add_line(r.steps, &r.n_steps, "Work=[%d,%d]", a, b); */
static inline void add_line(char lines[MAX_LINES][LINE_LEN], int *count, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static inline void add_line(char lines[MAX_LINES][LINE_LEN], int *count, const char *fmt, ...)
{
    if (*count < 0 || *count >= MAX_LINES)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(lines[*count], LINE_LEN, fmt, ap);
    va_end(ap);
    (*count)++;
}

#endif /* RESULTS_H */
