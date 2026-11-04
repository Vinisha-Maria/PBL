#include "ipc_shm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <semaphore.h>
#include <errno.h>

#define SHM_NAME "/sim_state"
#define SEM_NAME "/sim_state_lock"

static int shm_fd = -1;
static struct sim_state *shared_state_ptr = (void *)-1;
static sem_t *state_sem = SEM_FAILED;

int shm_state_create(void) {
    /* Clean up any stale instances from previous crashed runs */
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_NAME);

    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_state_create: shm_open");
        return -1;
    }

    if (ftruncate(shm_fd, sizeof(struct sim_state)) == -1) {
        perror("shm_state_create: ftruncate");
        close(shm_fd);
        shm_fd = -1;
        return -1;
    }

    shared_state_ptr = mmap(NULL, sizeof(struct sim_state), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_state_ptr == MAP_FAILED) {
        perror("shm_state_create: mmap");
        close(shm_fd);
        shm_fd = -1;
        return -1;
    }

    state_sem = sem_open(SEM_NAME, O_CREAT | O_RDWR, 0666, 1);
    if (state_sem == SEM_FAILED) {
        perror("shm_state_create: sem_open");
        munmap(shared_state_ptr, sizeof(struct sim_state));
        close(shm_fd);
        shared_state_ptr = (void *)-1;
        shm_fd = -1;
        return -1;
    }

    memset(shared_state_ptr, 0, sizeof(struct sim_state));
    return 0;
}

int shm_state_open(void) {
    shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        /* If UI or Core starts first and SHM isn't created yet, create it */
        return shm_state_create();
    }

    shared_state_ptr = mmap(NULL, sizeof(struct sim_state), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_state_ptr == MAP_FAILED) {
        perror("shm_state_open: mmap");
        close(shm_fd);
        shm_fd = -1;
        return -1;
    }

    state_sem = sem_open(SEM_NAME, O_RDWR, 0666);
    if (state_sem == SEM_FAILED) {
        perror("shm_state_open: sem_open");
        munmap(shared_state_ptr, sizeof(struct sim_state));
        close(shm_fd);
        shared_state_ptr = (void *)-1;
        shm_fd = -1;
        return -1;
    }

    return 0;
}

int shm_publish_state(const struct sim_state *st) {
    if (shared_state_ptr == (void *)-1 || state_sem == SEM_FAILED) {
        errno = EINVAL;
        return -1;
    }

    while (sem_wait(state_sem) == -1) {
        if (errno == EINTR) continue; /* Handle interrupted system call */
        return -1;
    }

    memcpy(shared_state_ptr, st, sizeof(struct sim_state));

    if (sem_post(state_sem) == -1) {
        return -1;
    }

    return 0;
}

int shm_read_state(struct sim_state *st) {
    if (shared_state_ptr == (void *)-1 || state_sem == SEM_FAILED) {
        errno = EINVAL;
        return -1;
    }

    while (sem_wait(state_sem) == -1) {
        if (errno == EINTR) continue;
        return -1;
    }

    memcpy(st, shared_state_ptr, sizeof(struct sim_state));

    if (sem_post(state_sem) == -1) {
        return -1;
    }

    return 0;
}

int shm_state_close(void) {
    if (shared_state_ptr != (void *)-1) {
        munmap(shared_state_ptr, sizeof(struct sim_state));
        shared_state_ptr = (void *)-1;
    }
    if (shm_fd != -1) {
        close(shm_fd);
        shm_fd = -1;
    }
    if (state_sem != SEM_FAILED) {
        sem_close(state_sem);
        state_sem = SEM_FAILED;
    }
    return 0;
}

int shm_state_unlink(void) {
    shm_state_close();
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_NAME);
    return 0;
}
