#include <cuda_runtime.h>
#include <stdint.h>
#include <stdio.h>
#include "../include/Graph.h"

extern "C" {

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
BORUVKA_API GraphEdge* boruvka_gpu(const Graph* g, int* mst_size);
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



extern "C" GraphEdge* boruvka_gpu(const Graph* g, int* mst_size)
{
    return NULL;
}