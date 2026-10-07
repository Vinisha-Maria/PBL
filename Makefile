# Makefile - OS process scheduler simulator (POSIX message queues, Linux only)
#
#   make              build bin/ui, bin/core, bin/logger
#   make run          build and start all three (see run.sh --help)
#   make clean        remove build/ and bin/
#   make clean-ipc    remove stale message queues left by a crashed run
#   make clean-logs   remove simulator.log, errors.log and run/
#   make distclean    all of the above

CC      := gcc
CFLAGS  := -std=gnu11 -Wall -Wextra -O2 -g -Iinclude
LDFLAGS := -pthread
LDLIBS  := -lrt

SRC     := src
BUILD   := build
BIN     := bin

PROGS   := ui core logger
TARGETS := $(addprefix $(BIN)/,$(PROGS))

.PHONY: all run clean clean-ipc clean-logs distclean $(PROGS)

all: $(TARGETS)

# Convenience aliases: `make core` etc.
$(PROGS): %: $(BIN)/%

# Every program links with the shared IPC wrapper.
$(BIN)/%: $(BUILD)/%.o $(BUILD)/ipc.o | $(BIN)
	$(CC) $(LDFLAGS) $^ -o $@ $(LDLIBS)

# -MMD/-MP track header dependencies, so editing common.h or ipc.h
# rebuilds everything that includes it.
$(BUILD)/%.o: $(SRC)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD) $(BIN):
	mkdir -p $@

# Keep the .o files so a rebuild only recompiles what changed.
.SECONDARY:

run: all
	./run.sh

clean:
	rm -rf $(BUILD) $(BIN)

clean-ipc:
	rm -f /dev/mqueue/sched_cmd /dev/mqueue/sched_resp /dev/mqueue/sched_log

clean-logs:
	rm -rf simulator.log errors.log run

distclean: clean clean-ipc clean-logs

-include $(wildcard $(BUILD)/*.d)

