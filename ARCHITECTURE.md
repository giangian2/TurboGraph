# TurboGraph Architecture: High-Performance Concurrent Graph Engine via Native Assembly Coroutines

This document outlines the low-level architectural design for evolving the `TurboGraph` engine into an ultra-fast, concurrent database server. By avoiding heavy C++ runtimes and high-overhead operating system context switches, this architecture achieves maximum hardware utilization through **Custom Assembly Coroutines** and a **Worker Thread Pool (POSIX Pthreads)**, operating entirely in C.

The core of `TurboGraph` relies on an ultra-dense **CSR (Compressed Sparse Row)** layout (`GRAPH_STAR`, see `src/star.c` and the `Star` header in `Graph.h`) designed to optimize CPU cache locality. To serve multiple network clients simultaneously without freezing the system, this design wraps native C graph traversals inside an asynchronous, cooperative control flow.

The guiding constraint of the whole design is stated up front, because everything else follows from it:

> **The graph algorithms stay pure C.** `graph_bfs()`, `graph_dfs()` and every algorithm that comes after them must remain plain, linear, single-threaded C functions that know nothing about sockets, threads, schedulers or coroutines. The concurrency layer wraps them; it does not rewrite them.

Sections 1-4 describe the model. Sections 5-12 answer the question that decides whether the model is viable: *how much of the existing algorithm code has to change, and where exactly does a coroutine instruction go?* The short answer is: **the algorithm bodies do not change; one no-op-by-default checkpoint macro is added to each unbounded loop, and the library's five `malloc` call sites are routed through an allocator hook.** Everything else lives outside `src/traversal.c`.

---

## 1. The Architectural Problem: CPU-Bound Traversals

Graph traversal algorithms, such as **BFS** and **DFS**, are intensely CPU-bound. When running over large CSR structures, they execute long sequential loops that monopolize CPU cores until the entire query or path-matching operation is complete.

In a high-throughput network server environment, traditional models fail:
* **Thread-per-Connection:** Spawning a full OS thread (`pthread`) for every client query wastes massive amounts of RAM on isolated stacks (megabytes per thread, 8 MB by default on glibc). It also introduces severe kernel overhead due to preemptive *Context Switching* under heavy concurrency, and every switch flushes the L1/L2 working set the traversal had just warmed up.
* **Single-Threaded Event Loop (Classic Redis Style):** While a single thread handles O(1) memory lookups efficiently, it cannot handle graph traversals. If one client triggers a massive BFS, the entire event loop freezes for milliseconds, causing network timeout spikes for all other connected users.

---

## 2. The Solution: Low-Level Stackful Coroutines (C & Assembly x86_64)

To achieve non-blocking execution while maintaining a lightweight C footprint, `TurboGraph` implements **User-Space Stackful Coroutines (Fibers)** written directly in x86_64 Assembly.

Instead of relying on heavy runtime libraries or POSIX `<ucontext.h>` (whose `swapcontext()` performs an `rt_sigprocmask` syscall on every switch to save the signal mask), this engine uses a custom Assembly context-switch routine of roughly fifteen instructions. It swaps only the *callee-saved* CPU registers (`RBP`, `RBX`, `R12`-`R15`), the *Stack Pointer* (`RSP`) and the return address; the exact contract is in section 9.

### Key Operational Advantages:
* **True Stackful Concurrency:** Every query receives a small, pre-allocated private stack (16-64 KB, see section 8). Because the coroutine owns a real machine stack, a C function running inside it can be suspended *anywhere*, with all of its locals, iterators and pending calls intact. The algorithm is never turned into a state machine; it does not even know it was suspended.
* **Nanosecond Context Switches:** The register swap itself costs on the order of **10-20 cycles**. In practice a switch costs tens of nanoseconds, dominated not by the swap but by the cache and TLB misses on the stack being resumed. Both figures are two to three orders of magnitude below a kernel thread switch.
* **Cooperative, budgeted yielding:** A traversal periodically reaches a *checkpoint* that decrements a per-query work budget and, when it hits zero, hands the CPU back to the worker. Long queries are sliced; short queries are not starved. (Section 7 explains why this is a different thing from *streaming* results, and why the two must not be conflated.)

### Why stackful, and not the alternatives

| Approach | Algorithm code | Switch cost | Stack per query | Verdict |
|---|---|---|---|---|
| **Stackful, custom asm** (chosen) | unchanged | ~10-20 cycles | 16-64 KB, pooled | fits the purity constraint |
| `ucontext` (`swapcontext`) | unchanged | ~1 µs (syscall per switch) | same | correct but 50-100x slower switch |
| Stackless / protothreads / Duff's device | rewritten as a state machine; no locals across yields | ~1 cycle | none | violates the purity constraint |
| Resumable "step" API (`bfs_step()`) | every algorithm re-designed around an explicit state object | ~1 cycle | none | violates the purity constraint |
| OS thread per query | unchanged | ~1-5 µs + cache pollution | 8 MB virtual | the problem statement |

The stackless options are the fastest possible, but only by moving every local variable of every algorithm into a hand-written struct. That is exactly the rewrite this design refuses. The stackful model is the only one whose cost is paid entirely *outside* `src/traversal.c`.

---

## 3. Core Components & Runtime Flow

The engine orchestrates execution by dividing responsibilities into three distinct software layers working in harmony:

### 1. The Network Event Loop
Operating on a dedicated thread, it uses non-blocking system calls (such as Linux `epoll` or `io_uring`) to accept raw sockets and ingest queries. It never touches the CSR graph directly. When a query arrives, it initializes a lightweight `TurboCo` coroutine object from a pool, assigns it a private stack and a memory arena (section 8), and hands it to the task queue.

### 2. The Shared Task Queue
Runnable query states (`TurboCo*`) are passed into a fast, thread-safe queue. If a coroutine is suspended during computation by a checkpoint, its pointer is re-enqueued here to wait for its next execution slice.

A coroutine is in exactly one of four states:

| State | Meaning | Who owns it |
|---|---|---|
| `RUNNABLE` | in the task queue, waiting for a worker | the queue |
| `RUNNING` | a worker is executing on its stack | that worker |
| `WAITING` | suspended on something other than CPU (output buffer full, backpressure) | the network thread |
| `DONE` / `CANCELLED` | finished or abandoned; stack and arena go back to their pools | the pool |

`WAITING` is what keeps workers from ever blocking on a socket: a coroutine that produces output faster than the client drains it parks itself instead of parking the worker.

### 3. The Worker Thread Pool
A fixed pool of native `pthreads` is pinned to the physical CPU cores to match hardware concurrency exactly.
* A worker thread pulls a `RUNNABLE` `TurboCo` from the queue and calls `turbo_co_resume()`.
* The Assembly routine shifts the worker's CPU registers onto the coroutine's private stack, running the native C traversal over the CSR structure at raw speed.
* **Thread Yielding & Migration:** When the query's budget runs out it yields. Control returns to the worker *on the worker's own stack*; only then does the worker push the coroutine back to the queue and pick up another client's query. The ordering matters and is spelled out in section 10.

A coroutine that yields on worker A may well be resumed on worker B. That is deliberate (it is what makes the pool self-balancing) and it is the source of every subtle rule in section 10.

---

## 4. Architectural Summary

By implementing an **M:N hybrid concurrency model** (mapping *M* lightweight Assembly coroutines onto *N* physical worker threads), `TurboGraph` achieves the following design metrics:
1. **No `malloc` on the hot path:** Query states, stacks and arenas are managed within fixed, pooled blocks of memory.
2. **Full core utilization:** Workers never sleep on I/O or kernel scheduling; they only ever wait on the task queue, and only when there is no query to run.
3. **Clean C Codebase:** High-performance graph traversal algorithms remain written in pure, linear C code, completely isolated from network logic and concurrency states.

---

## 5. What the algorithms look like today

The design above only works if the algorithms actually satisfy its assumptions, so this section records what `src/traversal.c` looks like at the time of writing.

* **Both traversals are already iterative.** `graph_bfs()` runs a `while` over a FIFO queue; `graph_dfs()` is a recursive DFS *unrolled onto an explicit stack of frames*, each frame carrying its own `GraphIter` cursor. Neither function recurses. The only recursive function in the library is `QuickSortKruskalMST()` in `src/sorting.c`.
* **All algorithm state lives on the heap, not on the C stack.** The visited/parent/distance arrays (`Traversal`), the BFS queue (`queue_create(int, g->n)`) and the DFS frame stack (`stack_create(dfs_frame, g->n)`) are all `malloc`'d, sized `n` up front, fixed capacity. What remains on the machine stack is a handful of pointers, two `int`s and one 32-byte `GraphIter`: well under 200 bytes per traversal.
* **The only entry point into the data layer is the `GraphOps` vtable.** Every neighbor access goes through `g->ops->iter_out()` / `g->ops->iter_next()`. There is no other coupling between an algorithm and a representation.
* **No global or static mutable state, no thread-local storage, no locks, no I/O**, apart from `LOG_DEBUG`/`LOG_ERROR`, which compile to nothing unless `DEBUG=1`.
* **Results are returned as a heap-allocated `Traversal*`** owned by the caller (`AUTO_FREE_TRAVERSAL` wraps it in a cleanup attribute).
* **The CSR (`GRAPH_STAR`) is immutable and position-independent**: one contiguous block, offsets instead of pointers, never mutated after `star_init_from_edges()`.

Two consequences follow immediately. First, the "preserves deep recursive stack frames" argument that usually motivates stackful coroutines does not apply here: the traversals need almost no stack. The real reason to go stackful is the one in the table above: not having to rewrite the loops as state machines. Second, the per-query memory that matters is **not the coroutine stack** but the three `n`-sized arrays plus the `n`-sized container. Section 8 deals with that.

---

## 6. Does the pure code have to change? The purity contract

Strictly speaking, **no**: a function like `graph_bfs()` can be called inside a coroutine today, unchanged, and it will run to completion on the coroutine's stack. That is milestone M1 in section 12 and it already gives N-way parallelism across the worker pool.

What the unchanged code cannot do is *yield in the middle*. Cooperative scheduling means somebody has to call the switch, and a loop that never calls it holds its worker until it finishes. So the honest formulation of the question is not "does the code change?" but "what is the *minimum* set of properties an algorithm must have so the scheduler can slice it, and which of them does the current code already satisfy?"

That set is the **purity contract**. An algorithm that honors it can be run inside a `TurboCo`, suspended, migrated to another worker, resumed, and abandoned mid-flight, without any further cooperation.

| # | Rule | Status in `src/traversal.c` today |
|---|---|---|
| P1 | No global or `static` mutable state | satisfied |
| P2 | No thread-local storage read on both sides of a checkpoint | satisfied (uses none) |
| P3 | No lock, mutex or spinlock held across a checkpoint | satisfied (uses none) |
| P4 | Bounded machine-stack usage; no unbounded recursion | satisfied for BFS/DFS; **violated by `QuickSortKruskalMST()`** (worst-case O(n) depth) |
| P5 | Every heap allocation goes through the library allocator, never bare `malloc`/`free` | **not yet**: 5 direct `malloc` sites (`traversal_new`, `queue__create`, `stack__create`) |
| P6 | Every unbounded loop contains exactly one checkpoint | **not yet**: 0 checkpoints |
| P7 | The `Graph*` argument is a `GRAPH_STAR` snapshot that outlives the coroutine | enforced by the server, not by the algorithm (section 11) |

P1-P4 are already true and only need to be kept true (P4 means the Kruskal sort becomes an introsort or an explicit-stack quicksort before it is ever served through a coroutine). P5 is a mechanical change to three files that touches no algorithmic line. P6 is the only change that goes *inside* an algorithm body, and it is one line per loop. Both are described next.

---

## 7. Where the yield goes: three insertion strategies

There are exactly three places a yield can be injected without turning the algorithm into a state machine. They were all considered; the second is the one adopted.

### Strategy A: yield in the data layer (a "yielding `Graph` view")

Wrap the real graph in a second `Graph` shell whose `GraphOps` delegate to the inner representation, and let the wrapper's `iter_next()` count calls and yield every K of them. Since every algorithm reaches the data only through the vtable, this gives **zero lines changed in any algorithm, ever**, including future ones.

It was rejected, narrowly, for three reasons:
* the yield point lives in the wrong layer: a *data structure* should not know about *scheduling*, and every representation would need the same shim;
* `iter_next()` is the single hottest function in the profile (see the README's callgrind notes), and the shim adds one more indirect call in front of it;
* the budget is measured in edges rather than in whatever unit the algorithm considers a "step", which is the wrong unit for anything that is not a traversal (a heap-based Dijkstra spends most of its time in the heap, not in `iter_next`).

It remains a valid fallback for third-party algorithms that cannot be edited.

### Strategy B: an explicit checkpoint macro in the algorithm (adopted)

Add a single header, `include/Coro.h`, that exposes a macro:

```c
/* Cooperative scheduling point. Expands to nothing in a plain library
 * build; in a server build (make CO=1) it charges one unit to the running
 * coroutine's budget and switches out when the budget is exhausted. Never
 * observable by the algorithm: it returns when the query is resumed. */
turbo_co_checkpoint();
```

and place it once at the top of each unbounded loop:

* `graph_bfs()`: at the top of the `while (!queue_is_empty(q))` body, i.e. once per **dequeued vertex**;
* `graph_dfs()`: at the top of the `while (!stack_is_empty(stack))` body, i.e. once per **edge or backtrack step** (that loop is the only loop);
* future algorithms: once per pop of the priority queue (Dijkstra, Prim), once per edge of the sorted list (Kruskal), once per outer iteration (PageRank-style sweeps).

This is the same mechanism the codebase already uses for `LOG_DEBUG`: a macro that is `((void)0)` unless a build flag is set, so the library and its tests are byte-for-byte unaffected. Cost when enabled: one decrement of a counter and one well-predicted branch per iteration, next to an indirect call and a memory access. On the `graph_dfs` profile that is under 2% of instructions and no additional cache traffic.

Why once per loop iteration rather than once per vertex in DFS: a vertex with a million out-edges that are all already discovered would otherwise spin a million iterations between checkpoints. Charging per iteration keeps the slice length proportional to *work done*, which is what fairness needs.

Why a budget in iterations rather than in time: reading the TSC costs 20-40 cycles and is not free to do per edge. The budget counts iterations; if wall-clock slices are ever wanted, the checkpoint samples `rdtsc` only when the iteration counter crosses a power of two.

### Strategy C: yield at result production (streaming)

Call the switch every time a vertex is *discovered*, passing its id to the network layer, so results stream out as they are found instead of being materialized.

This is a **different kind of yield** and must be kept separate from B:
* B is a *scheduling* yield: budgeted, carries no value, invisible to the algorithm, and returns immediately on resume.
* C is a *generator* yield: one per result, carries a value, and changes the algorithm's contract from "returns a `Traversal*`" to "produces a sequence".

The original version of this document claimed that C "eliminates the need to allocate the `Traversal*`". That is not correct: `dist[]` (or a bitmap derived from it) *is* the visited set; a BFS cannot run without it. Streaming can drop `order[]` and, for reachability-only queries, shrink `parent[]`/`dist[]` to `n` bits, but it cannot make the per-query O(n) state disappear.

Streaming is therefore deferred and, when it comes, it comes as a visitor hook (`on_discover(v, ctx)`, NULL by default) that the *coroutine's entry function* may bind to a generator yield. The algorithm body still contains a single extra line. There is also a zero-line variant worth noting: since `order[]` is append-only and `count` is monotonic, the network side can flush `order[sent..count)` between two slices of a suspended coroutine, if `traversal_new()` publishes the `Traversal*` on the current coroutine. That is a change to the infrastructure function, not to the traversal.

### The decision, in one table

| | Lines touched in an algorithm | Hot-path cost | Unit of budget | Layer that knows about scheduling |
|---|---|---|---|---|
| A: data-layer shim | 0 | +1 indirect call per edge | edges | data structure (wrong) |
| **B: checkpoint macro** | **1 per unbounded loop** | **dec + branch per iteration** | **algorithm-defined step** | **algorithm, opt-in, compiled out by default** |
| C: streaming hook | 1 per result site | 1 switch per result | results | algorithm contract changes |

B is adopted for scheduling. C is deferred to a later milestone as an *addition* to B, never a replacement.

---

## 8. Memory: arenas, stacks, cancellation

### It is the arrays, not the stacks

For a graph of `n` vertices, one BFS query owns `order`, `parent`, `dist` (3 x 4 bytes x n) plus the queue (4 bytes x n): 16 bytes per vertex, 16 MB per query on a one-million-vertex graph, regardless of how many vertices the query actually reaches. A DFS frame is 40 bytes, so DFS owns 52 bytes per vertex. A thousand concurrent full traversals of that graph would therefore need 16-52 GB, while their coroutine stacks would need 64 MB. The stack is not the memory problem; the O(n) per-query state is.

Two mitigations, both outside the algorithm bodies:
* **Bounded queries do not need O(n) state.** A k-hop neighborhood, a path-to-target search or a top-K expansion touches a small fraction of `n`; those variants should mark visited vertices in a hash set (`include/Hashtable.h` already exists) sized to the expected frontier, not in an `n`-sized array. This is a family of *new* algorithm entry points, not a change to the full traversals.
* **Admission control.** The server bounds the number of concurrent full traversals by the memory they would pin, not by the number of connections.

### One arena per coroutine

The arena is the part of this design that is most often misread, so its purpose is stated bluntly: **the arena is a lifetime tool, not a speed tool.** A BFS performs four `malloc` calls and a DFS four; each costs a few microseconds against tens of milliseconds of traversal. Replacing them buys nothing measurable in throughput. The arena exists to solve exactly one problem, cancellation, and gives admission control as a side effect.

**The problem it solves.** A client starts a BFS on a million-vertex graph. The coroutine runs, reaches a checkpoint, yields. Meanwhile the client closes the connection. The server now holds a coroutine suspended in the middle of `graph_bfs()`, and inside it four heap blocks (`order`, `parent`, `dist`, the queue) whose only references are local variables on the coroutine's private stack. Nobody outside the coroutine knows those pointers exist. There are three ways out:

| Option | What it costs | Verdict |
|---|---|---|
| Resume the coroutine and let it finish | tens of ms of CPU per abandoned query, for a result nobody reads | acceptable for a single agent; wasteful under many clients |
| A `cancelled` flag polled by the checkpoint, so the algorithm bails out | a cleanup-and-return path inside every loop of every algorithm | violates the purity constraint: concurrency handling inside pure code |
| Never resume it and discard everything it owned, wholesale | only possible if "everything it owned" is a single block the scheduler knows | **adopted**; the arena is what makes it possible |

**What the arena is.** A bump allocator over one block taken from a pool when the query is admitted: a base pointer, a capacity and an offset. Allocation aligns the offset, advances it, returns the old value. Freeing an individual object does nothing. Freeing the arena resets the offset and returns the block to the pool. No per-object header, no free list, no search.

```c
typedef struct
{
    char*  base; /* one block from the pool */
    size_t cap;
    size_t used;
} Arena;

static void* arena_alloc(Arena* a, size_t size)
{
    size_t off = (a->used + 15) & ~(size_t)15; /* 16-byte alignment */
    if (off + size > a->cap)
        return NULL; /* the query was mis-sized at admission */
    a->used = off + size;
    return a->base + off;
}

static void arena_reset(Arena* a)
{
    a->used = 0; /* everything freed at once; the block goes back to the pool */
}
```

**Why it fits these algorithms.** A query performs a fixed, known number of allocations, all at the start, all of a size computable from `n`:

| Query | Allocations | Bytes per vertex |
|---|---|---|
| BFS | 3 `Traversal` arrays + queue | 16 |
| DFS | 3 `Traversal` arrays + frame stack | 52 |

So at admission the server knows exactly how large the arena must be, takes one block of that size, and the query never touches `malloc`. No block chaining is needed for the traversals as they exist today.

**Lifecycle of a query under the arena:**

1. *Admission.* The server computes the size from the query type and `n`, takes a block from the pool (or refuses/queues the query if the total pinned memory would exceed the limit: this is the admission control mentioned above, and with bare `malloc` the same condition is only discovered when an allocation fails mid-traversal).
2. *Execution.* `traversal_new()`, `queue__create()` and `stack__create()` allocate through `tg_alloc()`. The hook calls `turbo_co_current()` (the `noinline` function of section 10): if a coroutine is running on this thread, it carves from that coroutine's arena; if not, the library is being used standalone and `tg_alloc()` is `malloc()`. The corresponding `tg_free()` is a no-op inside a coroutine. Because the arena belongs to the coroutine and not to the thread, a worker that resumes a migrated query finds the arena in the `TurboCo` and continues; nothing thread-specific is involved.
3. *Completion or cancellation.* `arena_reset()`, block back to the pool. On cancellation the coroutine is simply never resumed; the `AUTO_FREE_TRAVERSAL` cleanup never runs and that is fine, there is nothing left to free individually.

**What it does not touch.** The graph. The `Star` block is allocated once by `star_init_from_edges()`, lives for the lifetime of the server and is shared read-only by every coroutine. It has a different lifetime from any query, which is precisely why it lives in a different allocator. The arena manages per-query state only. The algorithm bodies are not touched either: the change is five `malloc`/`free` sites in `traversal.c`, `queue.c` and `stack.c`, rerouted to the hook.

**The one measurable side effect.** On a large graph the per-query arrays exceed glibc's `mmap` threshold, so each `malloc` is an `mmap` and each `free` an `munmap`; the next query re-faults every page (about 4000 faults, roughly a millisecond, for 16 MB). Pooled arena blocks keep their pages resident. On a 30 ms whole-graph traversal that is a few percent; on a 100 µs bounded-frontier query it can dominate. It is to be measured, not assumed, and it is not the reason the arena exists.

**Limit.** The arena works because every object's lifetime coincides with the query's. An algorithm that allocated incrementally and without bound inside its loop (a growing visited hash set, an unbounded result list) would need either chained blocks or an upper bound declared at admission. The bounded-frontier queries of section 13 are the relevant case: their bound is the frontier budget, which is already a parameter of the query.

**Deferrable.** Everything else in this document works with plain `malloc`. Without the arena, an abandoned query either runs to completion (option one above) or needs the cancelled flag. For a first prototype driven by a single agent over MCP, which issues few queries and rarely abandons one, option one is acceptable; the arena is milestone M3 and is introduced when abandoned queries are measured to cost something.

### Stacks

Each coroutine stack is `mmap`'d (so physical pages are only committed as they are touched) with a `PROT_NONE` **guard page** below it, so an overflow faults instead of corrupting the neighbor's stack. Sizes: the traversals need under 1 KB, but `LOG_DEBUG` goes through `vfprintf`, which uses several KB, and any future algorithm that calls into libc or recurses needs headroom. **64 KB** virtual per stack is the default (only the touched pages cost anything); **16 KB** is a reasonable floor for release builds. Stacks are pooled and reused, never freed per query.

---

## 9. The context switch: exact contract

The switch routine is small, but every one of its lines encodes an ABI rule; this section is the checklist for writing and reviewing it.

**Saved and restored** (System V x86-64 callee-saved set):
* `RBX`, `RBP`, `R12`, `R13`, `R14`, `R15`;
* `RSP`;
* the return address (implicitly: `turbo_co_switch(from, to)` is a normal `call`, so it is the `ret` at the end that lands on the other stack);
* the **x87 control word** and **MXCSR** control bits. They are callee-saved by the ABI and are the two things "just swap the GPRs" tutorials forget. Since no TurboGraph code changes rounding modes, they can be *documented as invariant* instead of saved, but the decision has to be explicit.

**Not saved**: caller-saved registers (`RAX`, `RCX`, `RDX`, `RSI`, `RDI`, `R8`-`R11`, all XMM/YMM registers) because the compiler already assumes they are clobbered by any call, and the signal mask, because it is process-wide state that a query has no business touching.

**Invariants**:
* `RSP` is 16-byte aligned *before* the `call` to the entry function, i.e. `RSP % 16 == 8` at the entry function's first instruction. Getting this wrong crashes on the first `movaps` in libc.
* The entry trampoline never returns into nothing: the bottom of a fresh stack holds the address of a `turbo_co_exit()` routine that marks the coroutine `DONE` and switches back to the worker.
* The red zone (128 bytes below `RSP`) needs no special handling because a switch is always a function call, and the caller already knows the red zone is dead across calls.

**Tooling** that must be told about the private stacks, or it lies:
* `valgrind` (`make memcheck` is part of the workflow): `VALGRIND_STACK_REGISTER()` per stack, or every memcheck run reports the stack as invalid memory;
* AddressSanitizer: `__sanitizer_start_switch_fiber()` / `__sanitizer_finish_switch_fiber()` around each switch;
* `gdb`: backtraces through a switch are unreadable unless the routine carries CFI directives (`.cfi_*`) that describe where it stashed the caller's registers. Worth the ten extra lines.

**Portability**: the routine is x86-64 SysV only. AArch64 (`x19`-`x29`, `sp`, `lr`, `d8`-`d15`) is a second ~20-line file behind the same three-function interface (`turbo_co_switch`, `turbo_co_init_stack`, `turbo_co_exit`); nothing above that interface is architecture-specific.

---

## 10. Thread migration: the rules that make M:N safe

A coroutine suspended on worker A and resumed on worker B is the whole point of the pool and the origin of every non-obvious bug. Four rules:

1. **Re-enqueue from the worker's stack, never from the coroutine's.** The worker calls `turbo_co_resume(co)`; when it returns (because `co` hit a checkpoint), *then* the worker pushes `co` onto the task queue. If the coroutine pushed itself before switching out, worker B could dequeue it and start running on a stack that worker A is still executing on.
2. **No TLS address may survive a checkpoint.** The compiler is allowed to compute the address of a `__thread` variable once and reuse it across calls in the same function, because it assumes the thread does not change underneath it. Inside a coroutine that assumption is false. The algorithms use no TLS (P2). The one piece of infrastructure that does, `turbo_co_current()` (which the checkpoint uses to find the running coroutine and its budget), is a `noinline` function in its own translation unit, so the address is recomputed on every call. This must be re-checked if LTO is ever enabled.
3. **Memory ordering is inherited from the queue.** Worker A's writes to the coroutine's arena happen-before worker B's reads because the push/pop on the task queue is a release/acquire pair (a mutex or an atomic CAS). No additional fences are needed in the algorithm.
4. **No lock across a checkpoint** (P3). A lock taken on worker A and released on worker B is undefined behavior for a `pthread_mutex`. Pure code holds none; the infrastructure must not either.

`errno` and the stack-protector canary are the two things people ask about: `errno` is only meaningful immediately after the call that set it, so migration cannot break correct code, and the canary is a per-process value copied into every thread's control block, so it reads the same on every worker.

---

## 11. Sharing the graph across workers

Workers never lock the graph, and the reason is a property the CSR already has: **`GRAPH_STAR` is immutable**. A block that is never written can be read by any number of threads with no synchronization at all, and its position-independent layout (offsets, not pointers) means the same block is also what gets `memcpy`'d to a GPU or a mapped file.

The server therefore serves **only star snapshots**. A mutation (edge insert, bulk load) is not applied in place: it produces a *new* star block, and the pointer that new queries pick up is swapped atomically. Queries already running keep reading the old block, which is retired when the last of them completes: a reference count per snapshot, incremented at query admission and decremented at completion or cancellation, is sufficient and cheap because it is touched once per query, not once per edge. Epoch-based reclamation is the upgrade path if the refcount ever shows up in a profile.

`GRAPH_LIST` and `GRAPH_MATRIX` are mutable in place and are not served through the coroutine layer at all. They remain the *build* representations: a graph is assembled in a list, converted to a star, and only then published.

This is purity rule P7: the algorithm assumes the `Graph*` it was given is valid for its whole lifetime, and the snapshot refcount is what makes that true.

---

## 12. Implementation milestones

Each milestone is independently testable and leaves the library build untouched.

| Milestone | Deliverable | Touches `src/traversal.c`? |
|---|---|---|
| **M0: switch primitive** | `src/coro_x86_64.S` + `include/Coro.h`: `turbo_co_switch`, stack init, exit trampoline, CFI, valgrind/ASan hooks. A test that ping-pongs two coroutines a few million times and checks the register set is intact. | no |
| **M1: run-to-completion pool** | Worker threads + task queue; each query calls `graph_bfs()`/`graph_dfs()` unchanged on a coroutine stack and runs to completion. Already gives N-way parallelism. | no |
| **M2: budgeted slicing** | `turbo_co_checkpoint()` macro, `make CO=1`, one line in each traversal loop. Fairness test: one whole-graph BFS and a thousand 2-hop queries share the pool; measure the 2-hop latency distribution. | **yes: 2 lines** |
| **M3: arena + cancellation** | `tg_alloc()`/`tg_free()` hook, 5 call sites rerouted, arena per coroutine, abandon-on-disconnect. | no (touches `queue.c`, `stack.c`, `traversal_new()`) |
| **M4: network + snapshots** | `epoll` front end, per-connection output buffer with `WAITING` state, refcounted star snapshots. | no |
| **M5: streaming (optional)** | `on_discover` visitor hook bound to a generator yield in the entry function; bounded-frontier variants (k-hop, path-to-target) with hash-set visited marks. | yes: 1 line for the hook; new entry points for the bounded variants |
| **M6: updates** | Stage 1 of section 13: linear merge of the current CSR with a sorted batch into a new snapshot, published through the section 11 swap; edge labels segment; string-to-id vertex map. | no (touches `star.c` and adds a write path) |

The algorithm code is touched in M2 and, optionally, M5, and in both cases the change is a single hook line whose default expansion is nothing.

---

## 13. Mutating the graph: updates from an agent over MCP

The README's roadmap has the engine weaving codebases, which means the graph is no longer built once and queried forever: an agent, talking to the server over MCP, edits a file and expects the graph to reflect the new edges on its very next query. This section settles how a CSR that is immutable by design absorbs writes.

### The workload is not a database workload

A tool call over MCP costs hundreds of milliseconds of round trip plus seconds of model reasoning between calls. Writes arrive in natural bursts ("file X changed: here are its new edges and the ones to drop"), so **the MCP call is already the batch**; no batching layer needs inventing. What the agent does require is *read-your-writes*: a query issued after an update must see the update, otherwise the agent reasons over stale data. Any scheme that defers visibility is therefore ruled out, whatever it saves.

Order of magnitude of a rebuild, to be confirmed with the callgrind workflow on a real graph:

| Edges | Rebuild from previous CSR + sorted batch |
|---|---|
| 1M | a few ms |
| 10M | tens of ms |
| 100M | about a second |

A large codebase sits in the 1-10M range. A synchronous rebuild is cheaper than one MCP round trip.

### Stage 1 (adopted first): synchronous rebuild on snapshot

The write call receives the batch, builds a **new** `Star` block by a linear merge of the current CSR with the batch sorted by source vertex (not from scratch through a quicksort), publishes it through the atomic swap and refcount of section 11, and only then returns. Queries already running finish on the old block; queries admitted afterwards read the new one. There is no window in which a reader sees a half-applied update, and the algorithms are untouched.

Deletions, the weak point of every other CSR update scheme, are free here: an edge to drop is simply filtered out during the merge.

### Stage 2 (when measured necessary): delta overlay behind the vtable

If writes become frequent (several agents, or a graph in the 100M range), add a fourth representation, `GRAPH_OVERLAY`: an immutable CSR base plus a mutable delta indexed by source vertex (and by target, for the in-star), plus tombstones for removed edges. Its `iter_next()` walks the CSR range of `u`, then the delta list of `u`, skipping tombstoned arcs. Compaction folds the delta into a new CSR in the background when it exceeds a threshold, typically while the agent is thinking. This is the log-structured (LSM) pattern applied to adjacency, and `GraphOps` is exactly the seam it needs: the algorithms see one `Graph*` and never learn that it is two structures.

The per-edge tombstone check is the cost to watch. A per-vertex "has tombstones" bit skips it for the vast majority of vertices that no delete ever touched.

### Ruled out

* **A bare edge list as the live query structure.** Every neighbor lookup becomes a linear scan of the delta; a few thousand pending edges and the BFS collapses.
* **A CSR mutated in place** (packed memory arrays, PCSR). Considerable complexity, and it breaks position independence, snapshots and GPU transferability, the three reasons the CSR exists.
* **Deferred compaction without an overlay.** It is the scheme that creates the very write-to-read latency gap this section is meant to avoid.

### The GPU constraint

A CUDA kernel wants a pure CSR. A GPU query therefore forces compaction before launch. That is acceptable: GPU jobs are the heavy ones, and a compaction is cheap next to them.

### What weaving needs that the core does not have yet

Independent of the update scheme, three additions are required and none of them touches the traversal code:

* **Stable vertex identity.** A string-to-id map outside the graph (the existing `Hashtable`), ids never reused. Between rebuilds new vertices take ids above `n`; the rebuild grows `n`.
* **Edge types.** Today an edge carries only a `double` weight. Weaving needs at least a label per arc: one more segment in the `Star` block, stored as an offset like the others so the block stays position independent.
* **Bounded-frontier query variants.** k-hop neighborhoods, path-to-target and top-K expansions are what an agent actually asks for, and they touch a small fraction of `n`. They mark visited vertices in a hash set sized to the frontier budget rather than in `n`-sized arrays (see section 8). New entry points, not changes to the full traversals.

---

## 14. Open questions

* **Budget size.** Somewhere between 2^14 and 2^18 iterations per slice; to be set by the M2 fairness test, not by intuition.
* **Work stealing vs. a single queue.** One shared queue is simplest and, with slices of tens of microseconds, contention on it is negligible. Per-worker queues with stealing are the upgrade if a profile ever says otherwise.
* **Priority.** Whether a query that has already consumed many slices should be deprioritized (multilevel feedback) so that a burst of small queries always wins. The checkpoint already knows how many slices a query has used; the policy is a queue concern.
* **Interaction with the CUDA roadmap.** A GPU kernel is the extreme case of "runs to completion outside the worker": the coroutine that launched it goes `WAITING` on a completion event rather than spinning on a worker. The state model in section 3 already has the slot for it.
