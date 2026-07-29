CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -g -Iinclude -O0

# library sources (main.c is NOT part of the library)
SRC = src/graph.c src/list.c src/star.c src/queue.c src/stack.c src/importer.c
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

run: bin/main
	./bin/main

# output directories, created on demand
bin build:
	mkdir -p $@

memcheck:
	valgrind --leak-check=full --track-origins=yes bin/main

clean:
	rm -rf build bin

.PHONY: all test run clean
