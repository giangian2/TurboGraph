/*
 * Boruvka minimum spanning forest on the GPU.
 *
 * Built as a shared library with a plain C ABI (see cuda/README.md), so the
 * C11 engine can load it at run time without sharing a toolchain with nvcc.
 *
 * Each round, on the edges that still join two different components:
 *   1. every component finds its lightest incident edge (two atomicMin
 *      passes: first the weight key, then the lowest position among the ties,
 *      so the choice is a strict total order and the picks form no cycle
 *      longer than a mutual pair);
 *   2. each component hooks onto the other end of its edge; a mutual pair is
 *      broken by the smaller id staying a root;
 *   3. pointer jumping flattens the hook forest;
 *   4. edges are relabeled to component ids and the internal ones are
 *      compacted away (scan + scatter).
 * The number of components at least halves per round: O(log n) rounds.
 */
#include <cuda_runtime.h>
#include <thrust/copy.h>
#include <thrust/device_ptr.h>
#include <thrust/execution_policy.h>
#include <thrust/iterator/counting_iterator.h>
#include <thrust/scan.h>
#include <stdint.h>
#include <stdio.h>

extern "C" {
/* same layout as GraphEdge in include/Graph.h */
typedef struct
{
    int    u, v;
    double w;
} BoruvkaEdge;

typedef struct
{
    float h2d_ms, compute_ms, d2h_ms;
    int   rounds;
} BoruvkaTimes;

#ifdef _WIN32
#define BORUVKA_API __declspec(dllexport)
#else
#define BORUVKA_API __attribute__((visibility("default")))
#endif

/*
 * Minimum spanning forest of the undirected graph with n vertices and the m
 * edges `edges`. out_idx (room for n ints) receives the indices of the edges
 * in the forest. Returns their number, or -1 on error (bad arguments, CUDA
 * failure). times is optional. Device memory is allocated and freed per call.
 */
BORUVKA_API int boruvka_gpu(int n, size_t m, const BoruvkaEdge* edges, int* out_idx,
                            BoruvkaTimes* times);
}

#define CK(call)                                                                               \
    do                                                                                         \
    {                                                                                          \
        cudaError_t err_ = (call);                                                             \
        if (err_ != cudaSuccess)                                                               \
        {                                                                                      \
            fprintf(stderr, "CUDA %s:%d: %s\n", __FILE__, __LINE__, cudaGetErrorString(err_)); \
            goto fail;                                                                         \
        }                                                                                      \
    } while (0)

#define BLOCK 256
#define GRID(x) ((int)(((x) + BLOCK - 1) / BLOCK))
#define NO_EDGE 0x7fffffff

typedef unsigned long long u64;

/* monotone map double -> u64, so that integer min is numeric min */
__device__ static inline u64 weight_key(double w)
{
    u64 b = (u64)__double_as_longlong(w);
    return (b >> 63) ? ~b : (b | 0x8000000000000000ULL);
}

__global__ static void split_k(const BoruvkaEdge* e, int m, int* cu, int* cv, u64* key, int* id)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= m)
        return;
    BoruvkaEdge x = e[i];
    cu[i]         = x.u;
    cv[i]         = x.v;
    key[i]        = weight_key(x.w);
    id[i]         = i;
}

__global__ static void reset_k(int n, u64* min_key, int* min_pos, int* parent, int init_parent)
{
    int c = blockIdx.x * blockDim.x + threadIdx.x;
    if (c >= n)
        return;
    min_key[c] = ~0ULL;
    min_pos[c] = NO_EDGE;
    if (init_parent)
        parent[c] = c;
}

__global__ static void min_key_k(const int* cu, const int* cv, const u64* key, int m, u64* min_key)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= m)
        return;
    u64 k = key[i];
    atomicMin(&min_key[cu[i]], k);
    atomicMin(&min_key[cv[i]], k);
}

__global__ static void min_pos_k(const int* cu, const int* cv, const u64* key, int m,
                                 const u64* min_key, int* min_pos)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= m)
        return;
    u64 k = key[i];
    int a = cu[i], b = cv[i];
    if (min_key[a] == k)
        atomicMin(&min_pos[a], i);
    if (min_key[b] == k)
        atomicMin(&min_pos[b], i);
}

__global__ static void hook_k(int n, const int* min_pos, const int* cu, const int* cv, int* parent)
{
    int c = blockIdx.x * blockDim.x + threadIdx.x;
    if (c >= n)
        return;
    int e = min_pos[c];
    if (e == NO_EDGE)
        return;
    parent[c] = cu[e] == c ? cv[e] : cu[e];
}

__global__ static void break_mark_k(int n, const int* min_pos, const int* id, int* parent,
                                    unsigned char* selected)
{
    int c = blockIdx.x * blockDim.x + threadIdx.x;
    if (c >= n)
        return;
    int e = min_pos[c];
    if (e == NO_EDGE)
        return;
    int p = parent[c];
    if (parent[p] == c && c < p)
        parent[c] = c; /* mutual pair: the smaller id is the root, the other marks the edge */
    else
        selected[id[e]] = 1;
}

__global__ static void jump_k(int n, int* parent, int* changed)
{
    int c = blockIdx.x * blockDim.x + threadIdx.x;
    if (c >= n)
        return;
    int p  = parent[c];
    int pp = parent[p];
    if (pp != p)
    {
        parent[c] = pp;
        *changed  = 1;
    }
}

__global__ static void relabel_k(int m, int* cu, int* cv, const int* parent, int* flag)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= m)
        return;
    int a   = parent[cu[i]];
    int b   = parent[cv[i]];
    cu[i]   = a;
    cv[i]   = b;
    flag[i] = a != b;
}

__global__ static void scatter_k(int m, const int* flag, const int* pos, const int* cu, const int* cv,
                                 const u64* key, const int* id, int* ocu, int* ocv, u64* okey,
                                 int* oid)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= m || !flag[i])
        return;
    int o   = pos[i];
    ocu[o]  = cu[i];
    ocv[o]  = cv[i];
    okey[o] = key[i];
    oid[o]  = id[i];
}

struct IsSet
{
    __host__ __device__ bool operator()(unsigned char x) const { return x != 0; }
};

extern "C" int boruvka_gpu(int n, size_t m, const BoruvkaEdge* edges, int* out_idx, BoruvkaTimes* times)
{
    if (n <= 0 || m > 0x7fffffffu)
        return -1;
    if (m == 0)
        return 0;

    int            count  = -1;
    int            mi     = (int)m;
    BoruvkaEdge*   d_edges = NULL;
    int*           cu[2]  = {NULL, NULL};
    int*           cv[2]  = {NULL, NULL};
    int*           id[2]  = {NULL, NULL};
    u64*           key[2] = {NULL, NULL};
    int*           flag = NULL;
    int*           pos = NULL;
    int*           parent = NULL;
    int*           min_pos = NULL;
    int*           changed = NULL;
    int*           d_out = NULL;
    u64*           min_key  = NULL;
    unsigned char* selected = NULL;
    cudaEvent_t    ev[4];
    int            ne = 0, rounds = 0, cur = 0;

    for (int i = 0; i < 4; i++)
        cudaEventCreate(&ev[i]);

    CK(cudaMalloc(&d_edges, m * sizeof(BoruvkaEdge)));
    for (int b = 0; b < 2; b++)
    {
        CK(cudaMalloc(&cu[b], m * sizeof(int)));
        CK(cudaMalloc(&cv[b], m * sizeof(int)));
        CK(cudaMalloc(&id[b], m * sizeof(int)));
        CK(cudaMalloc(&key[b], m * sizeof(u64)));
    }
    CK(cudaMalloc(&flag, m * sizeof(int)));
    CK(cudaMalloc(&pos, m * sizeof(int)));
    CK(cudaMalloc(&parent, (size_t)n * sizeof(int)));
    CK(cudaMalloc(&min_pos, (size_t)n * sizeof(int)));
    CK(cudaMalloc(&min_key, (size_t)n * sizeof(u64)));
    CK(cudaMalloc(&changed, sizeof(int)));
    CK(cudaMalloc(&selected, m));
    CK(cudaMalloc(&d_out, (size_t)n * sizeof(int)));
    CK(cudaMemset(selected, 0, m));

    CK(cudaEventRecord(ev[0]));
    CK(cudaMemcpy(d_edges, edges, m * sizeof(BoruvkaEdge), cudaMemcpyHostToDevice));
    CK(cudaEventRecord(ev[1]));

    split_k<<<GRID(mi), BLOCK>>>(d_edges, mi, cu[0], cv[0], key[0], id[0]);
    reset_k<<<GRID(n), BLOCK>>>(n, min_key, min_pos, parent, 1);
    ne = mi;

    for (;;)
    {
        /* relabel to roots, drop the internal edges */
        relabel_k<<<GRID(ne), BLOCK>>>(ne, cu[cur], cv[cur], parent, flag);
        thrust::device_ptr<int> f(flag), p(pos);
        thrust::exclusive_scan(thrust::device, f, f + ne, p);
        int last_pos = 0, last_flag = 0;
        CK(cudaMemcpy(&last_pos, pos + ne - 1, sizeof(int), cudaMemcpyDeviceToHost));
        CK(cudaMemcpy(&last_flag, flag + ne - 1, sizeof(int), cudaMemcpyDeviceToHost));
        int nn = last_pos + last_flag;
        if (nn == 0)
            break;
        scatter_k<<<GRID(ne), BLOCK>>>(ne, flag, pos, cu[cur], cv[cur], key[cur], id[cur], cu[cur ^ 1],
                                       cv[cur ^ 1], key[cur ^ 1], id[cur ^ 1]);
        cur ^= 1;
        ne = nn;
        rounds++;

        reset_k<<<GRID(n), BLOCK>>>(n, min_key, min_pos, parent, 0);
        min_key_k<<<GRID(ne), BLOCK>>>(cu[cur], cv[cur], key[cur], ne, min_key);
        min_pos_k<<<GRID(ne), BLOCK>>>(cu[cur], cv[cur], key[cur], ne, min_key, min_pos);
        hook_k<<<GRID(n), BLOCK>>>(n, min_pos, cu[cur], cv[cur], parent);
        break_mark_k<<<GRID(n), BLOCK>>>(n, min_pos, id[cur], parent, selected);
        for (;;)
        {
            int zero = 0, host_changed = 0;
            CK(cudaMemcpy(changed, &zero, sizeof(int), cudaMemcpyHostToDevice));
            jump_k<<<GRID(n), BLOCK>>>(n, parent, changed);
            CK(cudaMemcpy(&host_changed, changed, sizeof(int), cudaMemcpyDeviceToHost));
            if (!host_changed)
                break;
        }
    }
    CK(cudaGetLastError());

    /* selected flags -> compact list of original edge indices */
    {
        thrust::device_ptr<unsigned char> s(selected);
        thrust::device_ptr<int>           o(d_out);
        thrust::device_ptr<int>           end = thrust::copy_if(
            thrust::device, thrust::counting_iterator<int>(0), thrust::counting_iterator<int>(mi), s,
            o, IsSet());
        count = (int)(end - o);
    }
    CK(cudaEventRecord(ev[2]));
    CK(cudaMemcpy(out_idx, d_out, (size_t)count * sizeof(int), cudaMemcpyDeviceToHost));
    CK(cudaEventRecord(ev[3]));
    CK(cudaEventSynchronize(ev[3]));

    if (times)
    {
        cudaEventElapsedTime(&times->h2d_ms, ev[0], ev[1]);
        cudaEventElapsedTime(&times->compute_ms, ev[1], ev[2]);
        cudaEventElapsedTime(&times->d2h_ms, ev[2], ev[3]);
        times->rounds = rounds;
    }
    goto done;

fail:
    count = -1;
done:
    for (int i = 0; i < 4; i++)
        cudaEventDestroy(ev[i]);
    cudaFree(d_edges);
    for (int b = 0; b < 2; b++)
    {
        cudaFree(cu[b]);
        cudaFree(cv[b]);
        cudaFree(id[b]);
        cudaFree(key[b]);
    }
    cudaFree(flag);
    cudaFree(pos);
    cudaFree(parent);
    cudaFree(min_pos);
    cudaFree(min_key);
    cudaFree(changed);
    cudaFree(selected);
    cudaFree(d_out);
    return count;
}
