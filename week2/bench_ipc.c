/* bench_ipc.c - latency and throughput of the four IPC techniques (64-byte messages).
 *   ./bench_ipc [latency_round_trips=50000] [throughput_messages=200000]
 * Output (CSV): mechanism,avg_oneway_us,min_oneway_us,p99_oneway_us,throughput_msgs_per_s
 * one-way latency = round-trip time / 2 (ping-pong between a parent and a child process).
 * Shared memory uses a bounded buffer with process-shared (unnamed) semaphores placed inside the segment.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MSG    64
#define WARMUP 1000
#define RING   256

static int N_LAT = 50000, N_THR = 200000;

typedef struct {
    const char *name;
    void (*setup)(void);          /* before fork: create the IPC objects */
    void (*open_parent)(void);    /* after fork, in the parent */
    void (*open_child)(void);     /* after fork, in the child */
    void (*snd)(int dir, const char *m);   /* dir 0: parent->child, 1: child->parent */
    void (*rcv)(int dir, char *m);
    void (*cleanup)(void);
} mech_t;

static double now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static void die(const char *w) { perror(w); exit(1); }

static void read_full(int fd, char *b, size_t n)
{
    size_t o = 0;
    while (o < n) {
        ssize_t r = read(fd, b + o, n - o);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) die("read");
        o += (size_t)r;
    }
}
static void write_full(int fd, const char *b, size_t n)
{
    size_t o = 0;
    while (o < n) {
        ssize_t w = write(fd, b + o, n - o);
        if (w < 0 && errno == EINTR) continue;
        if (w < 0) die("write");
        o += (size_t)w;
    }
}

/* ---------- 1. POSIX message queue ---------- */
static mqd_t mq[2];
static void mq_setup(void)
{
    struct mq_attr at = { .mq_maxmsg = 10, .mq_msgsize = MSG };
    mq_unlink("/bench_q0"); mq_unlink("/bench_q1");
    mq[0] = mq_open("/bench_q0", O_CREAT | O_RDWR, 0600, &at);
    mq[1] = mq_open("/bench_q1", O_CREAT | O_RDWR, 0600, &at);
    if (mq[0] == (mqd_t)-1 || mq[1] == (mqd_t)-1) die("mq_open");
}
static void mq_noop(void) {}
static void mq_snd(int d, const char *m) { if (mq_send(mq[d], m, MSG, 0) < 0) die("mq_send"); }
static void mq_rcv(int d, char *m) { if (mq_receive(mq[d], m, MSG, NULL) < 0) die("mq_receive"); }
static void mq_clean(void) { mq_unlink("/bench_q0"); mq_unlink("/bench_q1"); }

/* ---------- 2. POSIX shared memory + semaphores ---------- */
struct shm_seg {
    sem_t empty[2], full[2];
    char  ring[2][RING][MSG];
};
static struct shm_seg *seg;
static unsigned sidx[2], ridx[2];                /* process-local: one sender and one receiver per direction */
static void shm_setup(void)
{
    shm_unlink("/bench_shm");
    int fd = shm_open("/bench_shm", O_CREAT | O_RDWR, 0600);
    if (fd < 0 || ftruncate(fd, sizeof(struct shm_seg)) < 0) die("shm_open");
    seg = mmap(NULL, sizeof *seg, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (seg == MAP_FAILED) die("mmap");
    close(fd);
    for (int d = 0; d < 2; d++) { sem_init(&seg->empty[d], 1, RING); sem_init(&seg->full[d], 1, 0); }
}
static void shm_snd(int d, const char *m)
{
    sem_wait(&seg->empty[d]);
    memcpy(seg->ring[d][sidx[d]++ % RING], m, MSG);
    sem_post(&seg->full[d]);
}
static void shm_rcv(int d, char *m)
{
    sem_wait(&seg->full[d]);
    memcpy(m, seg->ring[d][ridx[d]++ % RING], MSG);
    sem_post(&seg->empty[d]);
}
static void shm_clean(void) { shm_unlink("/bench_shm"); }

/* ---------- 3. Named pipe (FIFO) ---------- */
static const char *fp[2] = { "/tmp/bench_fifo0", "/tmp/bench_fifo1" };
static int ff[2];
static void fifo_setup(void)
{
    for (int i = 0; i < 2; i++) { unlink(fp[i]); if (mkfifo(fp[i], 0600) < 0) die("mkfifo"); }
}
static void fifo_parent(void) { ff[0] = open(fp[0], O_WRONLY); ff[1] = open(fp[1], O_RDONLY); }
static void fifo_child(void)  { ff[0] = open(fp[0], O_RDONLY); ff[1] = open(fp[1], O_WRONLY); }
static void fifo_snd(int d, const char *m) { write_full(ff[d], m, MSG); }
static void fifo_rcv(int d, char *m) { read_full(ff[d], m, MSG); }
static void fifo_clean(void) { unlink(fp[0]); unlink(fp[1]); }

/* ---------- 4. Unix domain socket ---------- */
static int sp[2], sfd[2];
static void sock_setup(void) { if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) < 0) die("socketpair"); }
static void sock_parent(void) { close(sp[1]); sfd[0] = sfd[1] = sp[0]; }
static void sock_child(void)  { close(sp[0]); sfd[0] = sfd[1] = sp[1]; }
static void sock_snd(int d, const char *m) { write_full(sfd[d], m, MSG); }
static void sock_rcv(int d, char *m) { read_full(sfd[d], m, MSG); }

static mech_t mechs[] = {
    { "message_queue",       mq_setup,   mq_noop,     mq_noop,   mq_snd,   mq_rcv,   mq_clean },
    { "shared_memory_sem",   shm_setup,  mq_noop,     mq_noop,   shm_snd,  shm_rcv,  shm_clean },
    { "named_pipe",          fifo_setup, fifo_parent, fifo_child, fifo_snd, fifo_rcv, fifo_clean },
    { "unix_domain_socket",  sock_setup, sock_parent, sock_child, sock_snd, sock_rcv, mq_noop },
};

static int cmp_d(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void run(mech_t *m)
{
    char buf[MSG];
    memset(buf, 'x', MSG);
    sidx[0] = sidx[1] = ridx[0] = ridx[1] = 0;
    m->setup();
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) die("fork");
    if (pid == 0) {                               /* ---- child: echo, then sink ---- */
        m->open_child();                          /* FIFO opens block until the peer opens too */
        for (int i = 0; i < N_LAT + WARMUP; i++) { m->rcv(0, buf); m->snd(1, buf); }
        for (int i = 0; i < N_THR; i++) m->rcv(0, buf);
        m->snd(1, buf);                           /* ack */
        _exit(0);
    }
    m->open_parent();

    double *rtt = malloc(sizeof(double) * (size_t)N_LAT);
    for (int i = 0; i < WARMUP; i++) { m->snd(0, buf); m->rcv(1, buf); }
    for (int i = 0; i < N_LAT; i++) {
        double t0 = now_us();
        m->snd(0, buf);
        m->rcv(1, buf);
        rtt[i] = now_us() - t0;
    }
    double t0 = now_us();
    for (int i = 0; i < N_THR; i++) m->snd(0, buf);
    m->rcv(1, buf);
    double secs = (now_us() - t0) / 1e6;

    double sum = 0;
    for (int i = 0; i < N_LAT; i++) sum += rtt[i];
    qsort(rtt, (size_t)N_LAT, sizeof(double), cmp_d);
    printf("%s,%.3f,%.3f,%.3f,%.0f\n", m->name, sum / N_LAT / 2, rtt[0] / 2,
           rtt[(int)(N_LAT * 0.99)] / 2, N_THR / secs);
    free(rtt);
    waitpid(pid, NULL, 0);
    m->cleanup();
}

int main(int argc, char **argv)
{
    if (argc > 1) N_LAT = atoi(argv[1]);
    if (argc > 2) N_THR = atoi(argv[2]);
    if (N_LAT < 100) N_LAT = 100;
    printf("mechanism,avg_oneway_us,min_oneway_us,p99_oneway_us,throughput_msgs_per_s\n");
    for (size_t i = 0; i < sizeof mechs / sizeof mechs[0]; i++) run(&mechs[i]);
    return 0;
}
