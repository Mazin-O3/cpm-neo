#ifndef ED_H
#define ED_H

#include <cpm.h>

#define EDIT_BUF_SIZE 4096
#define ED_MAX_LINES 2048
#define LINE_LEN 128

_Static_assert(EDIT_BUF_SIZE <= UINT16_MAX, "line_off[] entries are uint16_t");

typedef struct
{
    char     buf[EDIT_BUF_SIZE];
    uint16_t line_off[ED_MAX_LINES];
    int      num_lines;
    int      logical_bytes;
    int      gap_start;
    int      gap_end;
    int      cur;
    int      modified;
    int      verify;
    int      readonly;
    char     name[ARG_LEN_MAX];
} Editor;

static inline int log_to_phys(Editor *e, int log_off)
{
    if (log_off < e->gap_start)
        return log_off;
    else
        return log_off + (e->gap_end - e->gap_start);
}

int  ed_load(Editor *e, const char *path);
int  ed_save(Editor *e);
void ed_insert(Editor *e, int line);
void ed_delete(Editor *e, int from, int to);
void ed_list(Editor *e, int from, int to);
void ed_subst(Editor *e, int line, const char *old, const char *new_s);
int  ed_read(Editor *e, int line, const char *path);
void gap_move(Editor *e, int log_pos);

/* Insert text as line number `at` (later lines shift down). The NUL
 * terminator is stored with the text. Returns 0, or -1 if the buffer
 * is full (state unchanged apart from the gap position). */
int ed_put_line(Editor *e, int at, const char *text, int len);

#endif
