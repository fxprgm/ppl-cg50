/* The add-in's settings, kept in \\fls0\PPL.cfg (text, one "key=value" per line). */
#ifndef PPL_CONFIG_H
#define PPL_CONFIG_H

#include <stdbool.h>

typedef struct {
	int home_font;           /* font_sizes[] index for Home's history and command line */
	bool home_bold;
	int view_font;           /* the source viewer's */
	bool view_bold, view_numbers, view_wrap;
} config_t;

extern config_t cfg;

void config_load(void);      /* defaults, then the file's values if it is there */
void config_save(void);      /* errors are ignored: the settings stay for this session */

#endif
