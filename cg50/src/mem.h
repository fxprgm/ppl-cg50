/* The VM's memory: a kmalloc arena in the free DRAM (cg50_mem.h). The core is compiled with
 * -include ppl_alloc.h, so its malloc/calloc/realloc/free come here. */
#ifndef PPL_MEM_H
#define PPL_MEM_H

#include <stddef.h>
#include <stdint.h>

void mem_init(void);
/* bytes in use and the peak, in the arena */
void mem_stats(uint32_t *used, uint32_t *peak, uint32_t *size);

#endif
