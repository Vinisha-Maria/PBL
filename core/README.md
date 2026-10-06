# Student 2 — Core Process: CPU Execution, Memory, Stack, and Queue

**Module:** Core Process Engine (Week 4 Simulator)  
**Assigned Student:** Student 2  
**Repository Path:** `core/`

---

## 1. Overview & Architecture

The **Core Process** acts as the central execution engine of the multi-process OS simulator. It receives assembly-like instructions from the **UI Process**, manages a **Ready/Instruction Queue**, dispatches them to a simulated **CPU**, manipulates a simulated **Memory** and **Call Stack**, and emits event logs with timing metrics to the **Logger Process**.

```text
                  UI PROCESS (Teammate)
                          │
                          │  IPC (POSIX Message Queue: /pbl_ui_to_core)
                          ▼
        ┌───────────────────────────────────────────────┐
        │             CORE PROCESS (Student 2)          │
        │                                               │
        │   [ipc_core] ──> [Instruction Queue (FIFO)]   │
        │                         │                     │
        │                         ▼                     │
        │              [CPU Execution Engine]           │
        │             (PC, Registers R0-R3, ALU)        │
        │                   │            │              │
        │                   ▼            ▼              │
        │              [Memory RAM]   [Stack]           │
        │              (1024 words)  (256 LIFO)         │
        └───────────────────────┬───────────────────────┘
                                │
                                │  IPC (POSIX Message Queue: /pbl_core_to_logger)
                                ▼
                    LOGGER PROCESS (Teammate)
```

---

## 2. Directory Structure

```text
core/
├── cpu.h / cpu.c         # CPU Execution Unit (ALU, R0-R3, PC, Flags, Opcodes)
├── memory.h / memory.c   # Simulated RAM (1024 words, bounds-checked)
├── stack.h / stack.c     # LIFO Call Stack (Push, Pop, Peek, Overflow/Underflow)
├── queue.h / queue.c     # Circular FIFO Ready Queue for incoming instructions
├── ipc_core.h / ipc_core.c # POSIX Message Queue endpoints & IPC messaging
├── core.c                # Main controller process (Standalone, IPC, Benchmark)
├── mock_ui.c             # Test client to simulate the UI Process
├── mock_logger.c         # Test server to simulate the Logger Process
├── verify_core.py        # Automated test and benchmark runner
└── Makefile              # Automated build configuration
```

---

## 3. Instruction Set Architecture (ISA)

The Core CPU supports the following instructions (case-insensitive):

| Instruction | Syntax | Description | Example |
| :--- | :--- | :--- | :--- |
| **LOAD** | `LOAD Rx <val>` | Load immediate value into register | `LOAD R0 100` |
| **LOAD (Mem)** | `LOAD Rx [addr]` | Load value from memory address | `LOAD R1 [50]` |
| **STORE** | `STORE [addr] Rx` | Store register value into memory address | `STORE [50] R0` |
| **MOV** | `MOV Rx Ry` | Copy value from `Ry` to `Rx` | `MOV R2 R0` |
| **ADD** | `ADD Rx Ry` / `<val>` | Add operand to `Rx` (`Rx = Rx + operand`) | `ADD R0 R1` |
| **SUB** | `SUB Rx Ry` / `<val>` | Subtract operand from `Rx` (`Rx = Rx - operand`) | `SUB R0 25` |
| **MUL** | `MUL Rx Ry` / `<val>` | Multiply `Rx` by operand | `MUL R1 R2` |
| **DIV** | `DIV Rx Ry` / `<val>` | Divide `Rx` by operand (division-by-zero safe) | `DIV R0 2` |
| **PUSH** | `PUSH Rx` / `<val>` | Push register or value onto Stack | `PUSH R0` |
| **POP** | `POP Rx` | Pop top of Stack into register `Rx` | `POP R3` |
| **PEEK** | `PEEK Rx` | Inspect top of Stack without removing | `PEEK R1` |
| **PRINT** | `PRINT Rx` / `[addr]` | Display value of register or memory cell | `PRINT R0` |
| **DUMP** | `DUMP` | Print complete snapshot of CPU, Memory, Stack | `DUMP` |
| **HALT** | `HALT` / `EXIT` | Stop CPU execution | `HALT` |

---

## 4. Teammate IPC Integration Guide

### A. For the UI Process (Student 1 / UI Developer)
1. Open the message queue `/pbl_ui_to_core` with `O_WRONLY`.
2. Send commands wrapped in `IpcMessage` struct:
   ```c
   IpcMessage msg;
   msg.msg_type = MSG_TYPE_CMD; // Or MSG_TYPE_SHUTDOWN on quit
   msg.command_id = ++seq;
   strncpy(msg.payload, "LOAD R0 50", sizeof(msg.payload));
   mq_send(mq_ui, (char*)&msg, sizeof(msg), 0);
   ```
3. Read the execution response on `/pbl_core_to_ui` to get the result string and latency.

### B. For the Logger Process (Student 3 / 4 / Logger Developer)
1. Open the message queue `/pbl_core_to_logger` with `O_RDONLY`.
2. Read incoming logs:
   ```c
   IpcMessage log;
   mq_receive(mq_log, (char*)&log, sizeof(log), NULL);
   // log.command_id        -> Command sequence number
   // log.status_code       -> 0 for success, error code otherwise
   // log.execution_time_us -> Execution latency in microseconds
   // log.payload           -> Human-readable execution log
   ```

---

## 5. How to Compile & Run

### On Linux / WSL / POSIX (Standard Environment):
```bash
cd core/
make clean && make

# 1. Run Automated Unit Tests:
./core_process --test

# 2. Run Benchmarks (10,000 instructions):
./core_process --benchmark

# 3. Run Standalone Interactive Shell:
./core_process

# 4. Run Multi-Process IPC Mode:
# Terminal 1: Run Logger
./mock_logger

# Terminal 2: Run Core
./core_process --ipc

# Terminal 3: Run UI
./mock_ui
```

### Python Verification Runner:
```bash
python verify_core.py
```
