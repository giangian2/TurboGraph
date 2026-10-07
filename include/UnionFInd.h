#ifndef UNION_FIND_H
#define UNION_FIND_H

/**
 *	In order to make the algorithm efficient, elements
 *	of the Trees need to be integers and indexed by an
 *	integer. If different types are required from application
 *	domain, use an outstanding mapping to bind your instances
 *	inside the uf.
 */
typedef struct {
    int *parent;
    int *rank;
    size_t count;
} uf_header_t;

#define uf__hdr(uf) (((uf_header_t*)(void*)(uf)) -1)
#define uf_create(T, count) ((__typeof(T)__)uf__create(sizeof(T),(count)))
#define uf_free(uf) (uf__free(uf__hdr(uf)))
#define uf_find(node1, node2) ()


#ifndef UNION_FIND_IMPL
#define UNION_FIND_IMPL

static void* uf__create(size_t elem_size, size_t count)
{
	uf_header_t* uf_header = (uf_header_t*)calloc(sizeof(uf_header_t)+(count*2)*sizeof(int));
	if(uf_header == NULL)
	{
		return NULL;
	}
	/*
	 *	NOTE: th epointers to the tree elements are allocated wihting a single
	 *	RAM malloc. So we point the 2 staring offsets to the relative RAM
	 *	addresses... evaluate using OFFSETS instead of POINT. ARITHMETICS.
	 *
	 */
	uf_header->count 	= count;
	uf_header->parent 	= ((int*)((uf_header) + 1));
	uf_header->rank		= ((int*)((uf_header) + 1) + count);
}

#endif
#endif
