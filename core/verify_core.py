"""
Automated Simulator Verification & Benchmark Runner
Mirrors the C Core Process logic for direct validation and testing.
"""

import time

MEMORY_SIZE = 1024
STACK_CAPACITY = 256
QUEUE_CAPACITY = 128
NUM_REGISTERS = 4

class Memory:
    def __init__(self):
        self.data = [0] * MEMORY_SIZE
        self.read_count = 0
        self.write_count = 0

    def read(self, addr):
        if not (0 <= addr < MEMORY_SIZE):
            raise IndexError(f"Memory fault at address {addr}")
        self.read_count += 1
        return self.data[addr]

    def write(self, addr, val):
        if not (0 <= addr < MEMORY_SIZE):
            raise IndexError(f"Memory fault at address {addr}")
        self.data[addr] = val
        self.write_count += 1

class Stack:
    def __init__(self):
        self.items = []
        self.push_count = 0
        self.pop_count = 0

    def push(self, val):
        if len(self.items) >= STACK_CAPACITY:
            raise OverflowError("Stack Overflow")
        self.items.append(val)
        self.push_count += 1

    def pop(self):
        if not self.items:
            raise IndexError("Stack Underflow")
        self.pop_count += 1
        return self.items.pop()

    def peek(self):
        if not self.items:
            raise IndexError("Cannot peek empty stack")
        return self.items[-1]

class Queue:
    def __init__(self):
        self.items = []
        self.enqueue_count = 0
        self.dequeue_count = 0

    def enqueue(self, item):
        if len(self.items) >= QUEUE_CAPACITY:
            raise OverflowError("Queue Overflow")
        self.items.append(item)
        self.enqueue_count += 1

    def dequeue(self):
        if not self.items:
            raise IndexError("Queue Underflow")
        self.dequeue_count += 1
        return self.items.pop(0)

class CPU:
    def __init__(self):
        self.registers = [0] * NUM_REGISTERS
        self.pc = 0
        self.flag_zero = False
        self.flag_negative = False
        self.halted = False
        self.cycles = 0

    def get_reg_idx(self, name):
        name = name.upper()
        if name.startswith('R') and name[1:].isdigit():
            idx = int(name[1:])
            if 0 <= idx < NUM_REGISTERS:
                return idx
        return -1

    def parse_operand(self, token):
        r = self.get_reg_idx(token)
        if r != -1:
            return self.registers[r]
        return int(token)

    def execute(self, mem: Memory, stack: Stack, instruction: str):
        if self.halted:
            return "CPU_HALTED", "CPU is halted"

        tokens = instruction.strip().split()
        if not tokens or tokens[0].startswith('#'):
            return "NOOP", "Comment/empty"

        opcode = tokens[0].upper()
        self.cycles += 1
        init_pc = self.pc

        try:
            if opcode == "LOAD":
                r = self.get_reg_idx(tokens[1])
                arg = tokens[2]
                if arg.startswith('[') and arg.endswith(']'):
                    addr = int(arg[1:-1])
                    val = mem.read(addr)
                else:
                    val = self.parse_operand(arg)
                self.registers[r] = val
                self.pc += 1
                return "SUCCESS", f"PC={init_pc} | LOAD R{r} = {val}"

            elif opcode == "STORE":
                addr = int(tokens[1].strip('[]'))
                val = self.parse_operand(tokens[2])
                mem.write(addr, val)
                self.pc += 1
                return "SUCCESS", f"PC={init_pc} | STORE Mem[{addr}] = {val}"

            elif opcode == "MOV":
                r_dst = self.get_reg_idx(tokens[1])
                val = self.parse_operand(tokens[2])
                self.registers[r_dst] = val
                self.pc += 1
                return "SUCCESS", f"PC={init_pc} | MOV R{r_dst} = {val}"

            elif opcode == "ADD":
                r_dst = self.get_reg_idx(tokens[1])
                val = self.parse_operand(tokens[2])
                self.registers[r_dst] += val
                self.pc += 1
                return "SUCCESS", f"PC={init_pc} | ADD R{r_dst} = {self.registers[r_dst]}"

            elif opcode == "SUB":
                r_dst = self.get_reg_idx(tokens[1])
                val = self.parse_operand(tokens[2])
                self.registers[r_dst] -= val
                self.pc += 1
                return "SUCCESS", f"PC={init_pc} | SUB R{r_dst} = {self.registers[r_dst]}"

            elif opcode == "MUL":
                r_dst = self.get_reg_idx(tokens[1])
                val = self.parse_operand(tokens[2])
                self.registers[r_dst] *= val
                self.pc += 1
                return "SUCCESS", f"PC={init_pc} | MUL R{r_dst} = {self.registers[r_dst]}"

            elif opcode == "DIV":
                r_dst = self.get_reg_idx(tokens[1])
                divisor = self.parse_operand(tokens[2])
                if divisor == 0:
                    return "DIV_BY_ZERO", "Division by zero"
                self.registers[r_dst] //= divisor
                self.pc += 1
                return "SUCCESS", f"PC={init_pc} | DIV R{r_dst} = {self.registers[r_dst]}"

            elif opcode == "PUSH":
                val = self.parse_operand(tokens[1])
                stack.push(val)
                self.pc += 1
                return "SUCCESS", f"PC={init_pc} | PUSH {val}"

            elif opcode == "POP":
                r = self.get_reg_idx(tokens[1])
                val = stack.pop()
                self.registers[r] = val
                self.pc += 1
                return "SUCCESS", f"PC={init_pc} | POP R{r} = {val}"

            elif opcode == "HALT":
                self.halted = True
                return "SUCCESS", f"PC={init_pc} | CPU Halted"

            elif opcode == "DUMP":
                return "SUCCESS", f"State: R={self.registers}, PC={self.pc}, Stack={stack.items}"

            else:
                return "INVALID_OPCODE", f"Unknown opcode {opcode}"

        except Exception as e:
            return "ERROR", str(e)


def run_tests():
    print("=" * 60)
    print("      STUDENT 2 CORE PROCESS SIMULATION VERIFICATION")
    print("=" * 60)
    cpu = CPU()
    mem = Memory()
    stack = Stack()
    queue = Queue()

    test_instructions = [
        "LOAD R0 100",
        "LOAD R1 25",
        "ADD R0 R1",
        "STORE [50] R0",
        "PUSH R0",
        "LOAD R2 15",
        "PUSH R2",
        "POP R3",
        "POP R1",
        "SUB R1 R3",
        "LOAD R0 [50]",
        "DUMP",
        "HALT"
    ]

    print(f"\n[1] Staging {len(test_instructions)} instructions into Ready Queue...")
    for inst in test_instructions:
        queue.enqueue(inst)

    print(f"    Queue count: {len(queue.items)}")

    print("\n[2] Executing instructions through CPU Engine:")
    step = 1
    while queue.items:
        inst = queue.dequeue()
        status, msg = cpu.execute(mem, stack, inst)
        print(f"    Step {step:02d}: [{status}] {msg}")
        step += 1

    # Assertions
    assert cpu.registers[0] == 125, f"Expected R0=125, got {cpu.registers[0]}"
    assert cpu.registers[3] == 15, f"Expected R3=15, got {cpu.registers[3]}"
    assert cpu.registers[1] == 110, f"Expected R1=110, got {cpu.registers[1]}"
    assert mem.data[50] == 125, f"Expected Mem[50]=125, got {mem.data[50]}"
    print("\n[PASS] All assertions verified! CPU, Memory, Stack, and Queue work seamlessly.")

    print("\n[3] Running Benchmark (10,000 operations):")
    bench_start = time.perf_counter()
    for _ in range(10000):
        cpu.execute(mem, stack, "LOAD R0 42")
        cpu.execute(mem, stack, "STORE [10] R0")
        cpu.execute(mem, stack, "PUSH R0")
        cpu.execute(mem, stack, "POP R1")
        cpu.execute(mem, stack, "ADD R1 8")
    bench_duration = time.perf_counter() - bench_start
    total_ops = 50000
    print(f"    Total Ops: {total_ops}")
    print(f"    Total Time: {bench_duration*1000:.2f} ms")
    print(f"    Throughput: {total_ops / bench_duration:,.0f} ops/sec")
    print("=" * 60)

def run_interactive():
    print("=" * 60)
    print("   CORE PROCESS SIMULATOR - INTERACTIVE SHELL (WINDOWS)")
    print("   Type instructions (e.g. 'LOAD R0 10', 'ADD R0 5', 'DUMP')")
    print("   Type 'HELP', 'TEST', 'BENCHMARK', or 'EXIT' to quit.")
    print("=" * 60)
    cpu = CPU()
    mem = Memory()
    stack = Stack()
    queue = Queue()

    while True:
        try:
            line = input("\nCore> ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\nExiting.")
            break

        if not line:
            continue
        cmd = line.upper()
        if cmd in ("EXIT", "QUIT"):
            print("Simulator shutdown.")
            break
        elif cmd == "HELP":
            print("\nCommands supported:")
            print("  LOAD Rx <val>   | LOAD Rx [addr]")
            print("  STORE [addr] Rx | STORE [addr] <val>")
            print("  MOV Rx Ry")
            print("  ADD Rx Ry/<val> | SUB Rx Ry/<val>")
            print("  MUL Rx Ry/<val> | DIV Rx Ry/<val>")
            print("  PUSH Rx/<val>   | POP Rx | PEEK Rx")
            print("  DUMP            | HALT")
            print("  TEST            | BENCHMARK")
            continue
        elif cmd == "TEST":
            run_tests()
            continue
        elif cmd == "BENCHMARK":
            bench_start = time.perf_counter()
            for _ in range(10000):
                cpu.execute(mem, stack, "LOAD R0 42")
                cpu.execute(mem, stack, "STORE [10] R0")
                cpu.execute(mem, stack, "PUSH R0")
                cpu.execute(mem, stack, "POP R1")
                cpu.execute(mem, stack, "ADD R1 8")
            d = time.perf_counter() - bench_start
            print(f"Benchmark: 50,000 ops completed in {d*1000:.2f} ms ({50000/d:,.0f} ops/sec)")
            continue

        # Enqueue first to simulate Ready Queue
        queue.enqueue(line)
        inst = queue.dequeue()
        status, msg = cpu.execute(mem, stack, inst)
        print(f"[{status}] {msg}")
        if status == "SUCCESS" and "DUMP" in cmd:
            print(f"  Registers: R0={cpu.registers[0]}, R1={cpu.registers[1]}, R2={cpu.registers[2]}, R3={cpu.registers[3]}")
            print(f"  PC={cpu.pc}, Cycles={cpu.cycles}, Stack={stack.items}")

if __name__ == "__main__":
    import sys
    if len(sys.argv) > 1 and sys.argv[1] in ("-i", "--interactive", "interactive"):
        run_interactive()
    elif len(sys.argv) > 1 and sys.argv[1] in ("-b", "--benchmark"):
        cpu, mem, stack = CPU(), Memory(), Stack()
        bench_start = time.perf_counter()
        for _ in range(10000):
            cpu.execute(mem, stack, "LOAD R0 42")
            cpu.execute(mem, stack, "STORE [10] R0")
            cpu.execute(mem, stack, "PUSH R0")
            cpu.execute(mem, stack, "POP R1")
            cpu.execute(mem, stack, "ADD R1 8")
        d = time.perf_counter() - bench_start
        print(f"50,000 ops completed in {d*1000:.2f} ms ({50000/d:,.0f} ops/sec)")
    else:
        run_tests()
        print("\nTip: To run the interactive shell where you can type commands, run:")
        print("     python verify_core.py --interactive")

