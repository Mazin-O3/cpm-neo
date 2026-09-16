#include "ed.h"

int log_to_phys(Editor *e, int log_off)
{
    if (log_off < e->gap_start)
        return log_off;
    return log_off + (e->gap_end - e->gap_start);
}

/* Gap buffer operations */

void gap_move(Editor *e, int log_pos)
{
    if (log_pos < 0)
        log_pos = 0;

    if (log_pos > e->logical_bytes)
        log_pos = e->logical_bytes;

    int old_gs = e->gap_start;

    int old_ge = e->gap_end;

    if (log_pos < old_gs)
    {
        int move_size = old_gs - log_pos;
        memmove(e->buf + old_ge - move_size, e->buf + log_pos, move_size);

        e->gap_start = log_pos;
        e->gap_end = old_ge - move_size;
    }
    else if (log_pos > old_gs)
    {
        int move_size = log_pos - old_gs;
        memmove(e->buf + old_gs, e->buf + old_ge, move_size);

        e->gap_start = log_pos;
        e->gap_end = old_ge + move_size;
    }
}

/* Line operations */

int ed_put_line(Editor *e, int at, const char *text, int len)
{
    if (at < 0)
        at = 0;

    if (at > e->num_lines)
        at = e->num_lines;

    int insert_pos = (at >= e->num_lines) ? e->logical_bytes : e->line_off[at];

    gap_move(e, insert_pos);

    if (e->gap_end - e->gap_start < len + 1)
        return ENOSPC;

    memcpy(e->buf + e->gap_start, text, len);

    e->buf[e->gap_start + len] = 0;
    e->gap_start += len + 1;
    e->logical_bytes += len + 1;

    for (int j = e->num_lines; j > at; j--)
        e->line_off[j] = e->line_off[j - 1] + len + 1;

    e->line_off[at] = insert_pos;
    e->num_lines++;

    return EOK;
}

/* User interaction */

void ed_insert(Editor *e, int line)
{
    if (line < 0 || line > e->num_lines)
        line = e->num_lines;

    char in[LINE_LEN];

    int insert_line = line;

    for (;;)
    {
        if (e->verify)
            printf("     %d: ", insert_line + 1);
        else
            printf("     : ");

        int i = 0, c;

        while ((c = getchar()) != '\n' && c != '\r' && c != CH_EOF && c >= 0)
        {
            if ((c == '\b' || c == 0x7F) && i > 0)
            {
                i--;
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
            else if (i < LINE_LEN - 1)
            {
                in[i++] = c;
                putchar(c);
            }
        }

        in[i] = 0;
        putchar('\n');

        int stop = (c == CH_EOF);

        if (i == 0 && stop)
            break;

        if (ed_put_line(e, insert_line, in, i) != EOK)
        {
            printf("?FULL\n");
            break;
        }

        e->modified = 1;
        insert_line++;

        if (stop)
            break;
    }

    e->cur = -1;
}

/* Line deletion */

void ed_delete(Editor *e, int from, int to)
{
    if (from < 0)
        from = 0;

    if (to >= e->num_lines)
        to = e->num_lines - 1;

    if (from > to)
    {
        printf("?RANGE\n");
        return;
    }

    int from_log = e->line_off[from];
    int to_log = (to + 1 < e->num_lines) ? e->line_off[to + 1] : e->logical_bytes;
    int del_len = to_log - from_log;

    gap_move(e, from_log);

    e->gap_end += del_len;
    e->logical_bytes -= del_len;

    int count = to - from + 1;

    for (int i = from; i < e->num_lines - count; i++)
        e->line_off[i] = e->line_off[i + count] - del_len;

    e->num_lines -= count;

    if (e->cur >= from)
    {
        if (e->cur <= to)
            e->cur = from - 1;
        else
            e->cur -= count;
    }

    e->modified = 1;
}
