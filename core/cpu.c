#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void str_to_upper(char *str) {
    while (*str) {
        *str = (char)toupper((unsigned char)*str);
        str++;
    }
}

int cpu_get_register_index(const char *reg_str) {
    if (!reg_str || strlen(reg_str) < 2) return -1;
    if (reg_str[0] == 'R' || reg_str[0] == 'r') {
        int idx = reg_str[1] - '0';
        if (idx >= 0 && idx < NUM_REGISTERS) return idx;
    }
    return -1;
}

const char* cpu_status_to_string(CpuStatus status) {
    switch (status) {
        case CPU_SUCCESS: return "SUCCESS";
        case CPU_ERR_INVALID_OPCODE: return "INVALID_OPCODE";
        case CPU_ERR_INVALID_OPERAND: return "INVALID_OPERAND";
        case CPU_ERR_DIV_BY_ZERO: return "DIV_BY_ZERO";
        case CPU_ERR_MEMORY_FAULT: return "MEMORY_FAULT";
        case CPU_ERR_STACK_FAULT: return "STACK_FAULT";
        case CPU_ERR_HALTED: return "CPU_HALTED";
        default: return "UNKNOWN_ERROR";
    }
}

void cpu_init(Cpu *cpu) {
    if (!cpu) return;
    for (int i = 0; i < NUM_REGISTERS; i++) {
        cpu->registers[i] = 0;
    }
    cpu->pc = 0;
    cpu->flag_zero = false;
    cpu->flag_negative = false;
    cpu->halted = false;
    cpu->cycles_executed = 0;
}

static void update_flags(Cpu *cpu, int result) {
    cpu->flag_zero = (result == 0);
    cpu->flag_negative = (result < 0);
}

// Helper to parse either an immediate integer or a register
static bool parse_operand_val(const Cpu *cpu, const char *token, int *out_val) {
    int r = cpu_get_register_index(token);
    if (r >= 0) {
        *out_val = cpu->registers[r];
        return true;
    }
    char *endptr;
    long val = strtol(token, &endptr, 10);
    if (*endptr == '\0') {
        *out_val = (int)val;
        return true;
    }
    return false;
}

CpuStatus cpu_execute_instruction(Cpu *cpu, Memory *mem, Stack *stack, const char *instruction, char *out_log_msg, size_t max_log_len) {
    if (!cpu || !mem || !stack || !instruction) return CPU_ERR_INVALID_OPERAND;

    if (cpu->halted) {
        if (out_log_msg) snprintf(out_log_msg, max_log_len, "CPU is halted. Reset required.");
        return CPU_ERR_HALTED;
    }

    char buffer[256];
    strncpy(buffer, instruction, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    // Trim leading whitespace
    char *start = buffer;
    while (isspace((unsigned char)*start)) start++;
    if (*start == '\0' || *start == '#') {
        // Empty or comment
        if (out_log_msg) snprintf(out_log_msg, max_log_len, "NOOP");
        return CPU_SUCCESS;
    }

    char opcode[32] = {0};
    char arg1[64] = {0};
    char arg2[64] = {0};

    int tokens = sscanf(start, "%31s %63s %63s", opcode, arg1, arg2);
    str_to_upper(opcode);

    int initial_pc = cpu->pc;
    cpu->cycles_executed++;

    // 1. LOAD
    // Syntax: LOAD Rx <value>  OR  LOAD Rx [addr]
    if (strcmp(opcode, "LOAD") == 0) {
        int r = cpu_get_register_index(arg1);
        if (r < 0 || tokens < 3) {
            snprintf(out_log_msg, max_log_len, "Syntax Error: LOAD requires register and value/address");
            return CPU_ERR_INVALID_OPERAND;
        }
        if (arg2[0] == '[' && arg2[strlen(arg2) - 1] == ']') {
            int addr = atoi(arg2 + 1);
            int val = 0;
            if (!mem_read(mem, addr, &val)) {
                snprintf(out_log_msg, max_log_len, "Memory Fault: Failed reading address %d", addr);
                return CPU_ERR_MEMORY_FAULT;
            }
            cpu->registers[r] = val;
            update_flags(cpu, val);
            snprintf(out_log_msg, max_log_len, "PC=%d | LOAD R%d from Mem[%d] = %d", initial_pc, r, addr, val);
        } else {
            int val = 0;
            if (!parse_operand_val(cpu, arg2, &val)) {
                snprintf(out_log_msg, max_log_len, "Invalid operand: %s", arg2);
                return CPU_ERR_INVALID_OPERAND;
            }
            cpu->registers[r] = val;
            update_flags(cpu, val);
            snprintf(out_log_msg, max_log_len, "PC=%d | LOAD R%d = %d", initial_pc, r, val);
        }
        cpu->pc++;
        return CPU_SUCCESS;
    }

    // 2. STORE
    // Syntax: STORE [addr] Rx  OR  STORE [addr] <val>
    if (strcmp(opcode, "STORE") == 0) {
        if (tokens < 3 || arg1[0] != '[' || arg1[strlen(arg1) - 1] != ']') {
            snprintf(out_log_msg, max_log_len, "Syntax Error: STORE requires [addr] and value/reg");
            return CPU_ERR_INVALID_OPERAND;
        }
        int addr = atoi(arg1 + 1);
        int val = 0;
        if (!parse_operand_val(cpu, arg2, &val)) {
            snprintf(out_log_msg, max_log_len, "Invalid operand: %s", arg2);
            return CPU_ERR_INVALID_OPERAND;
        }
        if (!mem_write(mem, addr, val)) {
            snprintf(out_log_msg, max_log_len, "Memory Fault: Failed writing address %d", addr);
            return CPU_ERR_MEMORY_FAULT;
        }
        snprintf(out_log_msg, max_log_len, "PC=%d | STORE Mem[%d] = %d", initial_pc, addr, val);
        cpu->pc++;
        return CPU_SUCCESS;
    }

    // 3. MOV
    // Syntax: MOV Rx Ry
    if (strcmp(opcode, "MOV") == 0) {
        int r_dst = cpu_get_register_index(arg1);
        int val = 0;
        if (r_dst < 0 || !parse_operand_val(cpu, arg2, &val)) {
            snprintf(out_log_msg, max_log_len, "Syntax Error: MOV Rx Ry/val");
            return CPU_ERR_INVALID_OPERAND;
        }
        cpu->registers[r_dst] = val;
        update_flags(cpu, val);
        snprintf(out_log_msg, max_log_len, "PC=%d | MOV R%d = %d", initial_pc, r_dst, val);
        cpu->pc++;
        return CPU_SUCCESS;
    }

    // 4. ADD
    // Syntax: ADD Rx Ry  OR  ADD Rx <val>
    if (strcmp(opcode, "ADD") == 0) {
        int r_dst = cpu_get_register_index(arg1);
        int val = 0;
        if (r_dst < 0 || !parse_operand_val(cpu, arg2, &val)) {
            snprintf(out_log_msg, max_log_len, "Syntax Error: ADD Rx Ry/val");
            return CPU_ERR_INVALID_OPERAND;
        }
        cpu->registers[r_dst] += val;
        update_flags(cpu, cpu->registers[r_dst]);
        snprintf(out_log_msg, max_log_len, "PC=%d | ADD R%d = %d (Flags: Z=%d, N=%d)", initial_pc, r_dst, cpu->registers[r_dst], cpu->flag_zero, cpu->flag_negative);
        cpu->pc++;
        return CPU_SUCCESS;
    }

    // 5. SUB
    // Syntax: SUB Rx Ry  OR  SUB Rx <val>
    if (strcmp(opcode, "SUB") == 0) {
        int r_dst = cpu_get_register_index(arg1);
        int val = 0;
        if (r_dst < 0 || !parse_operand_val(cpu, arg2, &val)) {
            snprintf(out_log_msg, max_log_len, "Syntax Error: SUB Rx Ry/val");
            return CPU_ERR_INVALID_OPERAND;
        }
        cpu->registers[r_dst] -= val;
        update_flags(cpu, cpu->registers[r_dst]);
        snprintf(out_log_msg, max_log_len, "PC=%d | SUB R%d = %d (Flags: Z=%d, N=%d)", initial_pc, r_dst, cpu->registers[r_dst], cpu->flag_zero, cpu->flag_negative);
        cpu->pc++;
        return CPU_SUCCESS;
    }

    // 6. MUL
    // Syntax: MUL Rx Ry
    if (strcmp(opcode, "MUL") == 0) {
        int r_dst = cpu_get_register_index(arg1);
        int val = 0;
        if (r_dst < 0 || !parse_operand_val(cpu, arg2, &val)) {
            snprintf(out_log_msg, max_log_len, "Syntax Error: MUL Rx Ry/val");
            return CPU_ERR_INVALID_OPERAND;
        }
        cpu->registers[r_dst] *= val;
        update_flags(cpu, cpu->registers[r_dst]);
        snprintf(out_log_msg, max_log_len, "PC=%d | MUL R%d = %d", initial_pc, r_dst, cpu->registers[r_dst]);
        cpu->pc++;
        return CPU_SUCCESS;
    }

    // 7. DIV
    // Syntax: DIV Rx Ry
    if (strcmp(opcode, "DIV") == 0) {
        int r_dst = cpu_get_register_index(arg1);
        int divisor = 0;
        if (r_dst < 0 || !parse_operand_val(cpu, arg2, &divisor)) {
            snprintf(out_log_msg, max_log_len, "Syntax Error: DIV Rx Ry/val");
            return CPU_ERR_INVALID_OPERAND;
        }
        if (divisor == 0) {
            snprintf(out_log_msg, max_log_len, "CPU Error: Division by zero attempted in DIV R%d", r_dst);
            return CPU_ERR_DIV_BY_ZERO;
        }
        cpu->registers[r_dst] /= divisor;
        update_flags(cpu, cpu->registers[r_dst]);
        snprintf(out_log_msg, max_log_len, "PC=%d | DIV R%d = %d", initial_pc, r_dst, cpu->registers[r_dst]);
        cpu->pc++;
        return CPU_SUCCESS;
    }

    // 8. PUSH
    // Syntax: PUSH Rx  OR  PUSH <val>
    if (strcmp(opcode, "PUSH") == 0) {
        int val = 0;
        if (!parse_operand_val(cpu, arg1, &val)) {
            snprintf(out_log_msg, max_log_len, "Syntax Error: PUSH requires reg or value");
            return CPU_ERR_INVALID_OPERAND;
        }
        if (!stack_push(stack, val)) {
            snprintf(out_log_msg, max_log_len, "Stack Overflow error during PUSH");
            return CPU_ERR_STACK_FAULT;
        }
        snprintf(out_log_msg, max_log_len, "PC=%d | PUSH %d to Stack (Stack Size: %d)", initial_pc, val, stack_size(stack));
        cpu->pc++;
        return CPU_SUCCESS;
    }

    // 9. POP
    // Syntax: POP Rx
    if (strcmp(opcode, "POP") == 0) {
        int r = cpu_get_register_index(arg1);
        if (r < 0) {
            snprintf(out_log_msg, max_log_len, "Syntax Error: POP Rx");
            return CPU_ERR_INVALID_OPERAND;
        }
        int val = 0;
        if (!stack_pop(stack, &val)) {
            snprintf(out_log_msg, max_log_len, "Stack Underflow error during POP");
            return CPU_ERR_STACK_FAULT;
        }
        cpu->registers[r] = val;
        update_flags(cpu, val);
        snprintf(out_log_msg, max_log_len, "PC=%d | POP R%d = %d (Stack Size: %d)", initial_pc, r, val, stack_size(stack));
        cpu->pc++;
        return CPU_SUCCESS;
    }

    // 10. PEEK
    // Syntax: PEEK Rx
    if (strcmp(opcode, "PEEK") == 0) {
        int r = cpu_get_register_index(arg1);
        if (r < 0) {
            snprintf(out_log_msg, max_log_len, "Syntax Error: PEEK Rx");
            return CPU_ERR_INVALID_OPERAND;
        }
        int val = 0;
        if (!stack_peek(stack, &val)) {
            snprintf(out_log_msg, max_log_len, "Stack Underflow error during PEEK");
            return CPU_ERR_STACK_FAULT;
        }
        cpu->registers[r] = val;
        snprintf(out_log_msg, max_log_len, "PC=%d | PEEK R%d = %d", initial_pc, r, val);
        cpu->pc++;
        return CPU_SUCCESS;
    }

    // 11. PRINT
    // Syntax: PRINT Rx  OR  PRINT [addr]
    if (strcmp(opcode, "PRINT") == 0) {
        if (arg1[0] == '[' && arg1[strlen(arg1) - 1] == ']') {
            int addr = atoi(arg1 + 1);
            int val = 0;
            if (!mem_read(mem, addr, &val)) {
                snprintf(out_log_msg, max_log_len, "Memory Fault in PRINT [%d]", addr);
                return CPU_ERR_MEMORY_FAULT;
            }
            snprintf(out_log_msg, max_log_len, "PRINT: Mem[%d] = %d", addr, val);
        } else {
            int r = cpu_get_register_index(arg1);
            if (r >= 0) {
                snprintf(out_log_msg, max_log_len, "PRINT: R%d = %d", r, cpu->registers[r]);
            } else {
                snprintf(out_log_msg, max_log_len, "PRINT: %s", arg1);
            }
        }
        cpu->pc++;
        return CPU_SUCCESS;
    }

    // 12. DUMP
    if (strcmp(opcode, "DUMP") == 0) {
        cpu_dump(cpu);
        stack_dump(stack);
        mem_dump(mem, 0, 16);
        snprintf(out_log_msg, max_log_len, "System state dumped successfully");
        cpu->pc++;
        return CPU_SUCCESS;
    }

    // 13. HALT / EXIT
    if (strcmp(opcode, "HALT") == 0 || strcmp(opcode, "EXIT") == 0) {
        cpu->halted = true;
        snprintf(out_log_msg, max_log_len, "CPU Halted gracefully at PC=%d (Total cycles: %zu)", initial_pc, cpu->cycles_executed);
        return CPU_SUCCESS;
    }

    // Unrecognized Opcode
    snprintf(out_log_msg, max_log_len, "Unknown Opcode: '%s'", opcode);
    return CPU_ERR_INVALID_OPCODE;
}

void cpu_dump(const Cpu *cpu) {
    if (!cpu) return;
    printf("\n=== CPU State ===\n");
    printf("PC: %d | Cycles: %zu | Halted: %s\n", cpu->pc, cpu->cycles_executed, cpu->halted ? "YES" : "NO");
    printf("Flags: Zero(Z)=%d | Negative(N)=%d\n", cpu->flag_zero, cpu->flag_negative);
    printf("Registers:\n");
    for (int i = 0; i < NUM_REGISTERS; i++) {
        printf("  R%d = %-8d (0x%08X)\n", i, cpu->registers[i], cpu->registers[i]);
    }
    printf("=================\n\n");
}
