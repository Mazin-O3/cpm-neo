#ifndef PICO_H
#define PICO_H

#include <stdarg.h>
#include <ansi.h>
#include "fs.h"
#include "stdio.h"
#include "string.h"
#include "ctype.h"
#include "errno.h"
#include "stdlib.h"
#include "syscall.h"

#define BUF_SIZE 16384
#define PICO_ROWS (SCREEN_ROWS - 3)
#define BATCH_LEN 3072
#define MAX_VISIBLE_LINE 256

/* pico's assumed console geometry */
#define SCREEN_WIDTH 80
#define SCREEN_ROWS  24


/* Key codes */
#define KEY_ESC    0x1B
#define KEY_CTRL_O 0x0F
#define KEY_CTRL_S 0x13
#define KEY_CTRL_Q 0x11
#define KEY_BS     0x08
#define KEY_DEL    0x7F

typedef struct
{
    char data[BUF_SIZE];
    int start, end;
} PicoGap;

typedef struct
{
    int row, col, lines, top;
} PicoCur;

typedef struct
{
    char name[ARG_LEN_MAX], orig[ARG_LEN_MAX];
    int is_default, readonly;
} PicoFile;

typedef struct
{
    int file_modified, truncated, scr_dirty;
} PicoFlags;

typedef struct
{
    char buf[BATCH_LEN];
    int pos;
} PicoAnsi;

typedef struct
{
    PicoGap gap;
    PicoCur cur;
    PicoFile file;
    PicoFlags flags;
    PicoAnsi ansi;
} PicoState;

/* Utilities */
void file_prompt_error (PicoState *s, const char *msg);
void wait_key          (void);

/* Gap buffer */
int  gap_text_len      (PicoState *s);
void gap_init          (PicoState *s);
int  gap_line_range    (PicoState *s, int row, int *start, int *end);
int  gap_get_line      (PicoState *s, int row, char *out, int maxlen);
void gap_recount_lines (PicoState *s);
void gap_update_cursor (PicoState *s);
void gap_move_to       (PicoState *s, int target);
int  gap_buf_idx       (const PicoState *s, int li);

/* Screen rendering */
void scr_render            (PicoState *s);
void scr_redraw_line       (PicoState *s, int row);
void scr_ensure_visible    (PicoState *s);
void scr_batch_start       (PicoState *s);
void scr_batch_printf      (PicoState *s, const char *fmt, ...);
void scr_batch_flush       (PicoState *s);
void scr_batch_status      (PicoState *s);
void scr_invalidate_status (void);
void scr_batch_draw_banner (PicoState *s);
void scr_move_cursor       (PicoState *s);
int  scr_row               (PicoState *s);
int  scr_col               (PicoState *s);

/* File I/O */
int  file_load              (PicoState *s, const char *path);
int  file_save              (PicoState *s, const char *path);
void file_report_failure    (PicoState *s, int rc);
int  file_maybe_save        (PicoState *s);
void file_truncate_83       (char *name);
int  file_prompt            (PicoState *s, const char *prompt);

/* Edit operations */
void edit_insert_char   (PicoState *s, char c);
void edit_backspace     (PicoState *s);
void edit_cursor_left   (PicoState *s);
void edit_cursor_right  (PicoState *s);
void edit_cursor_up     (PicoState *s);
void edit_cursor_down   (PicoState *s);
int  edit_mark_modified (PicoState *s);

#endif
