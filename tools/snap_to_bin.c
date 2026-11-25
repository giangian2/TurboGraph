/*
 * Converts a SNAP edge list into the binary format of the MST suite, weighted
 * by the Jaccard distance (see bench/jaccard.h).
 *
 *   zcat graph.txt.gz | bin/snap_to_bin out.bin
 *
 * One edge per line, two ids separated by blanks or a comma; lines starting
 * with # or % and lines that do not start with a digit (a CSV header) are
 * skipped. Directed inputs are made undirected. Ids may be up to 38 digits
 * (Google+ uses 21) and are compacted to 0..n-1 in increasing order.
 */
#include "../bench/jaccard.h"
#include <ctype.h>

typedef unsigned __int128 u128;

static int cmp_u128(const void* a, const void* b)
{
    u128 x = *(const u128*)a, y = *(const u128*)b;
    return (x > y) - (x < y);
}

static const char* parse_id(const char* p, u128* out)
{
    while (*p == ' ' || *p == '\t' || *p == ',')
        p++;
    if (!isdigit((unsigned char)*p))
        return NULL;
    u128 x = 0;
    while (isdigit((unsigned char)*p))
        x = x * 10 + (u128)(*p++ - '0');
    *out = x;
    return p;
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: zcat graph.txt.gz | %s out.bin\n", argv[0]);
        return 2;
    }

    size_t cap = 1 << 20, m = 0;
    u128*  ends = malloc(sizeof(u128) * 2 * cap);
    char   line[4096];
    while (ends && fgets(line, sizeof line, stdin))
    {
        u128        a, b;
        const char* p = parse_id(line, &a);
        if (!p || !(p = parse_id(p, &b)))
            continue;
        if (m == cap)
            ends = realloc(ends, sizeof(u128) * 2 * (cap *= 2));
        if (!ends)
            break;
        ends[2 * m] = a, ends[2 * m + 1] = b, m++;
    }
    if (!ends)
        return fprintf(stderr, "out of memory\n"), 1;

    // compact the ids: sorted unique copy, then a binary search per endpoint
    u128* ids = malloc(sizeof(u128) * 2 * (m ? m : 1));
    if (!ids)
        return fprintf(stderr, "out of memory\n"), 1;
    memcpy(ids, ends, sizeof(u128) * 2 * m);
    qsort(ids, 2 * m, sizeof(u128), cmp_u128);
    size_t n = 0;
    for (size_t i = 0; i < 2 * m; i++)
        if (n == 0 || ids[i] != ids[n - 1])
            ids[n++] = ids[i];

    GraphEdge* raw = malloc(sizeof(GraphEdge) * (m ? m : 1));
    if (!raw)
        return fprintf(stderr, "out of memory\n"), 1;
    for (size_t i = 0; i < m; i++)
    {
        int end[2];
        for (int j = 0; j < 2; j++)
        {
            size_t lo = 0, hi = n;
            while (lo < hi)
            {
                size_t mid = (lo + hi) / 2;
                if (ids[mid] < ends[2 * i + j])
                    lo = mid + 1;
                else
                    hi = mid;
            }
            end[j] = (int)lo;
        }
        raw[i] = (GraphEdge){end[0], end[1], 1.0};
    }
    free(ends), free(ids);

    size_t     e;
    GraphEdge* w = jaccard_weights((int)n, raw, m, &e);
    free(raw);
    if (!w || bin_write(argv[1], (int)n, w, e) != 0)
        return fprintf(stderr, "conversion failed\n"), 1;
    printf("%s: n = %zu, m = %zu, avg degree %.1f\n", argv[1], n, e, 2.0 * (double)e / (double)n);
    free(w);
    return 0;
}
