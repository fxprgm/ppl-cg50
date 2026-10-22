/* Settings (config.h). */
#include "config.h"
#include "files.h"
#include "text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG "PPL.cfg"

config_t cfg;

static const struct { const char *key; int *i; bool *b; } items[] = {
	{ "home_font", &cfg.home_font, NULL }, { "home_bold", NULL, &cfg.home_bold },
	{ "view_font", &cfg.view_font, NULL }, { "view_bold", NULL, &cfg.view_bold },
	{ "view_numbers", NULL, &cfg.view_numbers }, { "view_wrap", NULL, &cfg.view_wrap },
};
#define NITEMS (int)(sizeof items / sizeof items[0])

void config_load(void)
{
	cfg.home_font = cfg.view_font = FONT_SIZE_DEFAULT;
	cfg.home_bold = cfg.view_bold = cfg.view_wrap = false;
	cfg.view_numbers = true;
	char *buf;
	if (files_read_all(CONFIG, &buf, 4096) < 0)
		return;
	for (char *line = buf; line && *line;) {
		char *nl = strchr(line, '\n');
		if (nl)
			*nl = 0;
		char *eq = strchr(line, '=');
		if (eq) {
			*eq = 0;
			int v = atoi(eq + 1);
			for (int i = 0; i < NITEMS; i++)
				if (!strcmp(line, items[i].key)) {
					if (items[i].i) *items[i].i = v;
					else *items[i].b = v != 0;
				}
		}
		line = nl ? nl + 1 : NULL;
	}
	free(buf);
	if (cfg.home_font < 0 || cfg.home_font >= FONT_SIZES) cfg.home_font = FONT_SIZE_DEFAULT;
	if (cfg.view_font < 0 || cfg.view_font >= FONT_SIZES) cfg.view_font = FONT_SIZE_DEFAULT;
}

void config_save(void)
{
	char t[256];
	int k = 0;
	for (int i = 0; i < NITEMS; i++)
		k += snprintf(t + k, sizeof t - k, "%s=%d\n", items[i].key,
		              items[i].i ? *items[i].i : (int)*items[i].b);
	if (k & 1)
		t[k++] = '\n';      /* BFile writes must be of even size */
	files_write(CONFIG, t, k);
}
