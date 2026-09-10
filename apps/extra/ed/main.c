#include "ed.h"

static Editor e;

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        printf("Usage: ED <file>\n");
        return 0;
    }

    memset(&e, 0, sizeof(e));
    e.cur = -1;
    e.verify = 1;

    ed_load(&e, argv[1]);

    if (e.num_lines == 0)
        printf("NEW FILE\n");

    for (;;)
    {
        if (e.cur < 0)
            printf("     : *");
        else
            printf("    %d: *", e.cur + 1);

        char cmd[80];
        int  cmdlen = getline(cmd, sizeof(cmd));

        if (cmdlen < 0)
            continue;

        int i = 0, c = (unsigned char)cmd[i++];

        if (c == 0)
        {
            int next = e.cur + 1;

            if (next >= e.num_lines)
                printf("END OF FILE\n");
            else
            {
                e.cur = next;
                printf("     %d: %s\n", next + 1, e.buf + log_to_phys(&e, e.line_off[next]));
            }

            continue;
        }

        int has_from = 0, from = 0;
        int neg = 0;

        if (c == '-')
        {
            neg = 1;
            c = (unsigned char)cmd[i++];
        }

        while (isdigit(c))
        {
            has_from = 1;
            from = from * 10 + (c - '0');
            c = (unsigned char)cmd[i++];
        }

        if (neg)
            from = -from;

        int has_comma = 0, has_to = 0, to = 0;

        if (c == ',')
        {
            has_comma = 1;
            c = (unsigned char)cmd[i++];

            while (isdigit(c))
            {
                has_to = 1;
                to = to * 10 + (c - '0');
                c = (unsigned char)cmd[i++];
            }
        }

        c = toupper(c);

        switch (c)
        {
        case 'B':
            if (cmd[i])
            {
                printf("?\n");
                break;
            }

            e.cur = -1;
            break;

        case 'L':
        {
            if (cmd[i])
            {
                printf("?\n");
                break;
            }

            int f, t;

            if (has_comma)
            {
                f = has_from ? from - 1 : 0;
                t = has_to ? to - 1 : e.num_lines - 1;
            }
            else if (has_from)
            {
                if (from >= 0)
                {
                    f = (e.cur < 0) ? 0 : e.cur;
                    t = f + from - 1;
                }
                else
                {
                    int cur = (e.cur < 0) ? 0 : e.cur;
                    f = cur + from;

                    if (f < 0)
                        f = 0;
                    t = cur - 1;
                }

                if (t >= e.num_lines)
                    t = e.num_lines - 1;

                if (f > t)
                {
                    printf("?\n");
                    break;
                }
            }
            else
            {
                f = (e.cur < 0) ? 0 : e.cur;
                t = f + 9;
            }

            ed_list(&e, f, t);
            break;
        }

        case 'D':
        {
            if (e.readonly)
            {
                printf("** FILE IS READ/ONLY **\n");
                break;
            }

            if (cmd[i])
            {
                printf("?\n");
                break;
            }

            if (has_comma)
            {
                int f = has_from ? from - 1 : 0;
                int t = has_to ? to - 1 : e.num_lines - 1;
                ed_delete(&e, f, t);
            }
            else if (has_from)
                ed_delete(&e, from - 1, from - 1);
            else
            {
                if (e.cur < 0 || e.cur >= e.num_lines)
                    printf("?RANGE\n");
                else
                    ed_delete(&e, e.cur, e.cur);
            }

            break;
        }

        case 'I':
            if (e.readonly)
            {
                printf("** FILE IS READ/ONLY **\n");
                break;
            }

            if (cmd[i])
            {
                printf("?\n");
                break;
            }

            ed_insert(&e, has_from ? from - 1 : e.cur + 1);
            break;

        case 'S':
        {
            if (e.readonly)
            {
                printf("** FILE IS READ/ONLY **\n");
                break;
            }

            int sep = (unsigned char)cmd[i++];

            if (sep != '/')
            {
                printf("?\n");
                break;
            }

            char  old[64], new_s[64];
            char *p = strchr(cmd + i, sep);

            if (!p || p - (cmd + i) >= (int)sizeof(old))
            {
                printf("?\n");
                break;
            }

            int len = p - (cmd + i);
            memcpy(old, cmd + i, len);
            old[len] = 0;
            i += len + 1;
            p = strchr(cmd + i, sep);

            if (!p || p - (cmd + i) >= (int)sizeof(new_s))
            {
                printf("?\n");
                break;
            }

            len = p - (cmd + i);
            memcpy(new_s, cmd + i, len);
            new_s[len] = 0;
            i += len + 1;

            if (cmd[i])
            {
                printf("?\n");
                break;
            }

            ed_subst(&e, has_from ? from - 1 : e.cur, old, new_s);
            break;
        }

        case '#':
            if (cmd[i])
            {
                printf("?\n");
                break;
            }

            e.verify = !e.verify;
            break;

        case 'H':
            if (cmd[i])
            {
                printf("?\n");
                break;
            }
            /* Save to disk, then resume editing at the top of the
             * (now clean) buffer. Never just drop the flag — That
             * lost all unsaved work on a following Q. */

            if (ed_save(&e) != 0)
                break;
            e.cur = -1;
            break;

        case 'R':
        {
            if (e.readonly)
            {
                printf("** FILE IS READ/ONLY **\n");
                break;
            }

            char fname[ARG_LEN_MAX];
            strncpy(fname, cmd + i, sizeof(fname) - 1);
            fname[sizeof(fname) - 1] = 0;

            if (fname[0])
                ed_read(&e, has_from ? from - 1 : e.cur + 1, fname);
            break;
        }

        case 'E':
            if (cmd[i])
            {
                printf("?\n");
                break;
            }

            if (ed_save(&e) == 0)
                return 0;
            break;

        case 'Q':
            if (cmd[i])
            {
                printf("?\n");
                break;
            }

            if (e.modified)
            {
                printf("Q-(Y/N)?");
                int a = getchar();
                putchar(a);
                putchar('\n');

                if (a != 'Y' && a != 'y')
                    break;
            }

            return 0;

        default:
            if (cmd[i])
            {
                printf("?\n");
                break;
            }

            if (has_from && !has_comma)
            {
                if (from < 1 || from > e.num_lines)
                    printf("?RANGE\n");
                else
                {
                    e.cur = from - 1;
                    ed_list(&e, e.cur, e.cur);
                }
            }
            else
                printf("?\n");
            break;
        }
    }

    return 0;
}
