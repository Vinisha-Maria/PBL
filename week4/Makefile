# Deadlock Simulation System - Makefile (owner: Team Lead)
# Targets: make | make test | make debug | make clean
# Teammates never need to edit this file: every src/**/*.c is picked up automatically.

CC      ?= gcc
CFLAGS  += -std=c11 -Wall -Wextra -Isrc -Isrc/core -Isrc/gui
LDLIBS  += -lm

# GTK flags are only evaluated when building the GUI app (not for `make test`)
GTK_CFLAGS = $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS   = $(shell pkg-config --libs gtk+-3.0)

SRC      := $(shell find src -name '*.c')
OBJ      := $(patsubst src/%.c,build/obj/%.o,$(SRC))
DEP      := $(OBJ:.o=.d)
CORE_SRC := $(wildcard src/core/*.c)
TEST_SRC := $(wildcard tests/test_*.c)
TARGET   := build/deadlock_sim

.PHONY: all test debug clean run

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(GTK_LIBS) $(LDLIBS)

build/obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -MMD -MP -c $< -o $@

run: all
	./$(TARGET)

# Compiles each tests/test_*.c with src/core/*.c only (no GTK) and runs it.
test:
	@mkdir -p build/tests
	@fail=0; for t in $(TEST_SRC); do \
	  n=$$(basename $$t .c); \
	  if $(CC) $(CFLAGS) -fsanitize=address -g -o build/tests/$$n $$t $(CORE_SRC) $(LDLIBS) && ./build/tests/$$n; \
	  then echo "PASS: $$n"; else echo "FAIL: $$n"; fail=1; fi; \
	done; exit $$fail

# Debug build with AddressSanitizer
debug: CFLAGS  += -g -fsanitize=address -fno-omit-frame-pointer
debug: LDFLAGS += -fsanitize=address
debug: clean all

clean:
	rm -rf build

-include $(DEP)
