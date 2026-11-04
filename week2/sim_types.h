/* sim_types.h - the ONE shared definition of every IPC name and data format.
 * UI, Core and Logger must include this file instead of redefining anything.
 * Owner: Mia (Team Leader). Do not change without telling the whole team.
 */
#ifndef SIM_TYPES_H
#define SIM_TYPES_H

#include <stddef.h>

/* ---- IPC object names ---- */
#define SIM_MQ_NAME        "/sim_cmd"          /* POSIX message queue   (Rian)   */
#define SIM_SHM_NAME       "/sim_state"        /* POSIX shared memory   (Varun)  */
#define SIM_SEM_NAME       "/sim_state_lock"   /* named semaphore       (Varun)  */
#define SIM_FIFO_PATH      "/tmp/sim_log_fifo" /* named pipe            (Rajath) */
/* control socket path SIM_CTRL_PATH lives in ipc_sock.h (Mia) */

/* ---- UI -> Core commands ---- */
enum sim_cmd_type {
    CMD_START = 1, CMD_PAUSE = 2, CMD_RESUME = 3, CMD_STEP = 4,
    CMD_ADD_PROCESS = 5, CMD_RESET = 6, CMD_QUIT = 7
};

struct sim_cmd {
    int      type;      /* enum sim_cmd_type */
    int      arg;
    unsigned seq;
};

/* ---- Core -> UI state snapshot ---- */
#define SIM_QUEUE_MAX 64

struct sim_state {
    unsigned long tick;
    int           running_pid;
    int           queue_len;
    int           queue[SIM_QUEUE_MAX];
    size_t        mem_used;
    size_t        mem_total;
    int           stack_depth;
    int           status;
};

/* ---- Core -> Logger line: "timestamp|LEVEL|SOURCE|message\n", LEVEL = INFO | WARN | ERROR ---- */
#define SIM_LOG_LINE_MAX 512

/* Catch "two students, two different structs" at compile time. */
_Static_assert(sizeof(struct sim_cmd) <= 64, "sim_cmd must fit one 64-byte mq message");
_Static_assert(SIM_LOG_LINE_MAX <= 4096, "log line must stay below PIPE_BUF so FIFO writes are atomic");

#endif
