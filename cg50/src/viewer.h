/* The source viewer: a program's text (UTF-8) with line numbers, colours for keywords, comments,
 * strings and numbers, wrap or sideways scrolling, in the viewer's font (Setup in config.h).
 * Keys: UP/DOWN a line (SHIFT first: a page), LEFT/RIGHT sideways (a page when wrapping),
 * F1/F2 or -/+ the size, F3 wrap, F4 line numbers, F5 bold, F6 top/end, EXIT back. */
#ifndef PPL_VIEWER_H
#define PPL_VIEWER_H

/* shows src (NUL-terminated) under title, line (1-based) as the current line */
void viewer_show(const char *title, const char *src, int line);

#endif
