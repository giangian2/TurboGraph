/*
 * Parser di un sottoinsieme "edge list" del formato Graphviz DOT (vedi
 * Importer.h per la grammatica supportata). Due passate concettuali in
 * una: si legge il file una volta sola accumulando gli archi in un
 * array dinamico e tenendo traccia dell'id di vertice massimo visto,
 * poi si costruisce il grafo con graph_from_edges() -- l'unico modo per
 * ottenere n corretto e' aver gia' visto tutti gli id.
 */
#include "../include/Importer.h"
#include "../include/Graph.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Buffer dinamico di GraphEdge, cresce raddoppiando. */
typedef struct
{
    GraphEdge* data;
    size_t     count;
    size_t     cap;
} EdgeBuf;

/**
 * @return True se l'arco (u, v, w) e' stato accodato, False se realloc
 * fallisce (EdgeBuf resta valido e invariato in quel caso).
 */
static bool edgebuf_push(EdgeBuf* b, int u, int v, double w)
{
    if (b->count == b->cap)
    {
        size_t     new_cap = b->cap ? b->cap * 2 : 64;
        GraphEdge* tmp     = realloc(b->data, new_cap * sizeof *tmp);
        if (!tmp)
            return false;
        b->data = tmp;
        b->cap  = new_cap;
    }
    b->data[b->count++] = (GraphEdge){u, v, w};
    return true;
}

static const char* skip_ws(const char* p)
{
    while (isspace((unsigned char)*p))
        p++;
    return p;
}

static bool starts_with(const char* s, const char* kw)
{
    return strncmp(s, kw, strlen(kw)) == 0;
}

/*
 * Legge un intero non negativo a partire da *p (dopo aver saltato gli
 * spazi), avanzando *p oltre le cifre lette. Ritorna False (e lascia *p
 * invariato) se non c'e' nessuna cifra, cioe' la riga non e' un id di
 * vertice valido a quel punto.
 */
static bool parse_uint(const char** p, int* out)
{
    const char* s = skip_ws(*p);
    if (!isdigit((unsigned char)*s))
        return false;
    char* end;
    long  val = strtol(s, &end, 10);
    if (val < 0 || val > INT_MAX)
        return false;
    *out = (int)val;
    *p   = end;
    return true;
}

Graph* graph_import_dot(const char* path, GraphRepr repr)
{
    if (!path)
        return NULL;

    FILE* f = fopen(path, "r");
    if (!f)
        return NULL;

    EdgeBuf edges          = {NULL, 0, 0};
    int     max_id         = -1; /* id di vertice piu' alto incontrato    */
    bool    directed       = true;
    bool    directed_known = false;
    bool    ok             = true;
    char    line[1024];

    while (ok && fgets(line, sizeof line, f))
    {
        /* Riga piu' lunga del buffer: scarta il resto fino al newline
         * cosi' non viene riletto come una nuova riga a meta'. */
        if (!strchr(line, '\n') && !feof(f))
        {
            int c;
            while ((c = fgetc(f)) != '\n' && c != EOF)
                ;
        }

        const char* p = skip_ws(line);

        if (*p == '\0' || *p == '%' || *p == '#' || (p[0] == '/' && p[1] == '/'))
            continue; /* riga vuota o commento */

        if (starts_with(p, "strict"))
            p = skip_ws(p + 6);
        if (starts_with(p, "digraph"))
        {
            directed       = true;
            directed_known = true;
            continue;
        }
        if (starts_with(p, "graph"))
        {
            directed       = false;
            directed_known = true;
            continue;
        }
        if (*p == '{' || *p == '}')
            continue; /* delimitatori di blocco */

        /* Catena "id (OP id)*", es. "3 -> 4 -> 5;" -> archi 3-4, 4-5. */
        int prev;
        if (!parse_uint(&p, &prev))
            continue; /* riga non riconosciuta: ignorata */
        if (prev > max_id)
            max_id = prev;

        for (;;)
        {
            const char* q     = skip_ws(p);
            bool        arrow = q[0] == '-' && q[1] == '>';
            bool        dash  = q[0] == '-' && q[1] == '-';
            if (!arrow && !dash)
                break;

            if (!directed_known)
            {
                directed       = arrow;
                directed_known = true;
            }

            const char* r = q + 2;
            int         next;
            if (!parse_uint(&r, &next))
                break; /* operatore senza destinazione: riga malformata */
            if (next > max_id)
                max_id = next;

            if (!edgebuf_push(&edges, prev, next, 1.0))
            {
                ok = false;
                break;
            }
            prev = next;
            p    = r;
        }
        /* Il resto della riga (attributi fra [ ], ';', commenti) e' ignorato. */
    }

    fclose(f);

    if (!ok || max_id < 0)
    {
        free(edges.data);
        return NULL;
    }

    Graph* g = graph_from_edges(max_id + 1, directed, edges.data, edges.count, repr);
    free(edges.data);
    return g;
}
