#include "ed.h"

/* String search */

static char *ed_find_str(const char *s, const char *p)
{
    if (!*p)
        return (char *)s;

    int plen = strlen(p);

    for (; *s; s++)

        if (*s == *p && strncmp(s, p, plen) == 0)
            return (char *)s;

    return 0;
}

/* Line display */

void ed_list(Editor *e, int from, int to)
{
    if (from < 0)
        from = 0;

    if (to >= e->num_lines)
        to = e->num_lines - 1;

    if (from > to)
    {
        printf("END OF FILE\n");
        return;
    }

    for (int i = from; i <= to; i++)
    {
        if (e->verify)
            printf("     %d: %s\n", i + 1, e->buf + log_to_phys(e, e->line_off[i]));
        else
            printf("     %s\n", e->buf + log_to_phys(e, e->line_off[i]));
    }
}

/* Text substitution */

int ed_subst(Editor *e, int line, const char *old, const char *new_s)
{
    if (line < 0 || line >= e->num_lines)
    {
        printf("?RANGE\n");
        return EINVAL;
    }

    int line_log = e->line_off[line];
    int phys_off = log_to_phys(e, line_log);
    int line_len = strlen(e->buf + phys_off);

    char *p = ed_find_str(e->buf + phys_off, old);

    if (!p)
    {
        printf("?NOT FOUND\n");
        return ENOENT;
    }

    int olen = strlen(old);
    int nlen = strlen(new_s);
    int diff = nlen - olen;
    int match_off = p - (e->buf + phys_off);

    gap_move(e, line_log + line_len + 1);

    phys_off = log_to_phys(e, line_log);

    int tail_phys = phys_off + match_off + olen;
    int tail_len = line_len - match_off - olen + 1;

    if (diff > 0 && e->gap_end - e->gap_start < diff)
    {
        printf("?LONG\n");
        return EINVAL;
    }

    memmove(e->buf + tail_phys + diff, e->buf + tail_phys, tail_len);
    memcpy(e->buf + phys_off + match_off, new_s, nlen);

    e->gap_start += diff;

    e->logical_bytes += diff;

    for (int i = line + 1; i < e->num_lines; i++)
        e->line_off[i] += diff;

    e->modified = 1;

    return EOK;
}
