#ifndef EXPORT_H
#define EXPORT_H

#include "../include/Graph.h"
#include "../include/Traversal.h"

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
int graph_export_dot(const Graph* g, const Traversal* t, const char* path);

#endif /* EXPORT_H */
