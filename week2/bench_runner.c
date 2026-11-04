/* bench_runner.c - runs a command N times and measures it (whole process tree).
 *   ./bench_runner <label> <runs> <command> [args...]
 * Output (CSV, no header): label,run,wall_s,user_s,sys_s,cpu_pct,peak_rss_kb
 *   wall_s      elapsed time (CLOCK_MONOTONIC)
 *   user_s/sys_s CPU time of the command AND all children it waited for (wait4 rusage)
 *   cpu_pct     (user+sys)/wall*100   (can exceed 100 on several cores)
 *   peak_rss_kb highest SUM of VmRSS over the command and all its descendants, sampled every 10 ms
 * stdin/stdout of the command go to /dev/null so terminal speed does not disturb the result.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAXP 8192

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static long read_ppid(int pid)
{
    char path[64], line[1024];
    snprintf(path, sizeof path, "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(line, sizeof line, f)) { fclose(f); return -1; }
    fclose(f);
    char *p = strrchr(line, ')');                /* process name may contain spaces/parentheses */
    if (!p) return -1;
    char st; long ppid;
    if (sscanf(p + 2, "%c %ld", &st, &ppid) != 2) return -1;
    return ppid;
}

static long read_rss_kb(int pid)
{
    char path[64], line[256];
    snprintf(path, sizeof path, "/proc/%d/status", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    long kb = 0;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "VmRSS: %ld kB", &kb) == 1) break;
    fclose(f);
    return kb;
}

/* sum of VmRSS of root and all its descendants */
static long tree_rss_kb(int root)
{
    static int pid[MAXP], ppid[MAXP], in[MAXP];
    int n = 0;
    DIR *d = opendir("/proc");
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < MAXP) {
        if (!isdigit((unsigned char)e->d_name[0])) continue;
        int p = atoi(e->d_name);
        long pp = read_ppid(p);
        if (pp < 0) continue;
        pid[n] = p; ppid[n] = (int)pp; in[n] = (p == root); n++;
    }
    closedir(d);
    for (int changed = 1; changed;) {            /* propagate "is descendant" down the tree */
        changed = 0;
        for (int i = 0; i < n; i++) {
            if (in[i]) continue;
            for (int j = 0; j < n; j++)
                if (in[j] && pid[j] == ppid[i]) { in[i] = 1; changed = 1; break; }
        }
    }
    long sum = 0;
    for (int i = 0; i < n; i++) if (in[i]) sum += read_rss_kb(pid[i]);
    return sum;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s <label> <runs> <command> [args...]\n", argv[0]);
        return 2;
    }
    const char *label = argv[1];
    int runs = atoi(argv[2]);

    for (int run = 1; run <= runs; run++) {
        double t0 = now_s();
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return 1; }
        if (pid == 0) {
            int dn = open("/dev/null", O_RDWR);
            dup2(dn, 0); dup2(dn, 1);
            execvp(argv[3], &argv[3]);
            perror("exec");
            _exit(127);
        }
        long peak = 0;
        int st = 0;
        struct rusage ru;
        for (;;) {
            pid_t r = wait4(pid, &st, WNOHANG, &ru);
            if (r == pid) break;
            if (r < 0) { perror("wait4"); return 1; }
            long rss = tree_rss_kb(pid);
            if (rss > peak) peak = rss;
            usleep(10 * 1000);
        }
        double wall = now_s() - t0;
        double u = ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6;
        double s = ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6;
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
            fprintf(stderr, "warning: run %d of '%s' did not exit with code 0\n", run, label);
        printf("%s,%d,%.4f,%.4f,%.4f,%.1f,%ld\n", label, run, wall, u, s,
               wall > 0 ? (u + s) / wall * 100.0 : 0.0, peak);
        fflush(stdout);
    }
    return 0;
}
