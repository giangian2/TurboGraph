#ifndef IMPORTER_H
#define IMPORTER_H

#include "Graph.h"

/*
 * Import di un grafo da un file in formato Graphviz DOT (sottoinsieme
 * "edge list", il tipo prodotto da tool come SNAP/Graphviz stesso e usato
 * in europe_roads.dot):
 *
 *   digraph G {
 *     1 -> 2;
 *     2 -> 3;
 *     ...
 *   }
 *
 * Regole di parsing:
 *   - "digraph" in testa al file  -> grafo diretto  (archi "->")
 *   - "graph"    in testa al file -> grafo non diretto (archi "--")
 *     Se l'header manca, la direzione e' dedotta dal primo operatore
 *     d'arco incontrato ("->" o "--").
 *   - righe vuote e commenti (che iniziano con '%', '#' o "//") sono
 *     ignorati; il file di esempio usa '%' per i commenti.
 *   - catene sulla stessa riga ("1 -> 2 -> 3;") sono supportate ed
 *     espanse nei singoli archi 1->2, 2->3.
 *   - eventuali attributi fra "[ ]" o il ';' finale sono ignorati.
 *
 * Il formato non porta pesi: ogni arco importato ha peso 1.0 (il minimo
 * valore diverso da 0.0, che in questa libreria significa "nessun arco").
 *
 * Gli identificatori dei vertici sono usati cosi' come compaiono nel file
 * (nessuno shift): se il file numera da 1 come europe_roads.dot, il
 * vertice 0 risultera' isolato. n = (id massimo visto nel file) + 1.
 *
 * repr sceglie la rappresentazione del grafo risultante (vedi Graph.h).
 *
 * Ritorna il grafo allocato in heap, oppure NULL se il file non si apre,
 * non contiene alcun arco/nodo valido, o l'allocazione fallisce.
 */
Graph *graph_import_dot(const char *path, GraphRepr repr);

#endif /* IMPORTER_H */
