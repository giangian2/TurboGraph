/*
 * Out Starts graph reperesentation
 *
 * g->data is an array of n Stars, each star is an array of outgoing edges.
 * 
 *
 * This i snot dynamic and allows iter_out in O(1) ...perfect for BFS
 */
#include <stdlib.h>
#include "../include/Graph.h"


/*
 * Mappa del blocco unico: nessun campo qui contiene dati, sono tutti
 * puntatori dentro la stessa allocazione a cui punta g->data.
 *
 * La out star c'e' sempre. La in star (rheads/from/rw) serve solo ai
 * grafi diretti: in un grafo non diretto la out star contiene gia'
 * entrambi i versi di ogni arco, quindi i tre campi restano NULL e
 * iter_in delega a iter_out.
 */
typedef struct
{
    //OUT
    int *heads;
    int *to;
    //IN (NULL se il grafo non e' diretto)
    int *rheads;
    int *from;
    //WEIGHTS (rw NULL se il grafo non e' diretto)
    double *w;
    double *rw;
    //TOTAL ARCS (IN CASE IT IS NOT DIRECTED)
    size_t n_arcs;

} Star;


static const GraphOps star_ops = {
    .add_edge    = star_add_edge,
    .remove_edge = star_remove_edge,
    .get_weight  = star_get_weight,
    .out_degree  = star_out_degree,
    .in_degree   = star_in_degree,
    .iter_out    = star_iter_out,
    .iter_in     = star_iter_in,
    .iter_next   = star_iter_next,
    .destroy     = star_destroy,
};


static int star_add_edge(Graph *g, int u, int v, double w)
{
    return GRAPH_ERR_IMMUTABLE;
}

static int star_remove_edge(Graph *g, int u, int v, double w)
{
    return GRAPH_ERR_IMMUTABLE;
}


static size_t arc_count(const Graph *g, const GraphEdge *e, size_t m)
{
    if (g->directed)
        return m;
    size_t a = 0;
    for (size_t k = 0; k < m; k++)
        a += (e[k].u == e[k].v) ? 1 : 2;
    return a;
}

/*
 * Riempimento: counting sort in quattro passate lineari, O(n + m), senza
 * allocazioni temporanee. heads[] arriva azzerato dalla calloc.
 *
 * Out star: gli archi sono raggruppati per sorgente. Se il grafo non e'
 * diretto ogni arco va scritto in tutti e due i blocchi, tranne i cappi
 * (stessa condizione u != v di arc_count).
 */
static void fill_out(const Graph *g, Star *s, const GraphEdge *edges, size_t m)
{
    const int n = g->n;
    int *head = s->heads;

    /* 1. gradi, sfasati di uno: head[x+1] = grado di x */
    for (size_t k = 0; k < m; k++) {
        head[edges[k].u + 1]++;
        if (!g->directed && edges[k].u != edges[k].v)
            head[edges[k].v + 1]++;
    }

    /* 2. somma prefissa: head[x] = inizio del blocco di x */
    for (int x = 0; x < n; x++)
        head[x + 1] += head[x];

    /* 3. head[x] fa da cursore di scrittura: niente array di cursori */
    for (size_t k = 0; k < m; k++) {
        int u = edges[k].u;
        int v = edges[k].v;
        int p = head[u]++;
        s->to[p] = v;
        s->w[p]  = edges[k].w;
        if (!g->directed && u != v) {
            p = head[v]++;
            s->to[p] = u;
            s->w[p]  = edges[k].w;
        }
    }

    /* 4. il cursore ha lasciato head[x] = inizio di x+1: l'array giusto
     *    traslato di uno. All'indietro, per non sovrascrivere celle da leggere */
    for (int x = n; x >= 1; x--)
        head[x] = head[x - 1];
    head[0] = 0;
}

/*
 * In star: identica, ma raggruppa per destinazione e memorizza la sorgente.
 * Serve solo ai grafi diretti, quindi non c'e' mai la doppia scrittura.
 */
static void fill_in(const Graph *g, Star *s, const GraphEdge *edges, size_t m)
{
    const int n = g->n;
    int *head = s->rheads;

    for (size_t k = 0; k < m; k++)
        head[edges[k].v + 1]++;

    for (int x = 0; x < n; x++)
        head[x + 1] += head[x];

    for (size_t k = 0; k < m; k++) {
        int p = head[edges[k].v]++;
        s->from[p] = edges[k].u;
        s->rw[p]   = edges[k].w;
    }

    for (int x = n; x >= 1; x--)
        head[x] = head[x - 1];
    head[0] = 0;
}

int star_init_from_edges(Graph *g, const GraphEdge *edges, size_t m){
    const int    tot_nodes = g->n;
    const bool   back      = g->directed;   //la in star serve solo ai grafi diretti
    const size_t tot_arcs  = arc_count(g,edges,m); //archi fisici: 2m - cappi se non diretto

    /*
     * Offset dei segmenti nel blocco unico, calcolati con un accumulatore:
     * per ogni segmento si registra l'offset corrente e poi si avanza di
     * n_elementi * sizeof(tipo). I segmenti opzionali avanzano dentro l'if,
     * quindi se non ci sono il segmento seguente prende il loro posto e
     * "total" cade da solo alla fine: non c'e' una riga che riassume il
     * totale e che possa dimenticarsi un pezzo.
     *
     * Ordine per allineamento decrescente (prima i double, poi gli int):
     * cosi' ogni segmento cade gia' allineato e non serve padding interno.
     */
    size_t off       = (sizeof(Star) + 7u) & ~(size_t)7u;
    size_t o_w       = off;  off += tot_arcs * sizeof(double);
    size_t o_rw      = 0;
    size_t o_rheads  = 0;
    size_t o_from    = 0;

    if (back) { 
        o_rw = off;  off += tot_arcs * sizeof(double); 
    }

    size_t o_heads   = off;  off += (size_t)(tot_nodes + 1) * sizeof(int);
    size_t o_to      = off;  off += tot_arcs * sizeof(int);

    if (back) {
        o_rheads = off;      off += (size_t)(tot_nodes + 1) * sizeof(int);
        o_from   = off;      off += tot_arcs * sizeof(int);
    }

    //ALLOCAZIONE UNICA E CONTIGUA. calloc, non malloc: la passata di
    //conteggio dei gradi assume heads[] (e rheads[]) gia' azzerati.
    char *blk = calloc(1, off);
    if (!blk)
        return GRAPH_ERR_ALLOC;

    Star *s   = (Star*)blk;
    s->w      = (double*)(blk + o_w);
    s->heads  = (int*)   (blk + o_heads);
    s->to     = (int*)   (blk + o_to);
    //non diretto: nessun segmento allocato, il NULL fa anche da flag
    s->rw     = back ? (double*)(blk + o_rw)     : NULL;
    s->rheads = back ? (int*)   (blk + o_rheads) : NULL;
    s->from   = back ? (int*)   (blk + o_from)   : NULL;
    s->n_arcs = tot_arcs;

    fill_out(g, s, edges, m);
    if (back)
        fill_in(g, s, edges, m);

    g->data = blk;
    g->m    = m;            //conteggio LOGICO degli archi, non tot_arcs
    g->ops  = &star_ops;
    return GRAPH_OK;
}
