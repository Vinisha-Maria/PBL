#include "common.h"

/* Strategy identifier for the shared simulation engine */
typedef enum {
    STRAT_FIFO,
    STRAT_LRU,
    STRAT_OPTIMAL
} strat_t;

/* Shared core engine that simulates page replacement for FIFO, LRU, and Optimal */
static void run_simulation(const int *pages, int n, int frames_count, strat_t strat, step_t *steps) {
    int frame_contents[MAX_FRAMES];
    int load_time[MAX_FRAMES];
    int last_used_time[MAX_FRAMES];

    // Initialize all frames and tracking metadata as empty (-1)
    for (int i = 0; i < MAX_FRAMES; i++) {
        frame_contents[i] = EMPTY;
        load_time[i] = -1;
        last_used_time[i] = -1;
    }

    for (int i = 0; i < n; i++) {
        int page = pages[i];
        int hit = 0;
        int hit_idx = -1;

        // 1. Check if the requested page is already in memory (Hit check)
        for (int f = 0; f < frames_count; f++) {
            if (frame_contents[f] == page) {
                hit = 1;
                hit_idx = f;
                break;
            }
        }

        int replaced = EMPTY;

        if (hit) {
            // On a hit, update last-used time for LRU (FIFO and Optimal do not change order on hit)
            last_used_time[hit_idx] = i;
        } else {
            // Fault: check for any empty frame first (lowest-numbered empty frame)
            int empty_idx = -1;
            for (int f = 0; f < frames_count; f++) {
                if (frame_contents[f] == EMPTY) {
                    empty_idx = f;
                    break;
                }
            }

            int victim_idx = -1;
            if (empty_idx != -1) {
                victim_idx = empty_idx;
            } else {
                // All frames are full; select a victim based on the algorithm strategy
                if (strat == STRAT_FIFO) {
                    int min_load = i + 1;
                    for (int f = 0; f < frames_count; f++) {
                        if (load_time[f] < min_load) {
                            min_load = load_time[f];
                            victim_idx = f;
                        }
                    }
                } else if (strat == STRAT_LRU) {
                    int min_used = i + 1;
                    for (int f = 0; f < frames_count; f++) {
                        if (last_used_time[f] < min_used) {
                            min_used = last_used_time[f];
                            victim_idx = f;
                        }
                    }
                } else if (strat == STRAT_OPTIMAL) {
                    int farthest_use = -1;
                    int earliest_load = i + 1;

                    for (int f = 0; f < frames_count; f++)
