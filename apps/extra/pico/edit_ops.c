#include "pico.h"

int edit_mark_modified(PicoState *s)
{
    if (!s->flags.file_modified)
    {
        s->flags.file_modified = 1;
        s->flags.scr_dirty = 1;
        return 1;
    }
    return 0;
}

static void cursor_up_line(PicoState *s)
{
    s->cur.row--;
    int i = s->gap.start;
    while (i > 0 && s->gap.data[i - 1] != '\n')
        i--;
    s->cur.col = s->gap.start - i;
}

void edit_insert_char(PicoState *s, char c)
{
    if (s->gap.start >= s->gap.end)
        return;
    s->gap.data[s->gap.start++] = c;
    if (c == '\n')
    {
        s->cur.lines++;
        s->cur.row++;
        s->cur.col = 0;
    }
    else
    {
        s->cur.col++;
    }
    int mod = edit_mark_modified(s);
    if (mod)
        scr_render(s);
    else
        scr_redraw_line(s, s->cur.row);
}

void edit_backspace(PicoState *s)
{
    if (s->gap.start == 0)
        return;
    char c = s->gap.data[s->gap.start - 1];
    s->gap.start--;
    if (c == '\n')
    {
        s->cur.lines--;
        cursor_up_line(s);
    }
    else if (s->cur.col > 0)
    {
        s->cur.col--;
    }
    int mod = edit_mark_modified(s);
    if (mod)
    {
        scr_render(s);
    }
    else if (c == '\n')
    {
        scr_render(s);
    }
    else
    {
        scr_redraw_line(s, s->cur.row);
    }
}

void edit_cursor_left(PicoState *s)
{
    if (s->gap.start == 0)
        return;
    char c = s->gap.data[s->gap.start - 1];
    s->gap.start--;
    s->gap.end--;
    s->gap.data[s->gap.end] = c;
    if (c == '\n')
    {
        cursor_up_line(s);
    }
    else if (s->cur.col > 0)
    {
        s->cur.col--;
    }
}

void edit_cursor_right(PicoState *s)
{
    int tl = gap_text_len(s);
    if (s->gap.end >= BUF_SIZE || s->gap.start >= tl)
        return;
    char c = s->gap.data[s->gap.end];
    s->gap.data[s->gap.start] = c;
    s->gap.start++;
    s->gap.end++;
    if (c == '\n')
    {
        s->cur.row++;
        s->cur.col = 0;
    }
    else
    {
        s->cur.col++;
    }
}

void edit_cursor_up(PicoState *s)
{
    if (s->cur.row == 0)
        return;
    int save_col = s->cur.col;
    int prev_start, prev_end;
    gap_line_range(s, s->cur.row - 1, &prev_start, &prev_end);
    int prev_len = prev_end - prev_start;
    int new_col = save_col < prev_len ? save_col : prev_len;
    gap_move_to(s, prev_start + new_col);
    s->cur.row--;
    s->cur.col = new_col;
    scr_ensure_visible(s);
    scr_move_cursor(s);
}

void edit_cursor_down(PicoState *s)
{
    if (s->cur.row >= s->cur.lines - 1)
        return;
    int save_col = s->cur.col;
    int next_start, next_end;
    if (!gap_line_range(s, s->cur.row + 1, &next_start, &next_end))
        return;
    int next_len = next_end - next_start;
    int new_col = save_col < next_len ? save_col : next_len;
    gap_move_to(s, next_start + new_col);
    s->cur.row++;
    s->cur.col = new_col;
    scr_ensure_visible(s);
    scr_move_cursor(s);
}
