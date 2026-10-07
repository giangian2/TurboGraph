#ifndef UNION_FIND_H
#define UNION_FIND_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define DEFAULT_UF_TYPE int

#ifndef UF_TYPE
#define UF_TYPE DEFAULT_UF_TYPE
#endif

#ifndef UF_NULL_VALUE
#define UF_NULL_VALUE ((UF_TYPE)-1)
#endif
/**
 *	In order to make the algorithm efficient, elements
 *	of the Trees need to be integers and indexed by an
 *	integer. If different types are required from application
 *	domain, use an outstanding mapping to bind your instances
 *	inside the uf (e.g. a hashmap T -> index).
 *
 *	Memory layout (single malloc, shadow header):
 *
 *	  [ uf_header_t | UF_TYPE parent[count] | UF_TYPE rank[count] ]
 *	                  ^-- pointer handed to the user (UF_TYPE*)
 *
 *	parent[i] == UF_NULL_VALUE means i is the root of its tree.
 *
 *	  UF_TYPE *uf = uf_create(n);
 *	  uf_union(uf, 1, 2);
 *	  if (uf_connected(uf, 1, 2)) ...
 *	  uf_free(uf);
 */
typedef struct {
    UF_TYPE *parent;
    UF_TYPE *rank;
    size_t count;	// number of elements
    size_t sets;	// number of disjoint trees, starts at count
} uf_header_t;


#define uf__hdr(uf) 			(((uf_header_t*)(void*)(uf)) - 1)
#define uf_create(count) 		((UF_TYPE*)uf__create((count)))
#define uf_free(uf) 			(uf__free(uf__hdr(uf)))
#define uf_count(uf)			(uf__hdr(uf)->count)
#define uf_sets(uf)				(uf__hdr(uf)->sets)
#define uf_find(uf, u)			(uf__find(uf__hdr(uf), (UF_TYPE)(u)))
// Returns true if u and v were in different trees (i.e. a merge happened)
#define uf_union(uf, u, v)		(uf__union(uf__hdr(uf), (UF_TYPE)(u), (UF_TYPE)(v)))
#define uf_connected(uf, u, v)	(uf_find(uf, u) == uf_find(uf, v))

#ifndef UNION_FIND_IMPL
#define UNION_FIND_IMPL
#include <stdlib.h>
#include <stdio.h>

static inline void* uf__create(size_t count)
{
	uf_header_t* uf_header = (uf_header_t*)malloc(sizeof(uf_header_t)+(count*2)*sizeof(UF_TYPE));
	if(uf_header == NULL)
	{
		return NULL;
	}
	/*
	 *	NOTE: the pointers to the tree elements are allocated within a single
	 *	RAM malloc. So we point the 2 starting offsets to the relative RAM
	 *	addresses... evaluate using OFFSETS instead of POINT. ARITHMETICS.
	 *
	 */
	uf_header->count 	= count;
	uf_header->sets 	= count;
	uf_header->parent 	= ((UF_TYPE*)((uf_header) + 1));
	uf_header->rank		= ((UF_TYPE*)((uf_header) + 1) + count);

	for(size_t i = 0; i < count; i++)
	{
		uf_header->rank[i] 		= 1;
		uf_header->parent[i] 	= UF_NULL_VALUE;
	}

	return (void*)(uf_header + 1);
}

static inline void uf__free(uf_header_t* uf_head)
{
	//Single malloc with shadow header!
	free(uf_head);
}

static inline UF_TYPE uf__find(uf_header_t* uf_header, UF_TYPE u)
{
	if((size_t)u >= uf_header->count)
	{
		printf("Union find index out of range!");
		return UF_NULL_VALUE;
	}

	UF_TYPE root = u;

	while (uf_header->parent[root] != UF_NULL_VALUE)
	{
		root = uf_header->parent[root];
	}

	// Fast path: u is the root or already hooked to it, nothing to compress
	if (u == root || uf_header->parent[u] == root)
	{
		return root;
	}

	// Path compression: second pass hooks every node on the path directly to the root
	while (u != root)
	{
		UF_TYPE next 			= uf_header->parent[u];
		uf_header->parent[u] 	= root;
		u 						= next;
	}

	return root;
}

static inline bool uf__union(uf_header_t* uf_header, UF_TYPE u, UF_TYPE v)
{
	UF_TYPE root_u = uf__find(uf_header, u);
	UF_TYPE root_v = uf__find(uf_header, v);

	if(root_u == UF_NULL_VALUE || root_v == UF_NULL_VALUE || root_u == root_v)
	{
		return false;
	}

	// Union by rank: the shorter tree is attached under the taller one
	if(uf_header->rank[root_u] < uf_header->rank[root_v])
	{
		uf_header->parent[root_u] = root_v;
	}
	else if(uf_header->rank[root_u] > uf_header->rank[root_v])
	{
		uf_header->parent[root_v] = root_u;
	}
	else
	{
		uf_header->parent[root_v] = root_u;
		uf_header->rank[root_u]++;
	}

	uf_header->sets--;
	return true;
}


#endif
#endif
