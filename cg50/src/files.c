/* Storage memory access: every BFile call runs in the OS world (gint_world_switch), as BFile
 * only works there. Reads go through the uncached I/O buffer (cg50_mem.h): BFile may use DMA. */
#include "files.h"
#include "cg50_mem.h"

#include <gint/bfile.h>
#include <gint/gint.h>
#include <stdlib.h>
#include <string.h>

static void fc_path(uint16_t *dst, const char *name)
{
	static const char prefix[] = "\\\\fls0\\";
	int n = 0;
	for (const char *p = prefix; *p; p++)
		dst[n++] = (uint8_t)*p;
	for (const char *p = name; *p && n < FILES_NAME_MAX + 6; p++)
		dst[n++] = (uint8_t)*p;
	dst[n] = 0;
}

static void ascii_name(const uint16_t *fc, char *out)
{
	/* the found path may or may not carry the \\fls0\ prefix: keep the last component */
	const uint16_t *base = fc;
	for (const uint16_t *p = fc; *p; p++)
		if (*p == '\\')
			base = p + 1;
	int n = 0;
	for (const uint16_t *p = base; *p && n < FILES_NAME_MAX - 1; p++)
		out[n++] = *p < 0x80 ? (char)*p : '?';
	out[n] = 0;
}

/* ---- directory listing ------------------------------------------------------------------- */

static struct {
	const char *dir;
	files_entry_t *out;
	int max, n;
} ls;

static int list_worker(void)
{
	uint16_t pattern[FILES_NAME_MAX + 16], found[256];
	struct BFile_FileInfo fi;
	char name[FILES_NAME_MAX];
	int sh, n = 0;
	static const char prefix[] = "\\\\fls0\\";
	for (const char *p = prefix; *p; p++)
		pattern[n++] = (uint8_t)*p;
	for (const char *p = ls.dir; *p && n < FILES_NAME_MAX; p++)
		pattern[n++] = (uint8_t)*p;
	pattern[n++] = '*';
	pattern[n] = 0;
	int rc = BFile_FindFirst(pattern, &sh, found, &fi);
	if (rc < 0)
		return 0; /* nothing there, or no such folder */
	while (rc >= 0 && ls.n < ls.max) {
		if (fi.type != BFile_Type_Directory) {
			ascii_name(found, name);
			files_entry_t *e = &ls.out[ls.n];
			int dl = strlen(ls.dir);
			if (dl + strlen(name) < FILES_NAME_MAX) {
				strcpy(e->name, ls.dir);
				strcpy(e->name + dl, name);
				e->size = fi.file_size;
				ls.n++;
			}
		}
		rc = BFile_FindNext(sh, found, &fi);
	}
	BFile_FindClose(sh);
	return 0;
}

int files_list(const char *dir, files_entry_t *out, int max)
{
	ls.dir = dir, ls.out = out, ls.max = max, ls.n = 0;
	gint_world_switch(GINT_CALL(list_worker));
	return ls.n;
}

/* ---- whole-file reads --------------------------------------------------------------------- */

static struct {
	uint16_t path[FILES_NAME_MAX + 8];
	int h, size, pos;
} rq;

static int open_worker(void)
{
	rq.h = BFile_Open(rq.path, BFile_ReadOnly);
	if (rq.h < 0)
		return rq.h;
	rq.size = BFile_Size(rq.h);
	return 0;
}

static int read_worker(void)
{
	return BFile_Read(rq.h, (void *)CG50_IOBUF_ADDR, rq.size, rq.pos);
}

static int close_worker(void)
{
	return BFile_Close(rq.h);
}

int files_read_all(const char *name, char **out, int max)
{
	*out = NULL;
	fc_path(rq.path, name);
	int rc = gint_world_switch(GINT_CALL(open_worker));
	if (rc < 0)
		return rc;
	int size = rq.size;
	if (size < 0 || size > max) {
		gint_world_switch(GINT_CALL(close_worker));
		return size < 0 ? size : -1001;
	}
	char *buf = malloc(size + 2);
	if (!buf) {
		gint_world_switch(GINT_CALL(close_worker));
		return -1000;
	}
	int done = 0;
	while (done < size) {
		int chunk = size - done;
		if (chunk > (int)CG50_IOBUF_SIZE)
			chunk = CG50_IOBUF_SIZE;
		rq.size = chunk, rq.pos = done;
		rc = gint_world_switch(GINT_CALL(read_worker));
		if (rc <= 0)
			break;
		memcpy(buf + done, (void *)CG50_IOBUF_ADDR, rc);
		done += rc;
	}
	gint_world_switch(GINT_CALL(close_worker));
	if (done < size) {
		free(buf);
		return rc < 0 ? rc : -1;
	}
	buf[size] = buf[size + 1] = 0;
	*out = buf;
	return size;
}

/* ---- small whole-file writes -------------------------------------------------------------- */

static int write_worker(void)
{
	int size = rq.size, rc;
	BFile_Remove(rq.path);
	if ((rc = BFile_Create(rq.path, BFile_File, &size)) < 0)
		return rc;
	if ((rq.h = BFile_Open(rq.path, BFile_WriteOnly)) < 0)
		return rq.h;
	rc = BFile_Write(rq.h, (void *)CG50_IOBUF_ADDR, rq.size);
	BFile_Close(rq.h);
	return rc < 0 ? rc : 0;
}

int files_write(const char *name, const void *data, int size)
{
	if ((size & 1) || size < 0 || size > (int)CG50_IOBUF_SIZE)
		return -1001;
	fc_path(rq.path, name);
	memcpy((void *)CG50_IOBUF_ADDR, data, size);
	rq.size = size;
	return gint_world_switch(GINT_CALL(write_worker));
}
