# Makefile for the CPU Process Scheduling Simulator
# Usage:  make        -> build ./scheduler
#         make run    -> build and run
#         make clean  -> remove build files

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99
SRC     = $(wildcard src/*.c)
OBJ     = $(patsubst src/%.c,build/%.o,$(SRC))
HEADERS = $(wildcard src/*.h)
TARGET  = scheduler

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

build/%.o: src/%.c $(HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf build $(TARGET) $(TARGET).exe

.PHONY: all run clean
