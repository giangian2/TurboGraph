/**
 * @author Gianluca Bianchi
 * 
 *      The aim of this API is to provide a set of utilities to generate random graphs with given properties.
 *      In order to define a graph we can pass to the input the space of all possible arcs (m) and a constant "r" that
 *      describes the densisty of the graph: so the number of random selected arcs will be r*m with r in ]0,1].
 */

#ifndef GENRATOR_H
#define GENRATOR_H

#include "Debug.h"
#include "hashmap.h"
#include "Graph.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

/* Positions and values are arc indices in [0, m), m = n(n-1) is past
 * INT_MAX already at n ~ 46k: 64 bits. No padding in the struct, the
 * hashmap compares the key with memcmp. */
typedef struct
{
    int64_t value;
    int64_t key; // position π in v, the only field the hashmap looks at
} random_entry_t;

/* splitmix64: rand() stops at RAND_MAX (2^31 - 1 on glibc), the high arc
 * indices of a big space would never come out. The state is the seed,
 * advanced by every call. */
static inline uint64_t random_next(uint64_t* state)
{
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z          = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z          = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/* Uniform in [min, max], min <= max. A plain % range favours the low
 * values when 2^64 is not a multiple of range: the draws below threshold
 * (2^64 mod range of them) are thrown away and redrawn. */
static inline int64_t random_int64(uint64_t* state, int64_t min, int64_t max)
{
    uint64_t range     = (uint64_t)(max - min) + 1;
    uint64_t threshold = (0 - range) % range;
    uint64_t r;

    do
    {
        r = random_next(state);
    } while (r < threshold);

    return min + (int64_t)(r % range);
}

/**
 *  Store only the elements of v for which v[i] != i.
    Assume we have a list of such elements; each record in the list has
    two fields, π and e: they indicate an element of v where v[π] = e and
    π != e. For all positions π not contained in the list, v[π] = π.
    Every time a position i is generated at random in iteration k, the list is
    scanned and the following two tests are done.
    • Test 1: if the list contains a record with π = i, let e be the corresponding element;
    • Test 2: if the list contains a record with π = k, let e'′ be the corresponding element.
    Four cases can occur:
    • Both tests fail: select i; append (i, k) to the list.
    • Test 1 succeeds, Test 2 fails: select e′; update (i, e′) to (i, k).
    • Test 1 fails, Test 2 succeeds: select i; update (k, e′′) to (i, e′′).
    • Both tests succeed: select e′; update (i, e′) to (i, e′′); delete (k, e′′).

    Returns p = density * m distinct arc indices in [0, m), NULL if the
    arguments are invalid or an allocation fails. Same seed, same arcs.
 */
static int64_t* generate_random_graph_edges(const float density, const int64_t m,
                                            const uint64_t seed)
{
    if (m <= 0 || density <= 0 || density > 1)
    {
        LOG_ERROR("invalid arguments for random generation, density=%f m=%" PRId64, density, m);
        return NULL;
    }

    // In double: a float has 24 bits of mantissa, m would be rounded
    int64_t p = (int64_t)((double)density * (double)m);
    if (p == 0)
    {
        LOG_ERROR("density=%f selects no arcs out of m=%" PRId64, density, m);
        return NULL;
    }

    int64_t* edges = (int64_t*)malloc(sizeof(int64_t) * (size_t)p);
    // At most p records, twice the slots keeps the load under 50%: there is no grow
    random_entry_t* map = hash_table_create(random_entry_t, 2 * (size_t)p, NULL);

    if (edges == NULL || map == NULL)
    {
        if (edges != NULL)
        {
            free(edges);
        }

        if (map != NULL)
        {
            hash_table_free(map);
        }

        LOG_ERROR("allocation failed for graph edges in random generation, with m=%" PRId64, m);
        return NULL;
    }

    uint64_t state = seed;

    for (int64_t k = 0; k < p; k++)
    {
        // Any drawer not sealed yet: 0..k-1 are taken
        int64_t i = random_int64(&state, k, m - 1);

        random_entry_t* rec_i = hash_table_get(map, i); // Test 1
        random_entry_t* rec_k = hash_table_get(map, k); // Test 2

        /* Contents of the drawers: e', e'' or the identity. Both read
         * before touching the table, a remove shifts the entries back and
         * the two pointers could end up on another record. */
        int64_t v_i = rec_i != NULL ? rec_i->value : i;
        int64_t v_k = rec_k != NULL ? rec_k->value : k;

        /* The four cases collapse in select v[i], v[i] = v[k], seal k:
         * the put appends or updates the record of i, the remove drops the
         * dead record of k (when i == k it is the one just put). */
        edges[k] = v_i;
        hash_table_put(map, ((random_entry_t){.key = i, .value = v_k}));
        hash_table_remove(map, k);
    }

    hash_table_free(map);
    return edges;
}

static void generate_random_weights(GraphEdge* edges)
{

}

Graph* generate_random_graph(const float density, const int64_t m, const bool weighted)
{
    if(m <= 0)
    {
        LOG_ERROR();
    }

    if(density<= 0)
    {
        LOG_ERROR();
    }


}

#endif