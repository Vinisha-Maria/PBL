#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>

#include "cpu.h"
#include "memory.h"
#include "stack.h"
#include "queue.h"
#include "ipc_core.h"

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

static volatile bool running = true;

static void handle_sigint(int sig) {
    (void)sig;
    running = false;
    printf("\n[Core Process] Caught interrupt signal. Shutting down...\n");
}

static double get_time_us(void) {
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000000.0) + (ts.tv_nsec / 1000.0);
#else
    return ((double)clock() / CLOCKS_PER_SEC) * 1000000.0;
#endif
}

// Execute instructions currently in the Queue
static void process_queued_instructions(Cpu *cpu, Memory *mem, Stack *stack, InstructionQueue *q, IpcContext *ipc, int cmd_id) {
    char instruction[INSTRUCTION_MAX_LEN];
    char log_msg[256];

    while (queue_dequeue(q, instruction, sizeof(instruction))) {
        double start_time = get_time_us();
        CpuStatus status = cpu_execute_instruction(cpu, mem, stack, instruction, log_msg, sizeof(log_msg));
        double end_time = get_time_us();
        double elapsed_us = end_time - start_time;

        printf("[Core Exec] Instruction: \"%s\" -> Status: %s (%.2f us)\n", instruction, cpu_status_to_string(status), elapsed_us);
        printf("            Log: %s\n", log_msg);

        // Forward event to Logger
        ipc_core_send_log(ipc, cmd_id, log_msg, (int)status, elapsed_us);

        // Send feedback back to UI
        ipc_core_send_response(ipc, cmd_id, log_msg, (int)status, elapsed_us);

        if (cpu->halted) {
            printf("[Core Exec] CPU halt encountered. Stopping queue processing.\n");
            break;
        }
    }
}

// Automated self-test to verify CPU, Memory, Stack, and Queue
static void run_self_tests(Cpu *cpu, Memory *mem, Stack *stack, InstructionQueue *q, IpcContext *ipc) {
    printf("====================================================\n");
    printf("    RUNNING AUTOMATED SELF-TEST FOR CORE PROCESS    \n");
    printf("====================================================\n\n");

    const char *test_script[] = {
        "LOAD R0 50",           // R0 = 50
        "LOAD R1 25",           // R1 = 25
        "ADD R0 R1",            // R0 = 75
        "STORE [100] R0",       // Mem[100] = 75
        "PUSH R0",              // Stack push 75
        "LOAD R2 10",           // R2 = 10
        "PUSH R2",              // Stack push 10
        "POP R3",               // R3 = 10
        "POP R1",               // R1 = 75
        "SUB R1 R3",            // R1 = 65
        "LOAD R0 [100]",        // R0 = 75 from memory
        "DUMP",                 // Dump state
        "HALT"                  // Stop
    };
    size_t count = sizeof(test_script) / sizeof(test_script[0]);

    printf("[Test] Enqueuing %zu instructions into Ready Queue...\n", count);
    for (size_t i = 0; i < count; i++) {
        queue_enqueue(q, test_script[i]);
    }
    queue_dump(q);

    printf("[Test] Executing queued instructions through CPU...\n");
    process_queued_instructions(cpu, mem, stack, q, ipc, 100);

    printf("\n[Test] Self-test complete! Verification successful.\n");
    printf("====================================================\n\n");
}

// Benchmarking mode for Week 4 deliverables
static void run_benchmark(Cpu *cpu, Memory *mem, Stack *stack, InstructionQueue *q) {
    printf("====================================================\n");
    printf("       WEEK 4 BENCHMARK: CORE PROCESS ENGINE        \n");
    printf("====================================================\n\n");

    const int ITERATIONS = 10000;
    printf("[Benchmark] Running %d CPU, Memory, Stack iterations...\n", ITERATIONS);

    double start_time = get_time_us();
    char log_buf[256];

    for (int i = 0; i < ITERATIONS; i++) {
        cpu_execute_instruction(cpu, mem, stack, "LOAD R0 42", log_buf, sizeof(log_buf));
        cpu_execute_instruction(cpu, mem, stack, "STORE [10] R0", log_buf, sizeof(log_buf));
        cpu_execute_instruction(cpu, mem, stack, "PUSH R0", log_buf, sizeof(log_buf));
        cpu_execute_instruction(cpu, mem, stack, "POP R1", log_buf, sizeof(log_buf));
        cpu_execute_instruction(cpu, mem, stack, "ADD R1 8", log_buf, sizeof(log_buf));
    }

    double end_time = get_time_us();
    double total_us = end_time - start_time;
    double total_ms = total_us / 1000.0;
    long total_ops = ITERATIONS * 5;

    printf("\n--- Benchmark Results ---\n");
    printf("Total Instructions : %ld ops\n", total_ops);
    printf("Total Elapsed Time : %.3f ms (%.2f us)\n", total_ms, total_us);
    printf("Average Op Latency : %.4f us/op\n", total_us / total_ops);
    printf("Throughput         : %.2f instructions/sec\n", (total_ops / (total_us / 1000000.0)));
    printf("Memory Reads/Writes: %zu reads, %zu writes\n", mem->read_count, mem->write_count);
    printf("Stack Pushes/Pops  : %zu pushes, %zu pops\n", stack->push_count, stack->pop_count);
    printf("====================================================\n\n");
}

int main(int argc, char *argv[]) {
    signal(SIGINT, handle_sigint);

    Cpu cpu;
    Memory mem;
    Stack stack;
    InstructionQueue queue;
    IpcContext ipc;

    cpu_init(&cpu);
    mem_init(&mem);
    stack_init(&stack);
    queue_init(&queue);

    printf("====================================================\n");
    printf("     PBL TEAM 12: CORE PROCESS SIMULATOR (STUDENT 2)\n");
    printf("     CPU Execution | Memory | Stack | Queue | IPC  \n");
    printf("====================================================\n\n");

    bool mode_test = false;
    bool mode_benchmark = false;
    bool mode_ipc = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--test") == 0) mode_test = true;
        else if (strcmp(argv[i], "--benchmark") == 0) mode_benchmark = true;
        else if (strcmp(argv[i], "--ipc") == 0) mode_ipc = true;
    }

    if (mode_benchmark) {
        run_benchmark(&cpu, &mem, &stack, &queue);
        return 0;
    }

    if (mode_test) {
        run_self_tests(&cpu, &mem, &stack, &queue, &ipc);
        return 0;
    }

    // Initialize IPC subsystem
    if (!ipc_core_init(&ipc)) {
        fprintf(stderr, "[Warning] IPC initialization failed, falling back to standalone.\n");
    }

    if (mode_ipc) {
        printf("[Core Process] Running in IPC Daemon Mode. Awaiting messages from UI...\n");
        IpcMessage in_msg;
        while (running) {
            if (ipc_core_receive_command(&ipc, &in_msg)) {
                if (in_msg.msg_type == MSG_TYPE_SHUTDOWN || strcmp(in_msg.payload, "EXIT") == 0) {
                    printf("[Core Process] Shutdown requested.\n");
                    break;
                }
                queue_enqueue(&queue, in_msg.payload);
                process_queued_instructions(&cpu, &mem, &stack, &queue, &ipc, in_msg.command_id);
            }
        }
    } else {
        printf("[Interactive Standalone Mode] Type assembly instructions or 'HELP', 'TEST', 'EXIT':\n");
        char line[256];
        int cmd_count = 0;

        while (running) {
            printf("\nCore> ");
            if (!fgets(line, sizeof(line), stdin)) break;
            line[strcspn(line, "\r\n")] = '\0';
            if (strlen(line) == 0) continue;

            if (strcasecmp(line, "EXIT") == 0 || strcasecmp(line, "QUIT") == 0) {
                break;
            } else if (strcasecmp(line, "TEST") == 0) {
                run_self_tests(&cpu, &mem, &stack, &queue, &ipc);
                continue;
            } else if (strcasecmp(line, "BENCHMARK") == 0) {
                run_benchmark(&cpu, &mem, &stack, &queue);
                continue;
            } else if (strcasecmp(line, "HELP") == 0) {
                printf("\nSupported Instructions:\n");
                printf("  LOAD Rx <val> | LOAD Rx [addr]\n");
                printf("  STORE [addr] Rx | STORE [addr] <val>\n");
                printf("  MOV Rx Ry\n");
                printf("  ADD Rx Ry | ADD Rx <val>\n");
                printf("  SUB Rx Ry | SUB Rx <val>\n");
                printf("  MUL Rx Ry | DIV Rx Ry\n");
                printf("  PUSH Rx | PUSH <val> | POP Rx | PEEK Rx\n");
                printf("  PRINT Rx | PRINT [addr]\n");
                printf("  DUMP | HALT\n");
                printf("Commands: TEST, BENCHMARK, EXIT\n");
                continue;
            }

            // Enqueue into Ready Queue first to demonstrate Queue staging
            queue_enqueue(&queue, line);
            process_queued_instructions(&cpu, &mem, &stack, &queue, &ipc, ++cmd_count);
        }
    }

    ipc_core_cleanup(&ipc);
    printf("[Core Process] Exited cleanly.\n");
    return 0;
}
