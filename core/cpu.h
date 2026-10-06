#ifndef CPU_H
#define CPU_H

#include "memory.h"
#include "stack.h"
#include <stdbool.h>
#include <stddef.h>

#define NUM_REGISTERS 4

typedef enum {
    CPU_SUCCESS = 0,
    CPU_ERR_INVALID_OPCODE,
    CPU_ERR_INVALID_OPERAND,
    CPU_ERR_DIV_BY_ZERO,
    CPU_ERR_MEMORY_FAULT,
    CPU_ERR_STACK_FAULT,
    CPU_ERR_HALTED
} CpuStatus;

typedef struct {
    int registers[NUM_REGISTERS]; // R0, R1, R2, R3
    int pc;                       // Program Counter
    bool flag_zero;               // Zero Flag (Z)
    bool flag_negative;           // Negative Flag (N)
    bool halted;                  // CPU Halted state
    size_t cycles_executed;       // Total instruction cycles
} Cpu;

// Core CPU functions
void cpu_init(Cpu *cpu);
CpuStatus cpu_execute_instruction(Cpu *cpu, Memory *mem, Stack *stack, const char *instruction, char *out_log_msg, size_t max_log_len);
void cpu_dump(const Cpu *cpu);
const char* cpu_status_to_string(CpuStatus status);
int cpu_get_register_index(const char *reg_str);

#endif // CPU_H
