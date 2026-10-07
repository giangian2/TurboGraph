#ifndef UNION_FIND_H
#define UNION_FIND_H

#include <unistd.h>
#include <stdint.h>

#define DEFAULT_UF_TYPE uint16_t

#ifndef UF_TYPE
#define UF_TYPE DEFAULT_UF_TYPE
#endif
/**
 *	In order to make the algorithm efficient, elements
 *	of the Trees need to be integers and indexed by an
 *	integer. If different types are required from application
 *	domain, use an outstanding mapping to bind your instances
 *	inside the uf.
 */
typedef struct {
    UF_TYPE *parent;
    UF_TYPE *rank;
    size_t count;
} uf_header_t;

typedef enum{
	NOT_OVERLAPPING 			= 0,
	OVERLAPPING_FIRST_LOW_RANK 	= 1,
	OVERLAPPING_SECOND_LOW_RANF = 2,
	ERRROR						= 3
} uf_result_t;

#define uf__koff(uf)			((size_t)((char*)&(uf)->key - (char*)(uf)))
#define uf__ksize(uf)			(sizoef((uf)->key))
#define uf__hdr(uf) 			(((uf_header_t*)(void*)(uf)) -1)
#define uf_create(T, count) 	((__typeof(T)__)uf__create(sizeof(T),(count)))
#define uf_free(uf) 			(uf__free(uf__hdr(uf)))
#define uf_find(f, s, uf)		(														\
									uf__find((void*)(f), 								\
											(void*)(s), 								\	
											uf__koff(uf), 								\
											uf__ksize(uf))								\
								)

#ifndef UNION_FIND_IMPL
#define UNION_FIND_IMPL
#include <stdlib.h>
#include <stdio.h>

static void* uf__create(size_t elem_size, size_t count)
{
	uf_header_t* uf_header = (uf_header_t*)malloc(sizeof(uf_header_t)+(count*2)*sizeof(UF_TYPE));
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
	uf_header->parent 	= ((UF_TYPE*)((uf_header) + 1));
	uf_header->rank		= ((UF_TYPE*)((uf_header) + 1) + count);
	return (void*)(uf_header + 1);
}

static void uf__free(uf_header_t* uf_head)
{
	//Single malloc with shadow header!
	free(uf_head);
}

uf_result_t uf__find(void* first, void* second, size_t key_off, size_t key_size, size_t count)
{
	if(key_size != sizeof(UF_TYPE)){
		printf("Type mismatching from uf elemnts key and base index type!");
		return ERRROR;
	}
	for(int i = 0; i < count; i++)
	{
		
	}
}


#endif
#endif
