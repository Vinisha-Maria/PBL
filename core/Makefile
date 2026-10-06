CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -D_XOPEN_SOURCE=700
LDFLAGS = -lrt

SRCS = core.c cpu.c memory.c stack.c queue.c ipc_core.c
OBJS = $(SRCS:.c=.o)
TARGET = core_process

all: $(TARGET) mock_ui mock_logger

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

mock_ui: mock_ui.c ipc_core.c
	$(CC) $(CFLAGS) -o $@ mock_ui.c ipc_core.c $(LDFLAGS)

mock_logger: mock_logger.c ipc_core.c
	$(CC) $(CFLAGS) -o $@ mock_logger.c ipc_core.c $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TARGET)
	./$(TARGET) --test

benchmark: $(TARGET)
	./$(TARGET) --benchmark

clean:
	rm -f $(OBJS) $(TARGET) mock_ui mock_logger simulator.log

.PHONY: all test benchmark clean
