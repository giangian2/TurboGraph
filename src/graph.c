/*
 * Public API: argument validation + dispatch through g->ops.
 * Nothing here knows how any representation works.
 */
#include <stdio.h>
#include <stdlib.h>
#include "../include/Graph.h"
#include "../include/Queue.h"

/**
 * @param g Graph
 * @param v Vertex
 *
 * @return True if the vertex can exists into the graph g with n edges, False otherwise
 */
static bool vertex_ok(const Graph *g, int v)
{
    return v >= 0 && v < g->n;
}

/**
 * @param n
 * @param deirected
 * @param repr
 * 
 * @return Pointer to Graph structure allocated in Heap,
 * for the representation specified in input and dimension n
 */
static Graph *shell(int n, bool directed, GraphRepr repr)
{
    Graph *g = malloc(sizeof(Graph));
    if (!g)
        return NULL;
    g->n = n;
    g->m = 0;
    g->directed = directed;
    g->repr = repr;
    g->ops = NULL;
    g->data = NULL;
    return g;
}


Graph *graph_create(int n, bool directed, GraphRepr repr)
{
    if (n <= 0)
        return NULL;
    Graph *g = shell(n, directed, repr);
    if (!g)
        return NULL;
    int rc;
    switch (repr) {
    case GRAPH_MATRIX:
        rc = GRAPH_ERR_ALLOC;
        break;
    case GRAPH_LIST:
        rc = list_init(g);
        break;
    case GRAPH_STAR:   /* immutable: an empty star would be useless */
    default:
        rc = GRAPH_ERR_ARG;
        break;
    }
    if (rc != GRAPH_OK) {
        free(g);
        return NULL;
    }
    return g;
}

Graph *graph_from_edges(int n, bool directed,
                        const GraphEdge *edges, size_t m, GraphRepr repr)
{
    if (n <= 0 || (m > 0 && !edges))
        return NULL;
    for (size_t k = 0; k < m; k++) {
        const GraphEdge *e = &edges[k];
        if (e->u < 0 || e->u >= n || e->v < 0 || e->v >= n || e->w == 0.0)
            return NULL;
    }

    if (repr == GRAPH_STAR) {
        
        Graph *g = shell(n, directed, repr);
        if (!g)
            return NULL;
        if (star_init_from_edges(g, edges, m) != GRAPH_OK) {
            free(g);
            return NULL;
        }
        return g;
    }

    Graph *g = graph_create(n, directed, repr);
    if (!g)
        return NULL;
    for (size_t k = 0; k < m; k++) {
        if (g->ops->add_edge(g, edges[k].u, edges[k].v, edges[k].w) != GRAPH_OK) {
            graph_free(g);
            return NULL;
        }
    }
    return g;
}

void graph_free(Graph *g)
{
    if (!g)
        return;
    if (g->ops)
        g->ops->destroy(g);
    free(g);
}

int graph_add_edge(Graph *g, int u, int v, double w)
{
    if (!g || !vertex_ok(g, u) || !vertex_ok(g, v) || w == 0.0)
        return GRAPH_ERR_ARG;
    return g->ops->add_edge(g, u, v, w);
}

int graph_remove_edge(Graph *g, int u, int v)
{
    if (!g || !vertex_ok(g, u) || !vertex_ok(g, v))
        return GRAPH_ERR_ARG;
    return g->ops->remove_edge(g, u, v);
}

bool graph_has_edge(const Graph *g, int u, int v)
{
    return graph_get_weight(g, u, v) != 0.0;
}

double graph_get_weight(const Graph *g, int u, int v)
{
    if (!g || !vertex_ok(g, u) || !vertex_ok(g, v))
        return 0.0;
    return g->ops->get_weight(g, u, v);
}

int graph_out_degree(const Graph *g, int u)
{
    if (!g || !vertex_ok(g, u))
        return GRAPH_ERR_ARG;
    return g->ops->out_degree(g, u);
}

int graph_in_degree(const Graph *g, int u)
{
    if (!g || !vertex_ok(g, u))
        return GRAPH_ERR_ARG;
    return g->ops->in_degree(g, u);
}

int graph_iter_out(const Graph *g, int u, GraphIter *it)
{
    if (!g || !it || !vertex_ok(g, u))
        return GRAPH_ERR_ARG;
    g->ops->iter_out(g, u, it);
    return GRAPH_OK;
}

int graph_iter_in(const Graph *g, int u, GraphIter *it)
{
    if (!g || !it || !vertex_ok(g, u))
        return GRAPH_ERR_ARG;
    g->ops->iter_in(g, u, it);
    return GRAPH_OK;
}

bool graph_iter_next(GraphIter *it, int *v, double *w)
{
    if (!it || !it->g)
        return false;
    return it->g->ops->iter_next(it, v, w);
}


/*
 * Allocates a Traversal for a graph of n vertices, with all vertices
 * marked "unreached" (parent = -1, dist = -1), as required by the
 * contract in Graph.h. Returns NULL if an allocation fails.
 */
static Traversal *traversal_new(int n)
{
    Traversal *t = malloc(sizeof *t);
    if (!t)
        return NULL;
    t->order  = malloc((size_t)n * sizeof *t->order);
    t->parent = malloc((size_t)n * sizeof *t->parent);
    t->dist   = malloc((size_t)n * sizeof *t->dist);
    t->count  = 0;
    t->n      = n;
    if (!t->order || !t->parent || !t->dist) {
        traversal_free(t);   /* free(NULL) is legal: only frees what actually succeeded */
        return NULL;
    }
    for (int v = 0; v < n; v++) {
        t->parent[v] = -1;
        t->dist[v]   = -1;
    }
    return t;
}

Traversal *graph_bfs(const Graph *g, int source)
{
    if (!g || !vertex_ok(g, source))
        return NULL;

    Traversal *t = traversal_new(g->n);
    if (!t)
        return NULL;

    /* Capacity g->n: given the invariant above, the queue can never fill
     * up, so the enqueues below can never fail. */
    Queue *q = queue_create(g->n, sizeof(int));
    if (!q) {
        traversal_free(t);
        return NULL;
    }

    /* The source is the starting point: distance 0 from itself,
     * no parent (stays -1), first vertex in the visit order. */
    t->dist[source] = 0;
    t->order[t->count++] = source;
    queue_enqueue(q, &source);

    /* While there is a discovered but not-yet-processed vertex... */
    int u;
    while (queue_dequeue(q, &u)) {
        /* Iterate the outgoing neighbors of u (the vertex just dequeued,
         * NOT the source). The iterator lives on the stack: no malloc. */
        GraphIter it;
        g->ops->iter_out(g, u, &it);

        int v;
        while (g->ops->iter_next(&it, &v, NULL)) {   /* weight ignored */
            if (t->dist[v] != -1)
                continue;   /* already discovered via an equal-or-shorter path */

            /* First discovery of v: u is its parent in the BFS tree and
             * its distance is one edge more than u's. */
            t->dist[v]   = t->dist[u] + 1;
            t->parent[v] = u;
            t->order[t->count++] = v;
            queue_enqueue(q, &v);
        }
    }

    /* Empty queue: no reachable vertex is left to explore.
     * Vertices that were never discovered stay at parent == -1, dist == -1. */
    queue_free(q);
    return t;
}


/* Hoare-scheme partition (NOT Lomuto): the pivot value is edges[p].w and is
 * never swapped out before scanning starts. e_minus/e_plus close in from
 * both ends until they cross; the returned index is a SPLIT POINT, not the
 * pivot's final sorted slot. edges[p..return] are all <= pivot and
 * edges[return+1..q] are all >= pivot, but the element sitting at the
 * returned index has no guarantee of already being in its final position
 * and must still be included in further sorting (see QuickSort below).
 * Safety note: the original pivot value always remains somewhere inside
 * [p,q] and acts as a sentinel that halts both inner while-loops, so
 * e_minus/e_plus can never scan past the p/q bounds. */
int partition(GraphEdge* edges, int count, int p, int q){

    //Handle edge cases
    if(p < 0 || q >=count){
        return -1;
    }

    int e_minus = p;
    int e_plus  = q;

    while( e_minus < e_plus ){
        while(edges[e_plus].w > edges[p].w){
            e_plus--;
        }

        while(edges[e_minus].w < edges[p].w){
            e_minus++;
        }

        if(e_minus < e_plus){
            //SWAP
            GraphEdge tmp  = edges[e_minus];
            edges[e_minus]  = edges[e_plus];
            edges[e_plus]   = tmp;

            e_minus ++;
            e_plus --;
        }
    }

    return e_plus;
}

void QuickSort(GraphEdge *edges,int count, int p,int q){
    //BASE
    if(p>=q){
        return;
    }

    int pivot_position = partition(edges, count, p , q);

    /* Because partition() uses the Hoare scheme, pivot_position is only a
     * split point, not the pivot's final index: the element at
     * pivot_position is NOT guaranteed to be already sorted, so it must be
     * included in the left recursive call (range [p, pivot_position]).
     * Using [p, pivot_position-1] here would silently skip that element
     * from both halves and leave the array unsorted. */
    QuickSort(edges, count, p, pivot_position);

    QuickSort(edges, count, pivot_position+1, q);
}

void PrimMST(GraphEdge* edges, int count){
    
}

void QuickSortKruskalMST(GraphEdge* edges, int count,int p, int q){
    if(p >= q){
        return;
    }

    int pivot_position = partition(edges,count,p,q);

    if(pivot_position > p){
        QuickSortKruskalMST(edges,count,p,pivot_position);
    }

    //In this point we have piovt_position = p 
    //printf("MST QICK FIRST EDGE:  [%i,%i].cost=%f \n",edges[p].u, edges[p].v, edges[p].w);

    if(pivot_position < q){
        //If we reach this point, than  p < pivot_position < q
        QuickSortKruskalMST(edges,count,pivot_position+1,q);
    }
}

void traversal_free(Traversal *t)
{
    if (!t)
        return;
    free(t->order);
    free(t->parent);
    free(t->dist);
    free(t);
}

/*
 * u->v (or u--v) is a tree edge of the traversal if v was discovered by
 * going through u, i.e. if parent[v] == u. In an undirected graph the
 * edge is emitted only once, so it must be checked in both directions.
 */
static bool is_tree_edge(const Graph *g, const Traversal *t, int u, int v)
{
    if (!t)
        return false;
    return t->parent[v] == u || (!g->directed && t->parent[u] == v);
}

int graph_export_dot(const Graph *g, const Traversal *t, const char *path)
{
    if (!g || !path || (t && t->n != g->n))
        return GRAPH_ERR_ARG;

    FILE *f = fopen(path, "w");
    if (!f)
        return GRAPH_ERR_ARG;

    /* DOT distinguishes directed graphs (digraph, "->" edges) from undirected ones (graph, "--") */
    const char *edge_op = g->directed ? "->" : "--";
    fprintf(f, "%s G {\n", g->directed ? "digraph" : "graph");
    fprintf(f, "  rankdir=LR;\n");
    fprintf(f, "  node [shape=circle, style=filled, fillcolor=white];\n\n");

    /* Nodes. With a Traversal: source in orange, reached vertices in light
     * blue with the distance as a label, unreached ones left white and unlabeled. */
    for (int v = 0; v < g->n; v++) {
        if (t && t->dist[v] >= 0)
            fprintf(f, "  %d [label=\"%d\\nd=%d\", fillcolor=%s];\n",
                    v, v, t->dist[v],
                    t->dist[v] == 0 ? "orange" : "lightblue");
        else
            fprintf(f, "  %d;\n", v);
    }
    fprintf(f, "\n");

    /* Edges, via the iterator: independent of the representation. In an
     * undirected graph each edge is seen twice (from both endpoints):
     * we emit it only from the side with the smaller index. */
    for (int u = 0; u < g->n; u++) {
        GraphIter it;
        g->ops->iter_out(g, u, &it);
        int v;
        double w;
        while (g->ops->iter_next(&it, &v, &w)) {
            if (!g->directed && v < u)
                continue;
            if (is_tree_edge(g, t, u, v))
                fprintf(f, "  %d %s %d [label=\"%g\", color=red, penwidth=2.0];\n",
                        u, edge_op, v, w);
            else
                fprintf(f, "  %d %s %d [label=\"%g\", color=gray50];\n",
                        u, edge_op, v, w);
        }
    }

    fprintf(f, "}\n");
    return fclose(f) == 0 ? GRAPH_OK : GRAPH_ERR_ARG;
}