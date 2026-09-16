/*
 * apps/extra/ed/ed.h — Line editor
 *
 * Buffer layout (gap buffer + line offsets), editor state, and the public
 * load/save/edit operation API.
 */

#ifndef ED_H
#define ED_H

#include <cpmneo.h>

#define EDIT_BUF_SIZE 4096
#define ED_MAX_LINES  2048
#define LINE_LEN      128

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

int  log_to_phys(Editor *e, int log_off);
int  ed_load(Editor *e, const char *path);
int  ed_save(Editor *e);
int  ed_read(Editor *e, int line, const char *path);
int  ed_put_line(Editor *e, int at, const char *text, int len);
void gap_move(Editor *e, int log_pos);
void ed_insert(Editor *e, int line);
void ed_delete(Editor *e, int from, int to);
void ed_list(Editor *e, int from, int to);
int  ed_subst(Editor *e, int line, const char *old, const char *new_s);

#endif