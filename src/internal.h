#ifndef GRAPH_INTERNAL_H
#define GRAPH_INTERNAL_H

#include "../include/Graph.h"

/*
 * Private to the library, shared by every representation.
 *
 * The algorithms are written once, generic over the neighbor iterator,
 * and instantiated per representation (see traversal.c). An instance that
 * receives a representation's inline iterators (star.h, ...) as
 * compile-time constants gets them inlined: no call per arc, the GraphIter
 * cursor lives in registers, and a NULL weight pointer folds away. Through
 * g->ops the same code makes an indirect call per arc, which is the
 * generic fallback for representations without inline iterators.
 */

/* Signatures of GraphOps.iter_out/iter_in and GraphOps.iter_next */
typedef void (*IterOutFn)(const Graph* g, int u, GraphIter* it);
typedef bool (*IterNextFn)(GraphIter* it, int* v, double* w);

#endif /* GRAPH_INTERNAL_H */
