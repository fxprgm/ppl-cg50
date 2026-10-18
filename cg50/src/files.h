/* Storage memory access (\\fls0\): BFile calls, each inside gint_world_switch(). */
#ifndef PPL_FILES_H
#define PPL_FILES_H

#include <stdint.h>

#define FILES_NAME_MAX 64

typedef struct {
	char name[FILES_NAME_MAX];   /* relative to \\fls0\, with the folder ("PPL\\FIB.ppl") */
	uint32_t size;
} files_entry_t;

/* The files (not folders) in the storage memory folder dir ("" = the root, "PPL\\" = a
   folder, with its backslash): up to max entries; returns how many. */
int files_list(const char *dir, files_entry_t *out, int max);

/* Reads the whole file (up to max bytes) into a new malloc'd buffer with a 0 after it.
   Returns the size, or a negative BFile error (-1000: no memory, -1001: too large). */
int files_read_all(const char *name, char **out, int max);

#endif
