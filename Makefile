CC      = gcc

# Optimization level, overridable from the command line:
#   make            ordinary build, -O0, easy to step through
#   make OPT=-O2    optimized build, required for profiling and measuring
# Changing OPT invalidates the objects already compiled (see $(OPTSTAMP)
# below), so build/ can never end up holding a mix of the two.
OPT     ?= -O0

# Structured debug logging (see include/Debug.h), off by default:
#   make            LOG_DEBUG/LOG_ERROR compile to nothing
#   make DEBUG=1    -DDEBUG, every LOG_DEBUG/LOG_ERROR prints to stderr
DEBUG   ?= 0
CFLAGS  = -std=c11 -Wall -Wextra -g -Iinclude $(OPT)
ifeq ($(DEBUG),1)
CFLAGS += -DDEBUG
endif

# library sources (main.c is NOT part of the library)
# DHeap.h, FibHeap.h, UnionFInd.h and hashmap.h are single headers: nothing to compile
SRC = src/graph.c src/list.c src/star_impl.c src/matrix_impl.c src/queue.c src/stack.c \
      src/importer.c src/traversal.c src/export.c src/sorting.c
HDR = $(wildcard include/*.h)
OBJ = $(SRC:src/%.c=build/%.o)
LIB = bin/libgraph.a

# Witness of the optimization level (and DEBUG setting) build/ was produced
# with: the file name embeds both, so switching either means it does not
# exist, the rule fires and throws the stale objects away before recompiling.
OPTSTAMP = build/.opt$(subst -,,$(OPT))-debug$(DEBUG)

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

# --- WebAssembly ---------------------------------------------------------
# The same library sources plus the flat binding in wasm/wasm_api.c,
# compiled with Emscripten into an ES module (bin/wasm/graphs.mjs + .wasm)
# that wasm/turbograph.js wraps. Always -O2: it also drops the unused
# MST_AVAILABLE_ALGORITHMS table that would otherwise need every MST symbol.
# Memory is capped at 2GB so every pointer stays a positive int on the JS
# side (WesternUSA, the largest input, peaks well below that).
#
#   make wasm          build bin/wasm/graphs.{mjs,wasm} (needs emcc on PATH)
#   make wasm-serve    serve the repo root, then open /wasm/demo.html
EMCC     ?= emcc
WASM_DIR  = bin/wasm
WASM_MJS  = $(WASM_DIR)/graphs.mjs
EMFLAGS   = -std=c11 -O2 -Iinclude \
            -sMODULARIZE -sEXPORT_ES6 -sEXPORT_NAME=createGraphsModule \
            -sALLOW_MEMORY_GROWTH -sMAXIMUM_MEMORY=2GB -sFORCE_FILESYSTEM \
            -sEXPORTED_FUNCTIONS=_malloc,_free \
            -sEXPORTED_RUNTIME_METHODS=FS,HEAPU8,HEAP32,HEAPF64,stringToNewUTF8

wasm: $(WASM_MJS)

$(WASM_MJS): $(SRC) wasm/wasm_api.c $(HDR) Makefile
	mkdir -p $(WASM_DIR)
	$(EMCC) $(EMFLAGS) $(SRC) wasm/wasm_api.c -o $@

wasm-serve: $(WASM_MJS)
	@echo "open http://localhost:8000/wasm/demo.html"
	python3 -m http.server 8000

clean:
	rm -rf build bin

.PHONY: all test run clean memcheck profile wasm wasm-serve
