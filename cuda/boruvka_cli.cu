/*
 * boruvka_cli edges.bin n m runs
 *
 * edges.bin holds m records of GraphEdge (int u, int v, double w). Runs the
 * GPU Boruvka once as warm-up and `runs` times for the measure, then prints
 *   <edges in the forest> <total weight> <rounds> <h2d ms> <compute ms> <d2h ms>
 * with the times of the best run (smallest total).
 */
#include "boruvka.cu"
#include <stdlib.h>

int main(int argc, char** argv)
{
    if (argc < 5)
        return fprintf(stderr, "usage: boruvka_cli edges.bin n m runs\n"), 2;
    int    n    = atoi(argv[2]);
    size_t m    = (size_t)atoll(argv[3]);
    int    runs = atoi(argv[4]);

    BoruvkaEdge* edges = (BoruvkaEdge*)malloc(m * sizeof(BoruvkaEdge));
    int*         idx   = (int*)malloc((size_t)n * sizeof(int));
    FILE*        f     = fopen(argv[1], "rb");
    if (!edges || !idx || !f || fread(edges, sizeof(BoruvkaEdge), m, f) != m)
        return fprintf(stderr, "cannot read %s\n", argv[1]), 2;
    fclose(f);

    BoruvkaTimes best = {1e30f, 1e30f, 1e30f, 0}, t = {0};
    int          count = boruvka_gpu(n, m, edges, idx, &t); /* warm-up: context, module load */
    for (int r = 0; r < runs && count >= 0; r++)
    {
        count = boruvka_gpu(n, m, edges, idx, &t);
        if (t.h2d_ms + t.compute_ms + t.d2h_ms < best.h2d_ms + best.compute_ms + best.d2h_ms)
            best = t;
    }
    if (count < 0)
        return 1;

    double w = 0;
    for (int i = 0; i < count; i++)
        w += edges[idx[i]].w;
    printf("%d %.12f %d %.4f %.4f %.4f\n", count, w, best.rounds, best.h2d_ms, best.compute_ms,
           best.d2h_ms);
    return 0;
}
