#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "common.h"

static void set_error(char *err, size_t size, const char *message)
{
    if (err != NULL && size > 0)
        snprintf(err, size, "%s", message);
}

static int write_all(int fd, const void *buffer, size_t size)
{
    const char *p = buffer;
    size_t done = 0;

    while (done < size) {
        ssize_t n = write(fd, p + done, size - done);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (n == 0)
            return -1;

        done += (size_t)n;
    }

    return 0;
}

static int read_all(int fd, void *buffer, size_t size)
{
    char *p = buffer;
    size_t done = 0;

    while (done < size) {
        ssize_t n = read(fd, p + done, size - done);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (n == 0)
            return -1;

        done += (size_t)n;
    }

    return 0;
}

static int wait_for_child(pid_t pid, int *status)
{
    pid_t result;

    do {
        result = waitpid(pid, status, 0);
    } while (result < 0 && errno == EINTR);

    return result == pid ? 0 : -1;
}

int ipc_run_algorithms(const int *pages, int n, int frames, int mask,
                       result_t *results, int *count,
                       char *err, size_t errsize)
{
    int total = 0;
    const int valid_mask =
        ALGO_BIT(ALGO_FIFO) |
        ALGO_BIT(ALGO_LRU) |
        ALGO_BIT(ALGO_OPTIMAL);

    if (count != NULL)
        *count = 0;

    if (pages == NULL || results == NULL || count == NULL ||
        n < 1 || n > MAX_PAGES ||
        frames < 1 || frames > MAX_FRAMES ||
        mask == 0 || (mask & ~valid_mask) != 0) {
        set_error(err, errsize, "Invalid IPC arguments.");
        return -1;
    }

    for (int a = 0; a < NUM_ALGOS; a++) {
        int pipefd[2];
        int status;
        pid_t pid;
        result_t child_result;

        if (!(mask & ALGO_BIT(a)))
            continue;

        if (pipe(pipefd) < 0) {
            set_error(err, errsize, "Could not create pipe.");
            return -1;
        }

        pid = fork();

        if (pid < 0) {
            close(pipefd[0]);
            close(pipefd[1]);
            set_error(err, errsize, "Could not create worker process.");
            return -1;
        }

        if (pid == 0) {
            /* Child process */
            close(pipefd[0]);

            memset(&child_result, 0, sizeof(child_result));
            child_result.algo = (algo_id_t)a;
            child_result.nsteps = n;

            switch (a) {
                case ALGO_FIFO:
                    fifo(pages, n, frames, child_result.steps);
                    break;

                case ALGO_LRU:
                    lru(pages, n, frames, child_result.steps);
                    break;

                case ALGO_OPTIMAL:
                    optimal(pages, n, frames, child_result.steps);
                    break;

                default:
                    close(pipefd[1]);
                    _exit(1);
            }

            int write_status =
                write_all(pipefd[1], &child_result,
                          sizeof(child_result));

            close(pipefd[1]);
            _exit(write_status == 0 ? 0 : 1);
        }

        /* Parent process */
        close(pipefd[1]);

        int read_status =
            read_all(pipefd[0], &results[total], sizeof(result_t));

        close(pipefd[0]);

        if (wait_for_child(pid, &status) < 0) {
            set_error(err, errsize, "Could not wait for worker process.");
            return -1;
        }

        if (read_status < 0 || !WIFEXITED(status) ||
            WEXITSTATUS(status) != 0) {
            set_error(err, errsize, "Worker process failed.");
            return -1;
        }

        total++;
    }

    if (total == 0) {
        set_error(err, errsize, "No algorithms were selected.");
        return -1;
    }

    *count = total;
    set_error(err, errsize, "");
    return 0;
}
