/*
 * input.h - Safe user input and random process generation.
 */
#ifndef INPUT_H
#define INPUT_H

#include "common.h"

/* Largest value accepted for any numeric field (prevents integer overflow). */
#define MAX_INPUT_VALUE 100000

/* Ask the user for processes. Fills p[] and sets *n. Re-asks on invalid input. */
void read_processes(Process p[], int *n);

/* Ask the user for the Round Robin time quantum (must be > 0). */
int read_quantum(void);

/* Fill p[0..n-1] with random processes (PIDs 1..n). */
void generate_random_processes(Process p[], int n);

/* Helper (also used by main.c): print `prompt`, read an integer between
 * min and max (inclusive). Keeps asking until the input is valid. */
int read_int_in_range(const char *prompt, int min, int max);

#endif /* INPUT_H */
