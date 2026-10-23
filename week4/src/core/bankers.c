
#include "bankers.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define VEC_LEN 128 /* room for "[a,b,c,...]" with 10 numbers */


#if defined(__GNUC__)
__attribute__((format(printf, 3, 4)))
#endif
static void add_line(char lines[MAX_LINES][LINE_LEN], int *count, const char *fmt, ...)
{
    if (*count < 0 || *count >= MAX_LINES) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(lines[*count], LINE_LEN, fmt, ap);
    va_end(ap);
    (*count)++;
}


static void format_vec(char *buf, size_t len, const int *v, int n)
{
    size_t pos = 0;
    if (len == 0) return;
    buf[0] = '\0';
    for (int i = -1; i <= n; i++) {
        int w;
        if (pos + 1 >= len) break; /* no room left */
        if (i == -1) w = snprintf(buf + pos, len - pos, "[");
        else if (i == n) w = snprintf(buf + pos, len - pos, "]");
        else w = snprintf(buf + pos, len - pos, "%s%d", i > 0 ? "," : "", v[i]);
        if (w < 0) break;
        pos += (size_t)w;
    }
}


static bool vec_leq(const int *a, const int *b, int n)
{
    for (int j = 0; j < n; j++)
        if (a[j] > b[j]) return false;
    return true;
}


SafetyResult bankers_is_safe(const SystemState *s)
{
    SafetyResult r;
    memset(&r, 0, sizeof r);

    if (s == NULL || s->n_proc < 0 || s->n_proc > MAX_PROC || s->n_res < 0 || s->n_res > MAX_RES) {
        add_line(r.steps, &r.n_steps, "Error: invalid system state");
        return r; /* is_safe stays false */
    }
    const int n = s->n_proc;
    const int m = s->n_res;

    /* Work = Available;  Need = Maximum - Allocation (both from state.h). */
    int work[MAX_RES];
    int need[MAX_PROC][MAX_RES];
    memset(work, 0, sizeof work);
    memset(need, 0, sizeof need);
    state_available(s, work);
    state_need(s, need);

    /* A state with negative numbers is meaningless; refuse to judge it. */
    for (int j = 0; j < m; j++) {
        if (work[j] < 0) {
            char rn[8];
            state_res_name(j, rn);
            add_line(r.steps, &r.n_steps, "Error: invalid state, more units of %s are allocated than exist", rn);
            return r;
        }
    }
    for (int i = 0; i < n; i++) {
        if (s->terminated[i]) continue;
        for (int j = 0; j < m; j++) {
            if (need[i][j] < 0) {
                char pn[8];
                state_proc_name(i, pn);
                add_line(r.steps, &r.n_steps, "Error: invalid state, %s holds more than its maximum claim", pn);
                return r;
            }
        }
    }

    /* Finish[i] = false, except terminated processes which are already done. */
    bool finish[MAX_PROC];
    for (int i = 0; i < MAX_PROC; i++) finish[i] = (i < n) ? s->terminated[i] : true;

    char wbuf[VEC_LEN], nbuf[VEC_LEN], w2buf[VEC_LEN];
    format_vec(wbuf, sizeof wbuf, work, m);
    add_line(r.steps, &r.n_steps, "Start: Work = Available = %s", wbuf);

  
    int step = 0;
    bool progress = true;
    while (progress) {
        progress = false;
        for (int i = 0; i < n; i++) {
            if (finish[i] || !vec_leq(need[i], work, m)) continue;

            char pn[8];
            state_proc_name(i, pn);
            format_vec(wbuf, sizeof wbuf, work, m);
            format_vec(nbuf, sizeof nbuf, need[i], m);
            for (int j = 0; j < m; j++) work[j] += s->allocation[i][j]; /* P_i finishes and releases */
            format_vec(w2buf, sizeof w2buf, work, m);

            step++;
            add_line(r.steps, &r.n_steps, "Step %d: Work=%s. %s can run because Need=%s <= Work. New Work=%s",
                     step, wbuf, pn, nbuf, w2buf);

            finish[i] = true;
            r.safe_sequence[r.seq_len++] = i; /* seq_len <= n <= MAX_PROC */
            progress = true;
        }
    }

    /* Who is left? */
    char left[LINE_LEN];
    left[0] = '\0';
    int n_left = 0;
    for (int i = 0; i < n; i++) {
        if (finish[i]) continue;
        char pn[8];
        state_proc_name(i, pn);
        size_t used = strlen(left);
        snprintf(left + used, sizeof left - used, "%s%s", n_left > 0 ? ", " : "", pn);
        n_left++;
    }

    if (n_left == 0) {
        r.is_safe = true;
        char seq[LINE_LEN];
        seq[0] = '\0';
        for (int k = 0; k < r.seq_len; k++) {
            char pn[8];
            state_proc_name(r.safe_sequence[k], pn);
            size_t used = strlen(seq);
            snprintf(seq + used, sizeof seq - used, "%s%s", k > 0 ? " -> " : "", pn);
        }
        if (r.seq_len > 0) add_line(r.steps, &r.n_steps, "SAFE: safe sequence is %s", seq);
        else add_line(r.steps, &r.n_steps, "SAFE: no active processes, nothing left to run");
    } else {
        r.is_safe = false;
        format_vec(wbuf, sizeof wbuf, work, m);
        add_line(r.steps, &r.n_steps, "UNSAFE: no remaining process has Need <= Work=%s. Cannot finish: %s", wbuf, left);
    }
    return r;
}


static RequestResult make_error(const char *msg)
{
    RequestResult r;
    memset(&r, 0, sizeof r);
    r.granted = false;
    snprintf(r.message, sizeof r.message, "%s", msg);
    add_line(r.safety.steps, &r.safety.n_steps, "%s", msg);
    return r;
}

RequestResult bankers_request(SystemState *s, int pid, const int request[MAX_RES])
{
    RequestResult r;
    memset(&r, 0, sizeof r);

    /* ---- input validation (no state change, so no notify) ---- */
    if (s == NULL || request == NULL || s->n_proc < 0 || s->n_proc > MAX_PROC || s->n_res < 0 ||
        s->n_res > MAX_RES)
        return make_error("Error: invalid request");
    if (pid < 0 || pid >= s->n_proc) return make_error("Error: no such process");
    if (s->terminated[pid]) return make_error("Error: process has already terminated");
    const int m = s->n_res;
    for (int j = 0; j < m; j++)
        if (request[j] < 0) return make_error("Error: request contains a negative amount");

    char pn[8];
    state_proc_name(pid, pn);

    int need[MAX_PROC][MAX_RES];
    int avail[MAX_RES];
    memset(need, 0, sizeof need);
    memset(avail, 0, sizeof avail);
    state_need(s, need);
    state_available(s, avail);

    char reqb[VEC_LEN], needb[VEC_LEN], availb[VEC_LEN];
    format_vec(reqb, sizeof reqb, request, m);
    format_vec(needb, sizeof needb, need[pid], m);
    format_vec(availb, sizeof availb, avail, m);

   
    if (!vec_leq(request, need[pid], m)) {
        snprintf(r.message, sizeof r.message, "%s", BANKERS_MSG_EXCEEDED);
        add_line(r.safety.steps, &r.safety.n_steps, "Step 1: Request %s of %s <= Need %s ?  NO", reqb, pn, needb);
        add_line(r.safety.steps, &r.safety.n_steps, "%s", r.message);
        return r; /* nothing changed */
    }
    add_line(r.safety.steps, &r.safety.n_steps, "Step 1: Request %s of %s <= Need %s ?  yes", reqb, pn, needb);

   
    if (!vec_leq(request, avail, m)) {
        snprintf(r.message, sizeof r.message, "%s", BANKERS_MSG_WAIT);
        add_line(r.safety.steps, &r.safety.n_steps, "Step 2: Request %s <= Available %s ?  NO", reqb, availb);
        add_line(r.safety.steps, &r.safety.n_steps, "%s", r.message);
        for (int j = 0; j < MAX_RES; j++) s->requests[pid][j] = (j < m) ? request[j] : 0;
        state_notify();
        return r;
    }
    add_line(r.safety.steps, &r.safety.n_steps, "Step 2: Request %s <= Available %s ?  yes", reqb, availb);

   
    SystemState copy = *s; /* no pointers inside, so this is a deep copy */
    for (int j = 0; j < m; j++) copy.allocation[pid][j] += request[j];

    int copy_avail[MAX_RES];
    int copy_need[MAX_PROC][MAX_RES];
    memset(copy_avail, 0, sizeof copy_avail);
    memset(copy_need, 0, sizeof copy_need);
    state_available(&copy, copy_avail);
    state_need(&copy, copy_need);
    format_vec(availb, sizeof availb, copy_avail, m);
    format_vec(needb, sizeof needb, copy_need[pid], m);
    add_line(r.safety.steps, &r.safety.n_steps,
             "Step 3: Pretend to allocate. Available would become %s and Need of %s would become %s.", availb, pn, needb);
    add_line(r.safety.steps, &r.safety.n_steps, "        Running the safety algorithm on that pretend state:");

    SafetyResult sr = bankers_is_safe(&copy);
    r.safety.is_safe = sr.is_safe;
    r.safety.seq_len = sr.seq_len;
    memcpy(r.safety.safe_sequence, sr.safe_sequence, sizeof r.safety.safe_sequence);
    for (int k = 0; k < sr.n_steps; k++) add_line(r.safety.steps, &r.safety.n_steps, "  %s", sr.steps[k]);

    if (sr.is_safe) {
      
        for (int j = 0; j < m; j++) s->allocation[pid][j] += request[j];
        for (int j = 0; j < MAX_RES; j++) s->requests[pid][j] = 0;
        r.granted = true;
        snprintf(r.message, sizeof r.message, "%s", BANKERS_MSG_GRANTED);
        add_line(r.safety.steps, &r.safety.n_steps, "%s", r.message);
        state_notify();
    } else {
        /* Unsafe: leave the allocation alone, remember the pending request. */
        for (int j = 0; j < MAX_RES; j++) s->requests[pid][j] = (j < m) ? request[j] : 0;
        r.granted = false;
        snprintf(r.message, sizeof r.message, "%s", BANKERS_MSG_UNSAFE);
        add_line(r.safety.steps, &r.safety.n_steps, "%s", r.message);
        state_notify();
    }
    return r;
}

BankersOutcome bankers_outcome(const RequestResult *r)
{
    if (r == NULL) return BANKERS_ERROR;
    if (r->granted) return BANKERS_GRANTED;
    if (strncmp(r->message, "Error:", 6) == 0) return BANKERS_ERROR;
    if (strcmp(r->message, BANKERS_MSG_WAIT) == 0) return BANKERS_WAIT;
    return BANKERS_DENIED;
}
