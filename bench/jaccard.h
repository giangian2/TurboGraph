#ifndef BENCH_JACCARD_H
#define BENCH_JACCARD_H

/*
 * Shared by tools/snap_to_bin.c and bench/mst_suite.c.
 *
 * jaccard_weights() turns an undirected edge list with arbitrary weights
 * (duplicates and self loops allowed) into a simple graph weighted by the
 * Jaccard distance of the two neighborhoods:
 *
 *     w(u, v) = 1 - |N(u) & N(v)| / |N(u) | N(v)|,  at least 1e-6
 *
 * An edge inside a community, with many common neighbors, is light; a bridge
 * between communities is heavy (w = 1 when nothing is shared). This is the
 * correlation between weights and structure that real weighted social graphs
 * have, and it puts the last edge of the MST at the top of the weight range.
 *
 * The binary format (.bin) is "MSTB", int32 n, uint64 m, then m GraphEdge.
 */
#include "../include/Graph.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int jac__cmp_u64(const void* a, const void* b)
{
    uint64_t x = *(const uint64_t*)a, y = *(const uint64_t*)b;
    return (x > y) - (x < y);
}

/* |a & b| of two sorted lists: merge when the sizes are close, otherwise a
 * binary search of each element of the shorter one in the longer one */
static size_t jac__common(const int* a, size_t na, const int* b, size_t nb)
{
    if (na > nb)
    {
        const int* t = a;
        a            = b;
        b            = t;
        size_t s     = na;
        na           = nb;
        nb           = s;
    }
    size_t c = 0;
    if (na * 16 < nb)
    {
        for (size_t i = 0; i < na; i++)
        {
            size_t lo = 0, hi = nb;
            while (lo < hi)
            {
                size_t mid = (lo + hi) / 2;
                if (b[mid] < a[i])
                    lo = mid + 1;
                else
                    hi = mid;
            }
            c += lo < nb && b[lo] == a[i];
        }
        return c;
    }
    for (size_t i = 0, j = 0; i < na && j < nb;)
    {
        if (a[i] < b[j])
            i++;
        else if (a[i] > b[j])
            j++;
        else
            c++, i++, j++;
    }
    return c;
}

/* Returns a new array of *m_out edges (u < v, no duplicates, sorted by u
 * then v) weighted by the Jaccard distance; NULL if out of memory. The input
 * is not modified. */
__attribute__((unused)) static GraphEdge* jaccard_weights(int n, const GraphEdge* in, size_t m, size_t* m_out)
{
    // 1. simple graph: each edge once as (min, max) << 32 | max, sorted, unique
    uint64_t* key = malloc(sizeof(uint64_t) * (m ? m : 1));
    if (!key)
        return NULL;
    size_t k = 0;
    for (size_t i = 0; i < m; i++)
    {
        uint32_t u = (uint32_t)in[i].u, v = (uint32_t)in[i].v;
        if (u == v)
            continue;
        key[k++] = u < v ? (uint64_t)u << 32 | v : (uint64_t)v << 32 | u;
    }
    qsort(key, k, sizeof(uint64_t), jac__cmp_u64);
    size_t e = 0;
    for (size_t i = 0; i < k; i++)
        if (e == 0 || key[i] != key[e - 1])
            key[e++] = key[i];

    // 2. CSR with sorted neighbor lists: the keys are sorted by (u, v), so a
    // counting pass and two fills in key order leave every list sorted
    size_t* head = calloc((size_t)n + 1, sizeof(size_t));
    int*    nbr  = malloc(sizeof(int) * (2 * e + 1));
    size_t* fill = malloc(sizeof(size_t) * ((size_t)n + 1));
    if (!head || !nbr || !fill)
    {
        free(key), free(head), free(nbr), free(fill);
        return NULL;
    }
    for (size_t i = 0; i < e; i++)
        head[(key[i] >> 32) + 1]++, head[(uint32_t)key[i] + 1]++;
    for (int u = 0; u < n; u++)
        head[u + 1] += head[u];
    memcpy(fill, head, sizeof(size_t) * ((size_t)n + 1));
    // smaller neighbors first: for v, every u < v arrives in increasing u
    for (size_t i = 0; i < e; i++)
        nbr[fill[(uint32_t)key[i]]++] = (int)(key[i] >> 32);
    for (size_t i = 0; i < e; i++)
        nbr[fill[key[i] >> 32]++] = (int)(uint32_t)key[i];
    free(fill);

    // 3. weights
    GraphEdge* out = malloc(sizeof(GraphEdge) * (e ? e : 1));
    if (!out)
    {
        free(key), free(head), free(nbr);
        return NULL;
    }
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 4096)
#endif
    for (size_t i = 0; i < e; i++)
    {
        int    u = (int)(key[i] >> 32), v = (int)(uint32_t)key[i];
        size_t du = head[u + 1] - head[u], dv = head[v + 1] - head[v];
        size_t c  = jac__common(nbr + head[u], du, nbr + head[v], dv);
        double w  = 1.0 - (double)c / (double)(du + dv - c);
        out[i]    = (GraphEdge){u, v, w > 1e-6 ? w : 1e-6};
    }

    free(key), free(head), free(nbr);
    *m_out = e;
    return out;
}

__attribute__((unused)) static int bin_write(const char* path, int n, const GraphEdge* e, size_t m)
{
    FILE* f = fopen(path, "wb");
    if (!f)
        return -1;
    uint64_t mm = m;
    int ok = fwrite("MSTB", 1, 4, f) == 4 && fwrite(&n, sizeof n, 1, f) == 1 &&
             fwrite(&mm, sizeof mm, 1, f) == 1 && fwrite(e, sizeof(GraphEdge), m, f) == m;
    return fclose(f) == 0 && ok ? 0 : -1;
}

/* NULL on error; *n and *m receive the sizes */
__attribute__((unused)) static GraphEdge* bin_read(const char* path, int* n, size_t* m)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return NULL;
    char     magic[4];
    uint64_t mm;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "MSTB", 4) != 0 || fread(n, sizeof *n, 1, f) != 1 ||
        fread(&mm, sizeof mm, 1, f) != 1)
    {
        fclose(f);
        return NULL;
    }
    GraphEdge* e = malloc(sizeof(GraphEdge) * (mm ? mm : 1));
    if (!e || fread(e, sizeof(GraphEdge), mm, f) != mm)
    {
        free(e);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *m = mm;
    return e;
}

#endif
