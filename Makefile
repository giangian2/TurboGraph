CC      = gcc

# Optimization level, overridable from the command line:
#   make            ordinary build, -O0, easy to step through
#   make OPT=-O2    optimized build, required for profiling and measuring
# Changing OPT invalidates the objects already compiled (see $(OPTSTAMP)
# below), so build/ can never end up holding a mix of the two.
OPT     ?= -O0
CFLAGS  = -std=c11 -Wall -Wextra -g -Iinclude $(OPT)

# library sources (main.c is NOT part of the library)
SRC = src/graph.c src/list.c src/star.c src/queue.c src/stack.c src/importer.c src/dheap.c \
      src/fibheap.c src/traversal.c src/sorting.c src/export.c
HDR = $(wildcard include/*.h)
OBJ = $(SRC:src/%.c=build/%.o)
LIB = bin/libgraph.a

# Witness of the optimization level build/ was produced with: the file name
# embeds OPT, so switching OPT means it does not exist, the rule fires and
# throws the stale objects away before recompiling.
OPTSTAMP = build/.opt$(subst -,,$(OPT))

all: $(LIB) bin/main

# your program, linked against the library
bin/main: src/main.c $(LIB) | bin
	$(CC) $(CFLAGS) $< -Lbin -lgraph -o $@

$(LIB): $(OBJ) | bin
	ar rcs $@ $^

build/%.o: src/%.c $(HDR) $(OPTSTAMP) | build
	$(CC) $(CFLAGS) -c $< -o $@

$(OPTSTAMP): | build
	rm -f build/*.o build/.opt* $(LIB)
	touch $@

# main.c reads res/ and writes out/, both relative to the project root
run: bin/main | out
	./bin/main

# output directories, created on demand
# (out/ holds the generated DOT/PNG exports and is not removed by `clean`)
bin build out:
	mkdir -p $@

memcheck:
	valgrind --leak-check=full --track-origins=yes bin/main

# --- profiling ----------------------------------------------------------
# Callgrind counts the instructions actually executed and attributes them to
# the source line: it does not sample, so two runs give the exact same number.
# That is the only reliable way to answer "did my change reduce the work?" on
# a machine whose timing noise is +-3 ms out of ~22.
#
# ALWAYS profile at -O2: at -O0 the profile is dominated by functions that the
# real build inlines away (measured on graph_bfs: 195M instructions at -O0
# against 61M at -O2, with a different ranking of the hot spots).
#
#   make profile                                        whole program
#   make profile PROFFLAGS="-f graph_dfs -c -B"         DFS only, cache and branches
#   make profile PROFFLAGS="-f graph_dfs -s before"     store a baseline
#   make profile PROFFLAGS="-f graph_dfs -d before"     compare against it
#   tools/profile.sh --help                             every option
#
# Note: this target rebuilds at -O2 and therefore invalidates the -O0 build;
# the next plain `make` restores it.
PROFFLAGS ?=

profile: bin/cgdiff | out
	$(MAKE) --no-print-directory OPT=-O2 bin/main
	tools/profile.sh $(PROFFLAGS)

# Profile comparison tool. Standalone: it depends on nothing but libc, and is
# built at -O2 regardless of OPT since it is never the thing being debugged.
bin/cgdiff: tools/cgdiff.c | bin
	$(CC) -std=c11 -Wall -Wextra -O2 $< -o $@

clean:
	rm -rf build bin

.PHONY: all test run clean memcheck profile
