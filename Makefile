CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -g -Iinclude -O0

# library sources (main.c is NOT part of the library)
SRC = src/graph.c src/list.c src/star.c src/queue.c src/stack.c src/importer.c src/dheap.c \
      src/fibheap.c src/traversal.c src/sorting.c src/export.c
HDR = $(wildcard include/*.h)
OBJ = $(SRC:src/%.c=build/%.o)
LIB = bin/libgraph.a

all: $(LIB) bin/main

# your program, linked against the library
bin/main: src/main.c $(LIB) | bin
	$(CC) $(CFLAGS) $< -Lbin -lgraph -o $@

$(LIB): $(OBJ) | bin
	ar rcs $@ $^

build/%.o: src/%.c $(HDR) | build
	$(CC) $(CFLAGS) -c $< -o $@

# main.c reads res/ and writes out/, both relative to the project root
run: bin/main | out
	./bin/main

# output directories, created on demand
# (out/ holds the generated DOT/PNG exports and is not removed by `clean`)
bin build out:
	mkdir -p $@

memcheck:
	valgrind --leak-check=full --track-origins=yes bin/main

clean:
	rm -rf build bin

.PHONY: all test run clean
