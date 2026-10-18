/* HP PPL for the fx-CG50: the add-in around the PPL core (src/). GPL-3.0-or-later.
 *
 * Memory: the core allocates from a 6 MB kmalloc arena in free DRAM (mem.c, cg50_mem.h).
 * Start-up: the stats block, the arena, the clock, the add-in's built-ins (host.c, looked up
 * before the core's), the VM and its hooks, G0 (gfx_init), then Home (home.c). */
#include "cg50_mem.h"
#include "clock.h"
#include "gfx.h"
#include "home.h"
#include "host.h"
#include "mem.h"
#include "progs.h"
#include "screen.h"
#include "stats.h"
#include "ui.h"

#include <stdlib.h>
#include <string.h>

void st_str(volatile char *dst, const char *src)
{
	int i = 0;
	for (; i < 63 && src && src[i]; i++)
		dst[i] = src[i];
	for (; i < 64; i++)
		dst[i] = 0;
}

int main(void)
{
	memset((void *)ST, 0, sizeof *ST);
	ST->magic = 0x50504C31; /* PPL1 */
	ST->phase = PH_START;
	mem_init();
	clock_init();
	bi_set_host_table(host_table);

	ppl_vm *V = malloc(sizeof *V);
	if (!V) {
		ui_message("HP PPL", "Not enough memory for the VM.", "Press any key", C_ERR);
		ui_key();
		return 1;
	}
	ppl_init(V);
	host_hooks(V);
	ST->vm_bytes = sizeof *V;
	uint32_t used, peak, size;
	mem_stats(&used, &peak, &size);
	ST->arena_used = used, ST->arena_peak = peak, ST->arena_size = size;
	gfx_init(V);
	g0_set_vm(V);
	progs_init(V);
	home_init(V);
	/* the programs in \\fls0\PPL\ are there from the start, as on the Prime */
	char loaded[400];
	bool err;
	progs_autoload(loaded, sizeof loaded, &err);
	if (loaded[0])
		home_note("PPL folder:", loaded, err);
	home_loop();
	clock_quit();
	return 0;
}
