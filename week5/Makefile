# Makefile - Page Replacement Simulator (C, Linux)
#
#   make          build the program ./pagesim
#   make test     run the tests (none yet)
#   make clean    remove build/ and pagesim

# Recipe lines start with ">" instead of a Tab, so copy and paste cannot break it.
.RECIPEPREFIX := >

CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -O2 -g -Iinclude -D_POSIX_C_SOURCE=200809L

SRCS    := $(wildcard src/*.c)
OBJS    := $(patsubst src/%.c,build/%.o,$(SRCS))

.PHONY: all test clean

all: pagesim

pagesim: $(OBJS)
> $(CC) $(OBJS) -o $@

# -MMD/-MP track header dependencies: editing common.h rebuilds everything.
build/%.o: src/%.c | build
> $(CC) $(CFLAGS) -MMD -MP -c $< -o $@

build:
> mkdir -p build

test: all
> @echo "no tests yet"

clean:
> rm -rf build pagesim

-include $(OBJS:.o=.d)
