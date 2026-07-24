#ifndef GRAPH_H
#define GRAPH_H

#include <stddef.h>
#include <stdbool.h>

/*
 * A graph with n vertices numbered 0 .. n-1, directed or undirected,
 * with weighted edges. A weight of 0.0 means "no edge" everywhere in
 * this library, so 0.0 is not a valid edge weight.
 *
 * Three interchangeable representations sit behind the same interface:
 *
 *   GRAPH_MATRIX  adjacency matrix, one flat n*n block. O(1) edge lookup,
 *                 O(n) neighbor sweeps, O(n^2) memory. Dense graphs.
 *   GRAPH_LIST    adjacency lists (linked chains). O(deg) everything,
 *                 O(n+m) memory, cheap insertion. Sparse dynamic graphs.
 *   GRAPH_STAR    forward/backward star (CSR). Compact contiguous arrays,
 *                 fastest traversals, O(1) degrees -- but immutable:
 *                 built once via graph_from_edges() or graph_convert().
 */

typedef enum {
    GRAPH_MATRIX,
    GRAPH_LIST,
    GRAPH_STAR
} GraphRepr;

/* Error codes returned by functions that return int. */
typedef enum {
    GRAPH_OK            = 0,
    GRAPH_ERR_ARG       = -1,   /* vertex out of range / invalid argument */
    GRAPH_ERR_IMMUTABLE = -2,   /* mutation attempted on a star graph     */
    GRAPH_ERR_ALLOC     = -3    /* out of memory                          */
} ERROR_CODES;

typedef struct Graph    Graph;
typedef struct GraphOps GraphOps;   /* private vtable, see src/internal.h */

typedef struct {
    int    u, v;
    double w;
} GraphEdge;

/*
 * Neighbor iterator. One struct for every representation; each uses the
 * cursor fields it needs. Obtain with graph_iter_out()/graph_iter_in(),
 * advance with graph_iter_next(). Do not mutate the graph while iterating.
 */
typedef struct {
    const Graph *g;
    int    u;      /* vertex whose neighbors are being walked   */
    int    i;      /* cursor: column (matrix) or index (star)   */
    void  *node;   /* cursor: current chain node (list)         */
    bool   in;     /* walking in-neighbors instead of out       */
} GraphIter;

struct GraphOps {
    int    (*add_edge)(Graph *g, int u, int v, double w);
    int    (*remove_edge)(Graph *g, int u, int v);
    double (*get_weight)(const Graph *g, int u, int v);
    int    (*out_degree)(const Graph *g, int u);
    int    (*in_degree)(const Graph *g, int u);
    void   (*iter_out)(const Graph *g, int u, GraphIter *it);
    void   (*iter_in)(const Graph *g, int u, GraphIter *it);
    bool   (*iter_next)(GraphIter *it, int *v, double *w);
    void   (*destroy)(Graph *g);
};

struct Graph {
    int       n;          /* number of vertices                          */
    size_t    m;          /* current number of (logical) edges           */
    bool      directed;
    GraphRepr repr;
    const GraphOps *ops;  /* operations of this representation           */
    void     *data;       /* representation-specific storage             */
};




/* Representation constructors: fill g->data and g->ops for an already
 * populated Graph shell (n, directed, repr set; m = 0). Return GRAPH_OK
 * or GRAPH_ERR_ALLOC. */
int matrix_init(Graph *g);
int list_init(Graph *g);

/* Star graphs are built in one shot from a validated edge list. */
int star_init_from_edges(Graph *g, const GraphEdge *edges, size_t m);

/* ---- construction / destruction ---------------------------------------- */

/* Empty graph. repr must be GRAPH_MATRIX or GRAPH_LIST (a star graph is
 * immutable, so an empty one would be useless): returns NULL for STAR. */
Graph *graph_create(int n, bool directed, GraphRepr repr);

/* Bulk build from an edge array (the only way to create a GRAPH_STAR
 * directly). Edges must be valid (vertices in range, weight != 0);
 * for STAR the array must not contain duplicate edges. */
Graph *graph_from_edges(int n, bool directed,
                        const GraphEdge *edges, size_t m, GraphRepr repr);

void graph_free(Graph *g);

/* ---- edges ------------------------------------------------------------- */

/* Adding an existing edge updates its weight (never duplicates).
 * Returns GRAPH_OK or an error code; GRAPH_ERR_IMMUTABLE on star graphs. */
int    graph_add_edge(Graph *g, int u, int v, double w);
int    graph_remove_edge(Graph *g, int u, int v);

bool   graph_has_edge(const Graph *g, int u, int v);
double graph_get_weight(const Graph *g, int u, int v);  /* 0.0 = absent */

int    graph_out_degree(const Graph *g, int u);   /* < 0 on error */
int    graph_in_degree(const Graph *g, int u);

/* ---- neighbor iteration ------------------------------------------------ */

int  graph_iter_out(const Graph *g, int u, GraphIter *it);
int  graph_iter_in(const Graph *g, int u, GraphIter *it);
/* Yields the next neighbor into *v (and its weight into *w, if non-NULL);
 * returns false when exhausted. */
bool graph_iter_next(GraphIter *it, int *v, double *w);

/* ---- traversals -------------------------------------------------------- */

/*
 * Result of a BFS or DFS from a source vertex. Arrays have length n and
 * are indexed by vertex; unreached vertices have parent == -1, dist == -1.
 *
 *   order   the `count` reached vertices, in visit order
 *   parent  traversal-tree parent (-1 for the source and unreached)
 *   dist    BFS: edge distance from the source
 *           DFS: depth in the DFS tree
 */
typedef struct {
    int *order;
    int *parent;
    int *dist;
    int  count; //numero totale di vertici raggiunti
    int  n;     //numero totale di vertific del grafo
} Traversal;

Traversal *graph_bfs(const Graph *g, int source);
Traversal *graph_dfs(const Graph *g, int source);
void       traversal_free(Traversal *t);

void QuickSortKruskalMST(GraphEdge* edges, int count,int p, int q);

void QuickSort(GraphEdge* edges, int count, int p, int q);


/* ---- export ------------------------------------------------------------ */

/*
 * Scrive il grafo su `path` in formato Graphviz DOT (renderizzabile con
 * `dot -Tpng file.dot -o file.png`). Generica: usa solo gli iteratori,
 * quindi funziona con qualunque rappresentazione.
 *
 * Se t != NULL evidenzia la visita: i nodi raggiunti mostrano la distanza
 * dalla sorgente e gli archi dell'albero di visita (parent[]) sono rossi
 * e spessi -- i cammini minimi della BFS si leggono seguendo gli archi
 * rossi a ritroso. Con t == NULL esporta il grafo e basta.
 *
 * Ritorna GRAPH_OK, o GRAPH_ERR_ARG su argomenti invalidi / errore di I/O.
 */
int graph_export_dot(const Graph *g, const Traversal *t, const char *path);

#endif /* GRAPH_H */
