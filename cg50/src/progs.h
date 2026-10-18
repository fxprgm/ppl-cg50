/* Program files: the picker (.hpprgm, .ppl, .txt in \\fls0\ and \\fls0\PPL\), loading
 * (compiling) them, and the source text kept for error screens. */
#ifndef PPL_PROGS_H
#define PPL_PROGS_H

#include <stdbool.h>
#include "vm.h"

void progs_init(ppl_vm *V);
/* at start: load every program file in \\fls0\PPL\ quietly; a summary into out */
void progs_autoload(char *out, int n, bool *any_error);
/* the file picker; returns after a load (or EXIT) */
void progs_picker(void);
/* load storage-memory file path (relative to \\fls0\); 0 or an E_* code (screens shown) */
int progs_load(const char *path, bool quiet);
/* line `line` (1-based) of program `name`'s source, or "" */
void progs_line(const char *name, int line, char *out, int n);
/* the exported functions list; returns 1 and fills `call` ("NAME(" or "NAME()") if one was
   picked, with *run = 1 when it takes no arguments */
int progs_functions(char *call, int n, int *run);

#endif
