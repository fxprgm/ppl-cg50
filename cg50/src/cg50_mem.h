/* The add-in's DRAM layout. Free DRAM while an add-in runs: physical 0x0C1E0000-0x0C7FFFFF,
 * used through the cached P1 view 0x8C1E0000 (gint 2.11's own memory ends below it; the
 * casio-cg50 wiki, memory-map.md). The OS zero-fills 0x0C1E0000-0x0C4DFFFF at every add-in
 * launch, so nothing in it survives a quit.
 *
 *   0x8C1E0000  stats block (ppl_stats_t, stats.h), read by the casio-cg50 probe test
 *   0x8C1F0000  file I/O buffer, 64 KB, used only through its uncached P2 view 0xAC1F0000
 *               (BFile may use DMA)
 *   0x8C200000  the "ppl" kmalloc arena, 6 MB: the VM, its values, programs, grobs G0-G9
 *   0x8C800000  end of DRAM
 */
#ifndef PPL_CG50_MEM_H
#define PPL_CG50_MEM_H

#define CG50_STATS_ADDR   0x8C1E0000u
#define CG50_IOBUF_ADDR   0xAC1F0000u   /* P2, uncached */
#define CG50_IOBUF_SIZE   0x00010000u
#define CG50_ARENA_ADDR   0x8C200000u
#define CG50_ARENA_END    0x8C800000u

#endif
