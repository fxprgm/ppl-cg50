/* Forced into every core file (CMakeLists.txt: -include): the core allocates from the
 * add-in's 6 MB DRAM arena (mem.c) instead of gint's small heap. */
#ifndef PPL_ALLOC_H
#define PPL_ALLOC_H

#include <stdlib.h>

void *cg_malloc(size_t n);
void *cg_calloc(size_t n, size_t size);
void *cg_realloc(void *p, size_t n);
void cg_free(void *p);

#define malloc(n) cg_malloc(n)
#define calloc(n, s) cg_calloc(n, s)
#define realloc(p, n) cg_realloc(p, n)
#define free(p) cg_free(p)

#endif
