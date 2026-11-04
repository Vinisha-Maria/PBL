CC      = gcc
CFLAGS  = -Wall -Wextra -pthread -g -O2
LDLIBS  = -lrt -pthread

MODS_ALL = ipc_mq.c ipc_shm.c ipc_fifo.c ipc_sock.c
HDRS     = $(wildcard *.h)

# Members' own unit tests: name them test_ipc_<anything>.c and `make test` picks them up.
TEST_SRCS = $(wildcard test_ipc_*.c)
TEST_BINS = $(TEST_SRCS:.c=)

MOCKS = mock/logger_process mock/core_process mock/ui_process

.PHONY: all run test bench mock mocktest clean

all: logger_process core_process ui_process sim_launcher test_sock bench_ipc bench_runner $(TEST_BINS)

logger_process: logger_process.c ipc_fifo.c ipc_sock.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ logger_process.c ipc_fifo.c ipc_sock.c $(LDLIBS)

core_process: core_process.c ipc_mq.c ipc_shm.c ipc_fifo.c ipc_sock.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ core_process.c ipc_mq.c ipc_shm.c ipc_fifo.c ipc_sock.c $(LDLIBS)

ui_process: ui_process.c ipc_mq.c ipc_shm.c ipc_sock.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ ui_process.c ipc_mq.c ipc_shm.c ipc_sock.c $(LDLIBS)

sim_launcher: sim_launcher.c ipc_sock.c ipc_sock.h
	$(CC) $(CFLAGS) -o $@ sim_launcher.c ipc_sock.c $(LDLIBS)

test_sock: test_sock.c ipc_sock.c ipc_sock.h
	$(CC) $(CFLAGS) -o $@ test_sock.c ipc_sock.c $(LDLIBS)

test_ipc_%: test_ipc_%.c $(MODS_ALL) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $< $(MODS_ALL) $(LDLIBS)

bench_ipc: bench_ipc.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

bench_runner: bench_runner.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

# ---- mock processes: test launcher/control channel before the real modules exist ----
mock: $(MOCKS)
mock/%_process: mock_process.c ipc_sock.c ipc_sock.h
	mkdir -p mock
	$(CC) $(CFLAGS) -o $@ mock_process.c ipc_sock.c $(LDLIBS)

mocktest: mock sim_launcher test_sock
	./test_sock
	MODE=mock ./test_integration.sh

# ---- main targets ----
run: all
	./sim_launcher

test: all
	./test_sock
	@for t in $(TEST_BINS); do echo "== $$t"; ./$$t || exit 1; done
	./test_integration.sh

bench: all
	./bench.sh 10 100000

clean:
	rm -rf logger_process core_process ui_process sim_launcher test_sock bench_ipc bench_runner mock $(TEST_BINS) *.o
	rm -f results_*.csv results_overhead.txt
