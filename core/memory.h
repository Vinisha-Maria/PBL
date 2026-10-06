#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>
#include <stdbool.h>

#define MEMORY_SIZE 1024

typedef struct {
    int data[MEMORY_SIZE];
    bool initialized[MEMORY_SIZE];
    size_t read_count;
    size_t write_count;
} Memory;

// Function declarations
void mem_init(Memory *mem);
bool mem_read(Memory *mem, int address, int *out_val);
bool mem_write(Memory *mem, int address, int val);
void mem_dump(const Memory *mem, int start, int count);
void mem_reset(Memory *mem);

#endif // MEMORY_H
