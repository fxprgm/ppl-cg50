/* The PRINT terminal: the lines programs print, shown full screen while a program prints and
 * after it ends (until a key), scrollable with UP/DOWN. */
#ifndef PPL_TERM_H
#define PPL_TERM_H

#include <stdbool.h>

void term_print(const char *utf8);   /* NULL: PRINT() clears */
void term_clear(void);
int term_count(void);
/* switch the LCD to the terminal and draw it (scroll: rows up from the bottom) */
void term_show(int scroll);
/* redraw if lines arrived since the last draw, at most every `every` ms */
void term_flush(int every);
/* the terminal as a screen of its own: UP/DOWN scroll; returns the key that closed it */
int term_view(const char *footer);

#endif
