/* core.c - Core process (Rajath) of the OS Process Scheduler Simulator.
 *   CPU execution : sched_tick() runs one time unit
 *   Queue         : ready queue (rq)
 *   Memory        : process table (p[], MAX_PROCESSES entries)
 *   Stack         : context-switch stack (ctx)
 * Algorithms (algo_t in common.h): FCFS, SJF (non-preemptive), Round Robin.
 * Threads: main = receiver (reads UI commands), worker = CPU thread (for CMD_RUN).
 * Every command gets zero or more RESP_STATE/RESP_ERROR messages followed by RESP_END.
 * Start Core FIRST: it creates the three queues. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "common.h"

#define CTX_STACK       256
#define SEG_MAX         128
#define MAX_TIME        100000
#define SEND_TIMEOUT_MS 100     /* max wait if a queue is full before dropping */

enum { ST_READY, ST_RUNNING, ST_DONE };
static const char *ALGO_NAMES[] = { "FCFS", "SJF", "RR" };

typedef struct {                       /* Process Control Block */
    char name[MAX_NAME_LEN];
    int burst, arrival, remaining, start, finish, state;
} pcb_t;

typedef struct {
    pcb_t p[MAX_PROCESSES]; int n;     /* process table                       */
    int rq[MAX_PROCESSES], rq_len;     /* ready queue (indices into p[])      */
    int running, last_run;             /* index or -1                         */
    int time, busy, done;
    int algo, quantum, qleft;
    int ctx[CTX_STACK], ctx_sp;        /* context-switch stack                */
    int seg_proc[SEG_MAX], seg_from[SEG_MAX], seg_to[SEG_MAX], nseg, seg_trunc; /* Gantt */
} sched_t;

static sched_t s;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  wake = PTHREAD_COND_INITIALIZER;
static int auto_run = 0, quitting = 0;
static mqd_t q_cmd, q_resp, q_log;
static unsigned seq_no = 0, dropped = 0;

/* ---------- IPC helpers (call with lock held) ---------- */
static void send_q(mqd_t q, message_t *m) {
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_nsec += SEND_TIMEOUT_MS * 1000000L;
    ts.tv_sec  += ts.tv_nsec / 1000000000L; ts.tv_nsec %= 1000000000L;
    m->seq = (int32_t)++seq_no;
    if (mq_timedsend(q, (const char *)m, sizeof *m, MQ_PRIORITY, &ts) < 0) {
        dropped++;
        if (errno != ETIMEDOUT) perror("mq_timedsend");
    }
}
static void reply(int type, const char *fmt, ...) {
    message_t m; memset(&m, 0, sizeof m); m.type = type;
    if (fmt) { va_list ap; va_start(ap, fmt); vsnprintf(m.payload.text, MAX_TEXT_LEN, fmt, ap); va_end(ap); }
    send_q(q_resp, &m);
}
static void log_text(int type, const char *fmt, ...) {
    message_t m; memset(&m, 0, sizeof m); m.type = type;
    if (fmt) { va_list ap; va_start(ap, fmt); vsnprintf(m.payload.text, MAX_TEXT_LEN, fmt, ap); va_end(ap); }
    send_q(q_log, &m);
}
static void log_event(int type, const char *proc, const char *next, int t, int wait) {
    message_t m; memset(&m, 0, sizeof m); m.type = type;
    strncpy(m.payload.event.proc, proc, MAX_NAME_LEN - 1);
    if (next) strncpy(m.payload.event.next, next, MAX_NAME_LEN - 1);
    m.payload.event.time = t; m.payload.event.waiting_time = wait;
    send_q(q_log, &m);
}
static void reply_error(const char *fmt, ...) {      /* error -> UI (RESP_ERROR) and Logger (EVT_ERROR) */
    char buf[MAX_TEXT_LEN]; va_list ap;
    va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    reply(RESP_ERROR, "%s", buf);
    log_text(EVT_ERROR, "%s", buf);
}

/* ---------- Scheduler ---------- */
static void sched_reset(void) {
    memset(&s, 0, sizeof s);
    s.running = s.last_run = -1; s.algo = ALGO_FCFS; s.quantum = DEFAULT_QUANTUM;
}

static int pick(void) {                   /* choose + remove next process from ready queue */
    int bi = 0;
    if (s.algo == ALGO_SJF)
        for (int k = 1; k < s.rq_len; k++)
            if (s.p[s.rq[k]].remaining < s.p[s.rq[bi]].remaining) bi = k;
    int idx = s.rq[bi];
    memmove(&s.rq[bi], &s.rq[bi + 1], (s.rq_len - bi - 1) * sizeof(int));
    s.rq_len--;
    return idx;
}

/* One time unit. Returns 0 = continue, 1 = all finished, -1 = error (already reported). */
static int sched_tick(void) {
    if (s.n == 0 || s.done == s.n) { reply_error("no processes to run"); return -1; }
    if (s.time >= MAX_TIME)        { reply_error("time limit reached");   return -1; }

    if (s.running < 0) {                                   /* dispatch */
        int i = pick(); pcb_t *p = &s.p[i];
        s.running = i; p->state = ST_RUNNING;
        if (p->start < 0) { p->start = s.time; log_event(EVT_START, p->name, NULL, s.time, 0); }
        if (s.last_run >= 0 && s.last_run != i)
            log_event(EVT_SWITCH, s.p[s.last_run].name, p->name, s.time, 0);
        s.last_run = i;
        if (s.ctx_sp == CTX_STACK) { memmove(s.ctx, s.ctx + 1, (CTX_STACK - 1) * sizeof(int)); s.ctx_sp--; }
        s.ctx[s.ctx_sp++] = i;
        s.qleft = s.quantum;
    }
    pcb_t *p = &s.p[s.running];
    p->remaining--; s.qleft--; s.busy++;

    int k = s.nseg - 1;                                    /* Gantt segment */
    if (k >= 0 && s.seg_proc[k] == s.running && s.seg_to[k] == s.time) s.seg_to[k] = s.time + 1;
    else if (s.nseg < SEG_MAX) { s.seg_proc[s.nseg] = s.running; s.seg_from[s.nseg] = s.time; s.seg_to[s.nseg++] = s.time + 1; }
    else s.seg_trunc = 1;

    s.time++;
    if (p->remaining == 0) {
        p->finish = s.time; p->state = ST_DONE; s.done++;
        log_event(EVT_FINISH, p->name, NULL, s.time, p->finish - p->arrival - p->burst);
        s.running = -1;
    } else if (s.algo == ALGO_RR && s.qleft == 0) {
        p->state = ST_READY; s.rq[s.rq_len++] = s.running; s.running = -1;
    }
    return s.done == s.n;
}

static void send_state(void) {
    char buf[MAX_TEXT_LEN]; int off;
    if (s.running >= 0) off = snprintf(buf, sizeof buf, "t=%d run=%s(rem %d) ready=", s.time, s.p[s.running].name, s.p[s.running].remaining);
    else                off = snprintf(buf, sizeof buf, "t=%d run=idle ready=", s.time);
    for (int k = 0; k < s.rq_len && off < (int)sizeof buf - 24; k++)
        off += snprintf(buf + off, sizeof buf - off, "%s%s", k ? "," : "", s.p[s.rq[k]].name);
    if (off < (int)sizeof buf - 24) snprintf(buf + off, sizeof buf - off, " done=%d/%d", s.done, s.n);
    reply(RESP_STATE, "%s", buf);
}

static void finish_report(void) {                          /* timeline + final statistics */
    char g[MAX_TEXT_LEN]; int off = snprintf(g, sizeof g, "Gantt:");
    for (int i = 0; i < s.nseg && off < (int)sizeof g - 24; i++)
        off += snprintf(g + off, sizeof g - off, " %s[%d-%d]", s.p[s.seg_proc[i]].name, s.seg_from[i], s.seg_to[i]);
    if (s.seg_trunc && off < (int)sizeof g - 5) snprintf(g + off, sizeof g - off, " ...");
    reply(RESP_STATE, "%s", g);

    double w = 0, t = 0; int d = 0;
    for (int i = 0; i < s.n; i++) if (s.p[i].state == ST_DONE) {
        int tat = s.p[i].finish - s.p[i].arrival; t += tat; w += tat - s.p[i].burst; d++;
    }
    if (!d) d = 1;
    char st[MAX_TEXT_LEN];
    snprintf(st, sizeof st, "avg_wait=%.2f avg_tat=%.2f util=%.1f%% done=%d/%d",
             w / d, t / d, s.time ? 100.0 * s.busy / s.time : 0.0, s.done, s.n);
    reply(RESP_STATE, "%s", st);
    log_text(EVT_STATS, "%s (%s)", st, ALGO_NAMES[s.algo]);
}

/* ---------- CPU worker thread: runs the simulation for CMD_RUN ---------- */
static void *cpu_worker(void *arg) {
    (void)arg;
    pthread_mutex_lock(&lock);
    while (!quitting) {
        while (!auto_run && !quitting) pthread_cond_wait(&wake, &lock);
        while (auto_run && !quitting) {
            int r = sched_tick();
            if (r != 0) {
                auto_run = 0;
                if (r == 1) finish_report();
                reply(RESP_END, NULL);                     /* RUN is complete */
                break;
            }
            pthread_mutex_unlock(&lock);                   /* let the receiver in */
            sched_yield();
            pthread_mutex_lock(&lock);
        }
    }
    pthread_mutex_unlock(&lock);
    return NULL;
}

/* ---------- command handlers (lock held) ---------- */
static void do_add(const message_t *m) {
    char name[MAX_NAME_LEN]; strncpy(name, m->payload.add.name, MAX_NAME_LEN - 1); name[MAX_NAME_LEN - 1] = '\0';
    int burst = m->payload.add.burst;
    if (name[0] == '\0')                 { reply_error("empty process name"); return; }
    if (burst < 1 || burst > MAX_BURST)  { reply_error("invalid burst time %d (allowed 1..%d)", burst, MAX_BURST); return; }
    if (s.n >= MAX_PROCESSES)            { reply_error("process table full (max %d)", MAX_PROCESSES); return; }
    for (int i = 0; i < s.n; i++) if (strcmp(s.p[i].name, name) == 0) { reply_error("duplicate process name %s", name); return; }
    pcb_t *p = &s.p[s.n];
    memset(p, 0, sizeof *p);
    strcpy(p->name, name);
    p->burst = p->remaining = burst; p->arrival = s.time; p->start = -1; p->state = ST_READY;
    s.rq[s.rq_len++] = s.n++;
    reply(RESP_STATE, "added %s (burst %d)", name, burst);
}
static void do_algo(const message_t *m) {
    int id = m->payload.algo.id;
    if (id < ALGO_FCFS || id > ALGO_RR) { reply_error("unknown algorithm %d", id); return; }
    if (s.time > 0) { reply_error("cannot change algorithm after start (reset first)"); return; }
    s.algo = id; reply(RESP_STATE, "algorithm = %s", ALGO_NAMES[id]);
}
static void do_quantum(const message_t *m) {
    int v = m->payload.quantum.value;
    if (v < 1 || v > MAX_QUANTUM) { reply_error("invalid quantum %d (allowed 1..%d)", v, MAX_QUANTUM); return; }
    if (s.time > 0) { reply_error("cannot change quantum after start (reset first)"); return; }
    s.quantum = v; reply(RESP_STATE, "quantum = %d", v);
}

static mqd_t open_q(const char *name) {
    struct mq_attr a = { .mq_maxmsg = MQ_MAX_MESSAGES, .mq_msgsize = MSG_SIZE }, g;
    mqd_t q = mq_open(name, O_CREAT | O_RDWR, 0644, &a);
    if (q == (mqd_t)-1) { perror(name); exit(1); }
    if (mq_getattr(q, &g) == 0 && g.mq_msgsize != MSG_SIZE) {
        fprintf(stderr, "%s already exists with another message size. Run: rm /dev/mqueue/*   then start again\n", name);
        exit(1);
    }
    return q;
}

int main(void) {
    q_cmd = open_q(QUEUE_CMD); q_resp = open_q(QUEUE_RESP); q_log = open_q(QUEUE_LOG);
    sched_reset();
    pthread_t worker; pthread_create(&worker, NULL, cpu_worker, NULL);
    fprintf(stderr, "[core] ready\n");

    int done = 0;
    while (!done) {
        message_t m;
        ssize_t n = mq_receive(q_cmd, (char *)&m, sizeof m, NULL);
        if (n < 0) { if (errno == EINTR) continue; perror("mq_receive"); break; }

        pthread_mutex_lock(&lock);
        if (auto_run && m.type != CMD_STATUS && m.type != CMD_QUIT) {
            reply_error("simulation is running"); reply(RESP_END, NULL);
        } else switch (m.type) {
        case CMD_ADD:     do_add(&m);     reply(RESP_END, NULL); break;
        case CMD_ALGO:    do_algo(&m);    reply(RESP_END, NULL); break;
        case CMD_QUANTUM: do_quantum(&m); reply(RESP_END, NULL); break;
        case CMD_STEP: {
            int r = sched_tick();
            if (r >= 0) send_state();
            if (r == 1) finish_report();
            reply(RESP_END, NULL); break; }
        case CMD_RUN:
            if (s.n == 0 || s.done == s.n) { reply_error("no processes to run"); reply(RESP_END, NULL); }
            else { auto_run = 1; pthread_cond_signal(&wake); }      /* worker sends RESP_END when finished */
            break;
        case CMD_STATUS:  send_state(); reply(RESP_END, NULL); break;
        case CMD_RESET:   sched_reset(); reply(RESP_STATE, "reset"); reply(RESP_END, NULL); break;
        case CMD_QUIT:    quitting = 1; auto_run = 0; done = 1; pthread_cond_broadcast(&wake);
                          log_text(EVT_QUIT, NULL); reply(RESP_STATE, "bye"); reply(RESP_END, NULL); break;
        default:          reply_error("unknown command %d", m.type); reply(RESP_END, NULL);
        }
        pthread_mutex_unlock(&lock);
    }

    pthread_join(worker, NULL);
    if (dropped) fprintf(stderr, "[core] warning: %u message(s) dropped (a queue stayed full)\n", dropped);
    mq_close(q_cmd); mq_close(q_resp); mq_close(q_log);
    mq_unlink(QUEUE_CMD); mq_unlink(QUEUE_RESP); mq_unlink(QUEUE_LOG);
    return 0;
}
