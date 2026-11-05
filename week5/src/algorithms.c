/*
 * algorithms.c - FIFO, LRU and Optimal page replacement.
 *
 * Each public function simulates one algorithm and writes one step_t
 * per page request into steps[0..n-1].
 *
 * The three algorithms share one helper (simulate). The only difference
 * between them is how the victim frame is chosen, so each algorithm
 * supplies a small "choose victim" function.
 *
 * No global or static variables are used, so every call gives the same
 * answer, also when called in separate worker processes.
 */

#include "common.h"

/* A function that picks the frame position to replace.
 *   pages     : all page requests
 *   n         : number of page requests
 *   now       : index of the current request in pages[]
 *   frames    : number of frames in use
 *   frame     : current page in each frame position (all are full here)
 *   load_time : request index at which each frame's page was loaded
 *   last_used : request index at which each frame's page was last used
 * It returns the position (0..frames-1) of the victim.                  */
typedef int (*victim_fn)(const int *pages, int n, int now, int frames,
                         const int *frame, const int *load_time,
                         const int *last_used);

/* FIFO: victim is the frame with the smallest load time. */
static int victim_fifo(const int *pages, int n, int now, int frames,
                       const int *frame, const int *load_time,
                       const int *last_used)
{
    int best = 0;
    (void)pages; (void)n; (void)now; (void)frame; (void)last_used;

    for (int f = 1; f < frames; f++) {
        if (load_time[f] < load_time[best])
            best = f;
    }
    return best;
}

/* LRU: victim is the frame with the smallest last-used time. */
static int victim_lru(const int *pages, int n, int now, int frames,
                      const int *frame, const int *load_time,
                      const int *last_used)
{
    int best = 0;
    (void)pages; (void)n; (void)now; (void)frame; (void)load_time;

    for (int f = 1; f < frames; f++) {
        if (last_used[f] < last_used[best])
            best = f;
    }
    return best;
}

/* Find the index of the next use of 'page' after position 'now'.
 * If the page is never used again, return n (larger than any index). */
static int next_use(const int *pages, int n, int now, int page)
{
    for (int j = now + 1; j < n; j++) {
        if (pages[j] == page)
            return j;
    }
    return n;
}

/* Optimal: victim is the frame whose page is used farthest in the future.
 * On a tie, the page that was loaded earliest is the victim. */
static int victim_optimal(const int *pages, int n, int now, int frames,
                          const int *frame, const int *load_time,
                          const int *last_used)
{
    int best = 0;
    int best_next = next_use(pages, n, now, frame[0]);
    (void)last_used;

    for (int f = 1; f < frames; f++) {
        int nu = next_use(pages, n, now, frame[f]);
        if (nu > best_next || (nu == best_next && load_time[f] < load_time[best])) {
            best = f;
            best_next = nu;
        }
    }
    return best;
}

/* Common part of all three algorithms: hit check, empty frame search,
 * replacement at the victim's position, and filling each step. */
static void simulate(const int *pages, int n, int frames, step_t *steps,
                     victim_fn choose)
{
    int frame[MAX_FRAMES];       /* page in each frame, or EMPTY */
    int load_time[MAX_FRAMES];   /* when the page was loaded */
    int last_used[MAX_FRAMES];   /* when the page was last used */

    for (int f = 0; f < MAX_FRAMES; f++) {
        frame[f] = EMPTY;
        load_time[f] = 0;
        last_used[f] = 0;
    }

    for (int i = 0; i < n; i++) {
        int page = pages[i];
        int hit_pos = EMPTY;
        int replaced = EMPTY;

        /* 1. Is the page already in a frame? */
        for (int f = 0; f < frames; f++) {
            if (frame[f] == page) {
                hit_pos = f;
                break;
            }
        }

        if (hit_pos != EMPTY) {
            /* Hit: only the last-used time changes (load time stays). */
            last_used[hit_pos] = i;
        } else {
            /* 2. Fault: look for the lowest-numbered empty frame. */
            int pos = EMPTY;
            for (int f = 0; f < frames; f++) {
                if (frame[f] == EMPTY) {
                    pos = f;
                    break;
                }
            }

            /* 3. No empty frame: choose a victim and remember its page. */
            if (pos == EMPTY) {
                pos = choose(pages, n, i, frames, frame, load_time, last_used);
                replaced = frame[pos];
            }

            /* Put the new page into that position. */
            frame[pos] = page;
            load_time[pos] = i;
            last_used[pos] = i;
        }

        /* 4. Fill the step. Unused frame entries stay EMPTY. */
        steps[i].page = page;
        for (int f = 0; f < MAX_FRAMES; f++)
            steps[i].frames[f] = (f < frames) ? frame[f] : EMPTY;
        steps[i].hit = (hit_pos != EMPTY) ? 1 : 0;
        steps[i].replaced = replaced;
    }
}

/* FIFO: replace the page that has been in memory the longest. */
void fifo(const int *pages, int n, int frames, step_t *steps)
{
    simulate(pages, n, frames, steps, victim_fifo);
}

/* LRU: replace the page that was used longest ago. */
void lru(const int *pages, int n, int frames, step_t *steps)
{
    simulate(pages, n, frames, steps, victim_lru);
}

/* Optimal: replace the page that will not be used for the longest time. */
void optimal(const int *pages, int n, int frames, step_t *steps)
{
    simulate(pages, n, frames, steps, victim_optimal);
}
