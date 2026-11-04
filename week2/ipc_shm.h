#ifndef IPC_SHM_H
#define IPC_SHM_H

#include <stddef.h>

struct sim_state {
    unsigned long tick;
    int running_pid;
    int queue_len;
    int queue[64];
    size_t mem_used;
    size_t mem_total;
    int stack_depth;
    int status; /* 0: Stopped, 1: Running, 2: Paused */
};

/* POSIX Shared Memory and Semaphore Functions */
int shm_state_create(void);
int shm_state_open(void);
int shm_publish_state(const struct sim_state *st);
int shm_read_state(struct sim_state *st);
int shm_state_close(void);
int shm_state_unlink(void);

#endif /* IPC_SHM_H */
