#ifndef GRAPH_STAR_H
#define GRAPH_STAR_H

#include "../include/Graph.h"

/*
 * Private to the library: internals of the star (CSR) representation,
 * implemented in star_impl.c. Header-only so that the algorithms can
 * inline the iterators (see internal.h).
 */

/* ---- block accessors ------------------------------------------------------- */

/* Offset to pointer. One addition, which next to the memory access that
 * follows it is not measurable. */
static inline int* st_heads(const Star* s)
{
    return (int*)((char*)s + s->o_heads);
}
static inline int* st_to(const Star* s)
{
    return (int*)((char*)s + s->o_to);
}
static inline double* st_w(const Star* s)
{
    return (double*)((char*)s + s->o_w);
}
static inline int* st_rheads(const Star* s)
{
    return (int*)((char*)s + s->o_rheads);
}
static inline int* st_from(const Star* s)
{
    return (int*)((char*)s + s->o_from);
}
static inline double* st_rw(const Star* s)
{
    return (double*)((char*)s + s->o_rw);
}

/*
 * True when the in star exists and is the one to read. An undirected graph
 * has no in star at all: its offsets are 0, so st_rheads() & co. would
 * resolve to the header itself. Every backward operation must therefore
 * fall back to the out star, which is symmetric anyway.
 */
static inline bool use_in_star(const Graph* g, bool in)
{
    return in && g->directed;
}

/* ---- star iterators ------------------------------------------------------- */

/*
 * Inline on purpose: star_ops points to them for the generic API, and the
 * algorithms' star instances call them directly (see internal.h).
 */
static inline void star_iter_out(const Graph* g, int u, GraphIter* it)
{
    const Star* star = (const Star*)g->data;

    it->g    = g;
    it->in   = false;
    it->u    = u;
    it->i    = st_heads(star)[u];
    it->end  = st_heads(star)[u + 1];
    it->nbr  = st_to(star);
    it->w    = st_w(star);
    it->node = NULL; // unused by this representation
}

static inline void star_iter_in(const Graph* g, int u, GraphIter* it)
{
    const Star* star = (const Star*)g->data;

    if (!g->directed)
    { /* out star is symmetric: delegate */
        star_iter_out(g, u, it);
        it->in = true; /* informational: use_in_star stays false */
        return;
    }
    it->g    = g;
    it->in   = true;
    it->u    = u;
    it->i    = st_rheads(star)[u];
    it->end  = st_rheads(star)[u + 1];
    it->nbr  = st_from(star);
    it->w    = st_rw(star);
    it->node = NULL;
}

/*
 * Arrays and bounds were resolved once by iter_out/iter_in, so the per-arc
 * cost is a compare and a load: no direction test, no offset arithmetic.
 */
static inline bool star_iter_next(GraphIter* it, int* v, double* w)
{
    /*
     * Boundary check. Only the upper bound is tested, and it is exclusive:
     * end = head[u+1] is the first index belonging to the next vertex. The
     * lower bound needs no test -- iter_out/iter_in set i = head[u] and
     * nothing but the increment below ever moves it. A zero-degree vertex
     * has i == end and stops here on the very first call.
     */
    if (it->i >= it->end)
        return false;

    /* Both outputs are optional: graph_bfs() passes NULL for the weight. */
    if (v)
        *v = it->nbr[it->i];
    if (w)
        *w = it->w[it->i];

    it->i++;
    return true;
}

#endif /* GRAPH_STAR_H */
