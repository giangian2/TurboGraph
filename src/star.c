/*
 * Forward/backward star (CSR) in un blocco contiguo unico.
 *
 * g->data punta a un solo malloc che contiene, in quest'ordine: l'header
 * Star, poi i segmenti dati. Gli archi uscenti di u sono l'intervallo
 * [heads[u], heads[u+1]) di to[]/w[]: contigui, quindi out_degree e'
 * O(1) e iterare i vicini e' scorrere un array. Immutabile.
 *
 * La out star c'e' sempre. La in star (rheads/from/rw) serve solo ai
 * grafi diretti: in un grafo non diretto la out star contiene gia'
 * entrambi i versi di ogni arco, quindi iter_in delega a iter_out.
 *
 * POSITION INDEPENDENT: l'header non contiene puntatori ma offset in byte
 * relativi all'inizio del blocco. Il blocco resta quindi valido a
 * qualunque indirizzo venga copiato -- memoria device, file mappato,
 * socket -- perche' non c'e' niente da rilocare. Un puntatore host
 * copiato su GPU sarebbe spazzatura; un offset no. Vedi
 * graph_star_layout() in Graph.h per il caricamento.
 */
#include <stdlib.h>
#include "../include/Graph.h"


/*
 * Mappa del blocco. Offset in byte da qui, non puntatori.
 * 0 = segmento assente: nessun segmento puo' partire a 0, li' c'e' l'header.
 */
typedef struct
{
    size_t bytes;       //dimensione totale del blocco: quanto copiare
    size_t n_arcs;      //archi fisici = lunghezza di to/w (e di from/rw)
    //OUT
    size_t o_heads;
    size_t o_to;
    size_t o_w;
    //IN (0 se il grafo non e' diretto)
    size_t o_rheads;
    size_t o_from;
    size_t o_rw;

} Star;

/* Da offset a puntatore. Una addizione, che accanto all'accesso in
 * memoria che segue non e' misurabile. */
static inline int    *st_heads (const Star *s) { return (int*)   ((char*)s + s->o_heads); }
static inline int    *st_to    (const Star *s) { return (int*)   ((char*)s + s->o_to);    }
static inline double *st_w     (const Star *s) { return (double*)((char*)s + s->o_w);     }
static inline int    *st_rheads(const Star *s) { return (int*)   ((char*)s + s->o_rheads); }
static inline int    *st_from  (const Star *s) { return (int*)   ((char*)s + s->o_from);  }
static inline double *st_rw    (const Star *s) { return (double*)((char*)s + s->o_rw);    }


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
    const int n    = g->n;
    int    *head   = st_heads(s);
    int    *to     = st_to(s);
    double *w      = st_w(s);

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
        to[p] = v;
        w[p]  = edges[k].w;
        if (!g->directed && u != v) {
            p = head[v]++;
            to[p] = u;
            w[p]  = edges[k].w;
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
    const int n    = g->n;
    int    *head   = st_rheads(s);
    int    *from   = st_from(s);
    double *rw     = st_rw(s);

    for (size_t k = 0; k < m; k++)
        head[edges[k].v + 1]++;

    for (int x = 0; x < n; x++)
        head[x + 1] += head[x];

    for (size_t k = 0; k < m; k++) {
        int p = head[edges[k].v]++;
        from[p] = edges[k].u;
        rw[p]   = edges[k].w;
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

    //Header: offset, non puntatori. Nessun indirizzo assoluto nel blocco,
    //quindi copiarlo altrove (device, file, rete) non richiede rilocazione.
    //I tre offset della in star restano a 0 = assente se non e' diretto.
    Star *s     = (Star*)blk;
    s->bytes    = off;
    s->n_arcs   = tot_arcs;
    s->o_w      = o_w;
    s->o_heads  = o_heads;
    s->o_to     = o_to;
    s->o_rw     = o_rw;
    s->o_rheads = o_rheads;
    s->o_from   = o_from;

    fill_out(g, s, edges, m);
    if (back)
        fill_in(g, s, edges, m);

    g->data = blk;
    g->m    = m;            //conteggio LOGICO degli archi, non tot_arcs
    g->ops  = &star_ops;
    return GRAPH_OK;
}
