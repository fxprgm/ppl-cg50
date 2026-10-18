/* The "ppl" arena: gint's allocator over 6 MB of free DRAM (cg50_mem.h). Allocations go there
 * first and fall back to gint's default arenas. */
#include "mem.h"
#include "cg50_mem.h"

#include <gint/kmalloc.h>
#include <string.h>

static kmalloc_arena_t arena;

void mem_init(void)
{
	arena.name = "ppl";
	arena.start = (void *)CG50_ARENA_ADDR;
	arena.end = (void *)CG50_ARENA_END;
	arena.is_default = 0;
	kmalloc_init_arena(&arena, true);
	kmalloc_add_arena(&arena);
}

void *cg_malloc(size_t n)
{
	void *p = kmalloc(n ? n : 1, "ppl");
	return p ? p : kmalloc(n ? n : 1, NULL);
}

void *cg_calloc(size_t n, size_t size)
{
	void *p = cg_malloc(n * size);
	if (p)
		memset(p, 0, n * size);
	return p;
}

void *cg_realloc(void *p, size_t n)
{
	if (!p)
		return cg_malloc(n);
	return krealloc(p, n ? n : 1);
}

void cg_free(void *p)
{
	kfree(p);
}

void mem_stats(uint32_t *used, uint32_t *peak, uint32_t *size)
{
	kmalloc_gint_stats_t *s = kmalloc_get_gint_stats(&arena);
	*used = s ? s->used_memory : 0;
	*peak = s ? s->peak_used_memory : 0;
	*size = CG50_ARENA_END - CG50_ARENA_ADDR;
}
