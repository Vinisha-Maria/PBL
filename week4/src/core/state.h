#ifndef STATE_H
#define STATE_H

/* Shared data model for the Deadlock Simulation System.
 * Owner: Team Lead.  Do NOT change without telling the team.
 * This file must never include GTK headers. */

#include <stdbool.h>
#include <stddef.h>

#define MAX_PROC 10
#define MAX_RES  10

typedef struct {
    int  n_proc, n_res;
    int  total[MAX_RES];                 /* total instances of each resource       */
    int  allocation[MAX_PROC][MAX_RES];  /* instances of Rj currently held by Pi   */
    int  maximum[MAX_PROC][MAX_RES];     /* maximum demand of Pi for Rj            */
    int  requests[MAX_PROC][MAX_RES];    /* pending (waiting) request of Pi for Rj */
    bool terminated[MAX_PROC];           /* terminated during recovery             */
} SystemState;

/* Reset to an empty system (0 processes, 0 resources). */
void state_init(SystemState *s);

/* out[j] = total[j] - sum over all processes of allocation[i][j]. */
void state_available(const SystemState *s, int out[MAX_RES]);

/* out[i][j] = maximum[i][j] - allocation[i][j]  (may be negative if data is invalid). */
void state_need(const SystemState *s, int out[MAX_PROC][MAX_RES]);

/* Returns 0 if the state is valid. Otherwise returns 1 and writes a human-readable
 * message into err.  Note: a process whose Maximum row is all zeros is treated as
 * "maximum not entered yet", so the Allocation matrix can be entered first. */
int  state_validate(const SystemState *s, char *err, size_t err_len);

/* Loads the textbook example (5 processes, 3 resources, safe state). */
void state_load_sample(SystemState *s);

void state_proc_name(int i, char out[8]);   /* "P0" */
void state_res_name(int j, char out[8]);    /* "R0" */

/* GUI refresh mechanism: tabs subscribe, anything that changes the state calls state_notify(). */
typedef void (*StateListener)(void *user_data);
void state_subscribe(StateListener cb, void *user_data);   /* up to 32 listeners */
void state_notify(void);

#endif /* STATE_H */
