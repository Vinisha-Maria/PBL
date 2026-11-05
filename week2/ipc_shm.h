#ifndef IPC_SHM_H
#define IPC_SHM_H

#include "sim_types.h"

/* POSIX Shared Memory and Semaphore Functions */
int shm_state_create(void);
int shm_state_open(void);
int shm_publish_state(const struct sim_state *st);
int shm_read_state(struct sim_state *st);
int shm_state_close(void);
int shm_state_unlink(void);

#endif /* IPC_SHM_H */
