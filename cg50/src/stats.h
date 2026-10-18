/* The stats block: big-endian words at physical 0x0C1E0000 (CG50_STATS_ADDR), read by the
   casio-cg50 probe test (emu_go/ppl_probe_test.go keeps the word offsets). Append only. */
#ifndef PPL_STATS_H
#define PPL_STATS_H

#include <stdint.h>
#include "cg50_mem.h"

typedef struct {
	uint32_t magic;        /* 'PPL1' once valid */
	uint32_t phase;        /* PH_* */
	uint32_t runs;         /* Home evaluations finished */
	uint32_t last_err;     /* E_* of the last evaluation (0 = none) */
	uint32_t last_errline;
	uint32_t arena_used, arena_peak, arena_size;
	uint32_t keyev;        /* gint key events seen while programs ran */
	uint32_t view;         /* VIEW_* */
	uint32_t ptr_mode, ptr_x, ptr_y;
	uint32_t run_ms;       /* the last evaluation, real ms (TMU) */
	uint32_t files;        /* program files the last picker listed */
	uint32_t progs;        /* programs loaded */
	uint32_t polls;        /* VM polls */
	uint32_t presents;     /* G0 rows pushed to the LCD (count of pushes) */
	uint32_t load_err;     /* last program load: 0 ok, else E_* */
	uint32_t load_line;
	uint32_t vm_bytes;     /* sizeof(ppl_vm) */
	uint32_t reserved[3];
	char last_in[64];      /* the last Home input (UTF-8) */
	char last_out[64];     /* its result, or the error message */
	char msg[64];          /* last message (loads, errors) */
} ppl_stats_t;

enum { PH_START, PH_HOME, PH_RUN, PH_INPUT, PH_CHOOSE, PH_MSGBOX, PH_PICK, PH_ERROR, PH_TERM,
       PH_FREEZE, PH_FUNCS, PH_CHARS, PH_SETUP, PH_HELP, PH_WAIT, PH_QUIT };

#define ST ((volatile ppl_stats_t *)CG50_STATS_ADDR)

void st_str(volatile char *dst, const char *src);   /* 63 chars at most, 0-filled */

#endif
