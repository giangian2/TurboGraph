/*
 * Public API: argument validation + dispatch through g->ops.
 * Nothing here knows how any representation works.
 */
#include <stdio.h>
#include <stdlib.h>
#include "../include/Graph.h"
#include "../include/Queue.h"

/**
 * @param g Grafo
 * @param v Vertice
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
 * Alloca un Traversal per un grafo di n vertici, con tutti i vertici
 * marcati "non raggiunto" (parent = -1, dist = -1), come richiede il
 * contratto in Graph.h. Ritorna NULL se un'allocazione fallisce.
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
        traversal_free(t);   /* free(NULL) è legale: libera solo il riuscito */
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

    /* Capacita' g->n: per l'invariante sopra la coda non puo' riempirsi,
     * quindi gli enqueue sotto non possono fallire. */
    Queue *q = queue_create(g->n, sizeof(int));
    if (!q) {
        traversal_free(t);
        return NULL;
    }

    /* La sorgente e' il punto di partenza: distanza 0 da se stessa,
     * nessun padre (resta -1), primo vertice nell'ordine di visita. */
    t->dist[source] = 0;
    t->order[t->count++] = source;
    queue_enqueue(q, &source);

    /* Finche' c'e' un vertice scoperto ma non ancora processato... */
    int u;
    while (queue_dequeue(q, &u)) {
        /* Scorri i vicini uscenti di u (il vertice appena estratto,
         * NON la sorgente). L'iteratore vive sullo stack: nessun malloc. */
        GraphIter it;
        g->ops->iter_out(g, u, &it);

        int v;
        while (g->ops->iter_next(&it, &v, NULL)) {   /* peso ignorato */
            if (t->dist[v] != -1)
                continue;   /* gia' scoperto da un cammino non piu' lungo */

            /* Prima scoperta di v: u e' il suo padre nell'albero BFS e
             * la sua distanza e' un arco in piu' di quella di u. */
            t->dist[v]   = t->dist[u] + 1;
            t->parent[v] = u;
            t->order[t->count++] = v;
            queue_enqueue(q, &v);
        }
    }

    /* Coda vuota: nessun vertice raggiungibile resta da esplorare.
     * I vertici mai scoperti restano a parent == -1, dist == -1. */
    queue_free(q);
    return t;
}


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
 * u->v (o u--v) e' un arco dell'albero di visita se v e' stato scoperto
 * passando da u, cioe' se parent[v] == u. In un grafo non diretto l'arco
 * viene emesso una volta sola, quindi va controllato in entrambi i versi.
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

    /* DOT distingue grafi diretti (digraph, archi "->") e non (graph, "--") */
    const char *edge_op = g->directed ? "->" : "--";
    fprintf(f, "%s G {\n", g->directed ? "digraph" : "graph");
    fprintf(f, "  rankdir=LR;\n");
    fprintf(f, "  node [shape=circle, style=filled, fillcolor=white];\n\n");

    /* Nodi. Con un Traversal: sorgente arancione, raggiunti azzurri con la
     * distanza in etichetta, non raggiunti lasciati bianchi e anonimi. */
    for (int v = 0; v < g->n; v++) {
        if (t && t->dist[v] >= 0)
            fprintf(f, "  %d [label=\"%d\\nd=%d\", fillcolor=%s];\n",
                    v, v, t->dist[v],
                    t->dist[v] == 0 ? "orange" : "lightblue");
        else
            fprintf(f, "  %d;\n", v);
    }
    fprintf(f, "\n");

    /* Archi, via iteratore: indipendente dalla rappresentazione. In un
     * grafo non diretto ogni arco e' visto due volte (da entrambi gli
     * estremi): lo emettiamo solo dal lato con indice minore. */
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