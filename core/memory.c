#include "memory.h"
#include <stdio.h>
#include <string.h>

void mem_init(Memory *mem) {
    if (!mem) return;
    memset(mem->data, 0, sizeof(mem->data));
    memset(mem->initialized, false, sizeof(mem->initialized));
    mem->read_count = 0;
    mem->write_count = 0;
}

bool mem_read(Memory *mem, int address, int *out_val) {
    if (!mem || !out_val) return false;
    if (address < 0 || address >= MEMORY_SIZE) {
        fprintf(stderr, "[Memory Error] Out of bounds read at address %d (Valid: 0-%d)\n", address, MEMORY_SIZE - 1);
        return false;
    }
    *out_val = mem->data[address];
    mem->read_count++;
    return true;
}

bool mem_write(Memory *mem, int address, int val) {
    if (!mem) return false;
    if (address < 0 || address >= MEMORY_SIZE) {
        fprintf(stderr, "[Memory Error] Out of bounds write at address %d (Valid: 0-%d)\n", address, MEMORY_SIZE - 1);
        return false;
    }
    mem->data[address] = val;
    mem->initialized[address] = true;
    mem->write_count++;
    return true;
}

void mem_dump(const Memory *mem, int start, int count) {
    if (!mem) return;
    if (start < 0) start = 0;
    if (count <= 0) count = 16;
    if (start + count > MEMORY_SIZE) count = MEMORY_SIZE - start;

    printf("\n--- Memory Dump [0x%04X to 0x%04X] ---\n", start, start + count - 1);
    printf("Addr    Value (Dec)    Value (Hex)\n");
    printf("----------------------------------\n");
    for (int i = start; i < start + count; i++) {
        if (mem->initialized[i] || mem->data[i] != 0) {
            printf("[%04d]  %-12d   0x%08X\n", i, mem->data[i], mem->data[i]);
        }
    }
    printf("Stats: Reads = %zu, Writes = %zu\n", mem->read_count, mem->write_count);
    printf("----------------------------------\n\n");
}

void mem_reset(Memory *mem) {
    mem_init(mem);
}
