/* The VM's hooks on the CG50 and the built-ins the add-in provides (GETKEY, ISKEYDOWN, WAIT,
 * MOUSE, INPUT, CHOOSE, MSGBOX, FREEZE): registered before the core's, so they replace the
 * desktop stubs of the same names. */
#ifndef PPL_HOST_H
#define PPL_HOST_H

#include "vm.h"

extern const builtin host_table[];
void host_hooks(ppl_vm *V);
void host_run_start(void);   /* a Home evaluation starts */

#endif
