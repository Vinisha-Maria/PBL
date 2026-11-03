#include "state.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MAX_LISTENERS 32

static struct {
    StateListener cb;
    void         *user_data;
} g_listeners[MAX_LISTENERS];
static int g_n_listeners = 0;

void state_init(SystemState *s)
{
    memset(s, 0, sizeof *s);
}

void state_available(const SystemState *s, int out[MAX_RES])
{
    for (int j = 0; j < MAX_RES; j++) {
        int used = 0;
        for (int i = 0; i < s->n_proc; i++)
            used += s->allocation[i][j];
        out[j] = (j < s->n_res) ? s->total[j] - used : 0;
    }
}

void state_need(const SystemState *s, int out[MAX_PROC][MAX_RES])
{
    for (int i = 0; i < MAX_PROC; i++)
        for (int j = 0; j < MAX_RES; j++)
            out[i][j] = (i < s->n_proc && j < s->n_res)
                            ? s->maximum[i][j] - s->allocation[i][j]
                            : 0;
}

static int fail(char *err, size_t len, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static int fail(char *err, size_t len, const char *fmt, ...)
{
    if (err && len > 0) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, len, fmt, ap);
        va_end(ap);
    }
    return 1;
}

int state_validate(const SystemState *s, char *err, size_t err_len)
{
    if (err && err_len > 0)
        err[0] = '\0';

    if (s->n_proc < 1 || s->n_proc > MAX_PROC)
        return fail(err, err_len, "Number of processes must be between 1 and %d. Create processes first.", MAX_PROC);
    if (s->n_res < 1 || s->n_res > MAX_RES)
        return fail(err, err_len, "Number of resources must be between 1 and %d. Create resources first.", MAX_RES);

    for (int j = 0; j < s->n_res; j++)
        if (s->total[j] < 0)
            return fail(err, err_len, "Total instances of R%d cannot be negative.", j);

    for (int i = 0; i < s->n_proc; i++) {
        bool max_entered = false;
        for (int j = 0; j < s->n_res; j++)
            if (s->maximum[i][j] != 0)
                max_entered = true;

        for (int j = 0; j < s->n_res; j++) {
            if (s->allocation[i][j] < 0)
                return fail(err, err_len, "P%d has a negative allocation of R%d.", i, j);
            if (s->maximum[i][j] < 0)
                return fail(err, err_len, "P%d has a negative maximum for R%d.", i, j);
            if (s->requests[i][j] < 0)
                return fail(err, err_len, "P%d has a negative request for R%d.", i, j);
            if (s->maximum[i][j] > s->total[j])
                return fail(err, err_len, "P%d maximum of R%d (%d) exceeds the total instances (%d).",
                            i, j, s->maximum[i][j], s->total[j]);
            if (max_entered && s->allocation[i][j] > s->maximum[i][j])
                return fail(err, err_len, "P%d allocation of R%d exceeds its maximum.", i, j);
        }
    }

    for (int j = 0; j < s->n_res; j++) {
        int sum = 0;
        for (int i = 0; i < s->n_proc; i++)
            sum += s->allocation[i][j];
        if (sum > s->total[j])
            return fail(err, err_len, "Total allocation of R%d (%d) exceeds its total instances (%d).",
                        j, sum, s->total[j]);
    }
    return 0;
}

void state_load_sample(SystemState *s)
{
    static const int alloc[5][3] = {{0,1,0},{2,0,0},{3,0,2},{2,1,1},{0,0,2}};
    static const int maxi[5][3]  = {{7,5,3},{3,2,2},{9,0,2},{2,2,2},{4,3,3}};
    static const int total[3]    = {10, 5, 7};

    state_init(s);
    s->n_proc = 5;
    s->n_res  = 3;
    for (int j = 0; j < 3; j++)
        s->total[j] = total[j];
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 3; j++) {
            s->allocation[i][j] = alloc[i][j];
            s->maximum[i][j]    = maxi[i][j];
        }
}

void state_proc_name(int i, char out[8]) { snprintf(out, 8, "P%d", i); }
void state_res_name(int j, char out[8])  { snprintf(out, 8, "R%d", j); }

void state_subscribe(StateListener cb, void *user_data)
{
    if (cb && g_n_listeners < MAX_LISTENERS) {
        g_listeners[g_n_listeners].cb = cb;
        g_listeners[g_n_listeners].user_data = user_data;
        g_n_listeners++;
    }
}

void state_notify(void)
{
    for (int i = 0; i < g_n_listeners; i++)
        g_listeners[i].cb(g_listeners[i].user_data);
}
