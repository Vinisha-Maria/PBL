#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#include "ipc_shm.h"
#include "ipc_mq.h"
#include "ipc_fifo.h"

/* Simulator state and synchronization globals */
static struct sim_state current_state;
static pthread_mutex_t sim_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sim_cond = PTHREAD_COND_INITIALIZER;
static volatile int keep_running = 1;

/* Benchmark function required by Task 7 */
void run_shm_benchmark(void) {
    struct sim_state test_st;
    struct timespec start, end;
    long long elapsed_ns;
    int i;

    memset(&test_st, 0, sizeof(test_st));
    test_st.tick = 12345;
    test_st.running_pid = 42;
    test_st.status = 1;

    printf("[BENCHMARK] Starting 1,000,000 publish/read iterations...\n");

    if (clock_gettime(CLOCK_MONOTONIC, &start) == -1) {
        perror("clock_gettime start");
        return;
    }

    for (i = 0; i < 1000000; i++) {
        shm_publish_state(&test_st);
        shm_read_state(&test_st);
    }

    if (clock_gettime(CLOCK_MONOTONIC, &end) == -1) {
        perror("clock_gettime end");
        return;
    }

    elapsed_ns = (end.tv_sec - start.tv_sec) * 1000000000LL + (end.tv_nsec - start.tv_nsec);
    printf("[BENCHMARK] Completed 1,000,000 iterations in %.3f ms (%.2f ns per roundtrip)\n",
           (double)elapsed_ns / 1000000.0, (double)elapsed_ns / 2000000.0);
}

/* Signal handler for graceful shutdown on SIGINT */
void handle_sigint(int sig) {
    (void)sig;
    keep_running = 0;
    pthread_cond_broadcast(&sim_cond);
}

/* Execution Thread: Runs CPU simulation ticks and publishes state */
void *execution_thread_func(void *arg) {
    (void)arg;
    log_write("INFO", "CORE", "Execution thread started");

    while (keep_running) {
        pthread_mutex_lock(&sim_mutex);
        while (current_state.status != 1 && keep_running) {
            pthread_cond_wait(&sim_cond, &sim_mutex);
        }

        if (!keep_running) {
            pthread_mutex_unlock(&sim_mutex);
            break;
        }

        /* --- Simulated CPU / Memory / Stack / Queue Logic --- */
        current_state.tick++;
        current_state.mem_used = 2048 + (current_state.tick % 512);
        current_state.mem_total = 16384;
        current_state.stack_depth = (int)(current_state.tick % 16);
        
        if (current_state.queue_len < 64) {
            current_state.queue[current_state.queue_len++] = (int)(current_state.tick % 100);
            log_write("INFO", "QUEUE", "Queue change: item added");
        }

        pthread_mutex_unlock(&sim_mutex);

        /* Publish state to shared memory */
        if (shm_publish_state(&current_state) == -1) {
            log_write("ERROR", "SHM", "Failed to publish state snapshot");
        }

        log_write("INFO", "CORE", "Process scheduled and ticked");
        usleep(100000); /* 100ms tick interval */
    }

    log_write("INFO", "CORE", "Execution thread exiting");
    return NULL;
}

/* Command Thread: Receives commands from UI via Message Queue */
void *command_thread_func(void *arg) {
    (void)arg;
    struct sim_cmd cmd;

    log_write("INFO", "CORE", "Command thread started");

    while (keep_running) {
        if (mq_cmd_receive(&cmd, 100) == -1) {
            if (errno == EINTR) continue;
            usleep(10000);
            continue;
        }

        pthread_mutex_lock(&sim_mutex);
        switch (cmd.type) {
            case CMD_START:
                current_state.status = 1;
                log_write("INFO", "CMD", "Command received: START");
                pthread_cond_signal(&sim_cond);
                break;
            case CMD_PAUSE:
                current_state.status = 2;
                log_write("INFO", "CMD", "Command received: PAUSE");
                break;
            case CMD_RESUME:
                current_state.status = 1;
                log_write("INFO", "CMD", "Command received: RESUME");
                pthread_cond_signal(&sim_cond);
                break;
            case CMD_STEP:
                current_state.status = 1;
                log_write("INFO", "CMD", "Command received: STEP");
                pthread_cond_signal(&sim_cond);
                /* Sleep briefly then pause again for single-step behavior */
                usleep(50000);
                current_state.status = 2;
                break;
            case CMD_ADD_PROCESS:
                current_state.running_pid = cmd.arg;
                log_write("INFO", "CMD", "Command received: ADD_PROCESS");
                break;
            case CMD_RESET:
                current_state.tick = 0;
                current_state.queue_len = 0;
                current_state.stack_depth = 0;
                log_write("INFO", "CMD", "Command received: RESET");
                break;
            case CMD_QUIT:
                keep_running = 0;
                log_write("INFO", "CMD", "Command received: QUIT");
                pthread_cond_broadcast(&sim_cond);
                break;
            default:
                log_write("WARN", "CMD", "Unknown command type received");
                break;
        }
        pthread_mutex_unlock(&sim_mutex);
    }

    log_write("INFO", "CORE", "Command thread exiting");
    return NULL;
}

int main(void) {
    pthread_t exec_thread, cmd_thread;

    /* Setup signal handling */
    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    /* Initialize subsystems */
    if (log_open_writer() == -1) {
        fprintf(stderr, "Initialization error: Logger failed\n");
    }
    if (mq_cmd_open_receiver() == -1) {
        fprintf(stderr, "Initialization error: Message Queue failed\n");
    }
    if (shm_state_create() == -1) {
        fprintf(stderr, "Initialization error: Shared Memory failed\n");
        exit(EXIT_FAILURE);
    }

    memset(&current_state, 0, sizeof(current_state));
    current_state.status = 0; /* Stopped by default */

    /* Run the benchmark test */
    run_shm_benchmark();

    /* Create threads */
    pthread_create(&exec_thread, NULL, execution_thread_func, NULL);
    pthread_create(&cmd_thread, NULL, command_thread_func, NULL);

    log_write("INFO", "CORE", "Core process fully initialized and running");

    /* Main thread waits for shutdown signal */
    while (keep_running) {
        pause();
    }

    /* Cleanup and unlink resources */
    log_write("INFO", "CORE", "Shutting down Core process and cleaning up IPC resources");
    
    pthread_join(exec_thread, NULL);
    pthread_join(cmd_thread, NULL);

    shm_state_unlink();
    mq_cmd_close();
    mq_cmd_unlink();
    log_close();

    printf("Core process exited cleanly.\n");
    return 0;
}
