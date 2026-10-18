/* The LCD: the Prime's 320x240 G0 in the CG50's 396x224, a status panel in the 76 spare
 * columns, and pushes of only the rows that changed.
 *
 * Views (240 Prime rows into 224 display rows):
 *   FIT     every group of 15 rows -> 14 display rows, the last two of the group averaged (a
 *           1-pixel line there is drawn at half intensity instead of vanishing)
 *   TOP     rows 0-223, 1:1        BOTTOM  rows 16-239, 1:1 (the soft menu stays visible)
 */
#ifndef PPL_SCREEN_H
#define PPL_SCREEN_H

#include <stdbool.h>
#include "vm.h"

enum { VIEW_FIT, VIEW_TOP, VIEW_BOTTOM, VIEW_COUNT };
extern int view;
extern const char *const view_names[VIEW_COUNT];

/* what the LCD shows */
enum { DISP_HOME, DISP_G0, DISP_TERM };
extern int disp;

/* send display rows y0..y1 (widened to groups of 4 rows: see the gotcha in screen.c) */
void scr_push(int y0, int y1);
void scr_push_all(void);

void g0_set_vm(ppl_vm *V);
/* rows y0..y1 of G0 changed (the present hook) */
void g0_mark(int y0, int y1);
/* convert and push what changed, if the G0 view is up; at most every `every` ms unless
   every == 0 */
void g0_flush(int every);
/* switch the LCD to the G0 view: everything converted and pushed */
void g0_show(void);
/* G0 rows shown on display row d: *a, and *b (averaged) or -1; the display row of Prime row y */
void g0_sources(int d, int *a, int *b);
int g0_display_row(int y);

/* the status panel at x 320-395 (G0 view only): redrawn and pushed if its text changed */
void panel_update(void);
/* the panel's state line */
extern const char *panel_state;
extern const char *panel_what;

#endif
