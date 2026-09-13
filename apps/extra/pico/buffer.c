#include "pico.h"

int gap_text_len(PicoState *s)
{
    return BUF_SIZE - (s->gap.end - s->gap.start);
}

int gap_buf_idx(const PicoState *s, int li)
{
    return li < s->gap.start ? li : li + (s->gap.end - s->gap.start);
}

void gap_init(PicoState *s)
{
    s->gap.start = 0;
    s->gap.end = BUF_SIZE;
    s->cur.lines = 1;
    s->cur.row = 0;
    s->cur.col = 0;
    s->flags.truncated = 0;
}

int gap_line_range(PicoState *s, int row, int *start, int *end)
{
    int tl = gap_text_len(s);
    int count = 0, line_start = 0;
    for (int li = 0; li <= tl; li++)
    {
        int is_nl = (li < tl) && (s->gap.data[gap_buf_idx(s, li)] == '\n');
        if (is_nl || li == tl)
        {
            if (count == row)
            {
                *start = line_start;
                *end = li;
                return 1;
            }
            count++;
            line_start = li + 1;
        }
    }
    return 0;
}

int gap_get_line(PicoState *s, int row, char *out, int maxlen)
{
    int start, end;
    if (!gap_line_range(s, row, &start, &end))
    {
        out[0] = '\0';
        return 0;
    }
    int len = end - start;
    if (len > maxlen - 1)
        len = maxlen - 1;
    for (int j = 0; j < len; j++)
        out[j] = s->gap.data[gap_buf_idx(s, start + j)];
    out[len] = '\0';
    return len;
}

void gap_recount_lines(PicoState *s)
{
    int tl = gap_text_len(s);
    int count = 1;
    for (int li = 0; li < tl; li++)
    {
        if (s->gap.data[gap_buf_idx(s, li)] == '\n')
            count++;
    }
    s->cur.lines = count;
}

void gap_update_cursor(PicoState *s)
{
    int tl = gap_text_len(s);
    int count = 0, line_start = 0;
    for (int li = 0; li <= tl; li++)
    {
        int is_nl = (li < tl) && (s->gap.data[gap_buf_idx(s, li)] == '\n');
        if (is_nl || li == tl)
        {
            if (li >= s->gap.start)
            {
                s->cur.row = count;
                s->cur.col = s->gap.start - line_start;
                return;
            }
            count++;
            line_start = li + 1;
        }
    }
    s->cur.row = s->cur.lines - 1;
    s->cur.col = s->gap.start - line_start;
}

void gap_move_to(PicoState *s, int target)
{
    if (target < 0)
        target = 0;
    if (target > gap_text_len(s))
        target = gap_text_len(s);

    if (target < s->gap.start)
    {
        int n = s->gap.start - target;
        memmove(s->gap.data + s->gap.end - n, s->gap.data + target, n);
        s->gap.start -= n;
        s->gap.end -= n;
    }
    else if (target > s->gap.start)
    {
        int n = target - s->gap.start;
        memmove(s->gap.data + s->gap.start, s->gap.data + s->gap.end, n);
        s->gap.start += n;
        s->gap.end += n;
    }
}
