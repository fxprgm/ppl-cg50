/* The modal screens programs open: INPUT, CHOOSE, MSGBOX; and the error screen. They draw
 * over whatever the LCD shows and restore it afterwards (home_restore). AC/ON in them breaks
 * the program (E_BREAK). */
#ifndef PPL_DIALOGS_H
#define PPL_DIALOGS_H

#include "vm.h"

int dlg_input(ppl_vm *V, int argc, value *a, value *out);
int dlg_choose(ppl_vm *V, int argc, value *a, value *out);
int dlg_msgbox(ppl_vm *V, int argc, value *a, value *out);

/* the text of a value as Home shows it (UTF-8, cut to n-1 bytes) */
void value_text(ppl_vm *V, const value *v, char *out, int n, int quote);

#endif
