#include "ed.h"

/* File I/O */

int ed_load(Editor *e, const char *path)
{
    strcpy(e->name, path);

    FileInfo fi;

    if (find(path, &fi) == 0 && (fi.attrib & FILE_ATTR_READ_ONLY))
    {
        printf("** FILE IS READ/ONLY **\n");
        e->readonly = 1;
    }

    int fd = open(e->name, "r");

    if (fd < 0)
    {
        e->gap_start = 0;
        e->gap_end = EDIT_BUF_SIZE;
        return -1;
    }

    char line[LINE_LEN];

    while (e->num_lines < ED_MAX_LINES && readline(fd, line, sizeof(line)) > 0)
    {
        if (ed_put_line(e, e->num_lines, line, strlen(line)) < 0)
        {
            printf("?FULL\n");
            break;
        }
    }

    close(fd);

    e->gap_start = e->logical_bytes;
    e->gap_end = EDIT_BUF_SIZE;

    return 0;
}

int ed_save(Editor *e)
{
    if (e->readonly)
    {
        printf("** FILE IS READ/ONLY **\n");
        return -1;
    }

    int fd = open(e->name, "w");

    if (fd == EFILERO)
    {
        printf("** FILE/VOL IS READ/ONLY **\n");
        return -1;
    }

    if (fd < 0)
        return -1;

    for (int i = 0; i < e->num_lines; i++)
    {
        int phys = log_to_phys(e, e->line_off[i]);
        write(fd, e->buf + phys, strlen(e->buf + phys));
        write(fd, "\n", 1);
    }

    int rc = close(fd);

    if (rc < 0)
    {
        printf("** SAVE FAILED **\n");
        return -1;
    }

    e->modified = 0;

    return 0;
}

/* External file read */

int ed_read(Editor *e, int line, const char *path)
{
    int fd = open(path, "r");

    if (fd < 0)
    {
        printf("NOT FOUND\n");
        return -1;
    }

    if (line < 0)
        line = 0;

    if (line > e->num_lines)
        line = e->num_lines;

    char lbuf[LINE_LEN];

    while (e->num_lines < ED_MAX_LINES && readline(fd, lbuf, sizeof(lbuf)) > 0)
    {
        if (ed_put_line(e, line, lbuf, strlen(lbuf)) < 0)
        {
            printf("?FULL\n");
            close(fd);
            return -1;
        }

        e->modified = 1;

        line++;
    }

    close(fd);

    e->cur = line - 1;

    return 0;
}
