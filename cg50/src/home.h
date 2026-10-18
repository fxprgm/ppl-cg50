/* Home: the command line and its history (input left, result right, as on the Prime), the
 * soft-key menus (program files, exported functions, characters, setup), running what is
 * typed, and the screens a run ends with (error, terminal, frozen G0). */
#ifndef PPL_HOME_H
#define PPL_HOME_H

#include <stdbool.h>
#include "vm.h"

void home_init(ppl_vm *V);
void home_loop(void);
/* redraw what the LCD should show now (after a dialog) */
void home_restore(void);
/* the program ran FREEZE: keep G0 on the screen after it ends, until a key */
extern bool home_freeze;

#endif
