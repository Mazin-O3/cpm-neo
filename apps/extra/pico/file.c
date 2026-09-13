#include "pico.h"

int file_load(PicoState *s, const char *path)
{
    if (!path || !path[0])
        return ENOENT;

    int fd = open(path, "r");
    if (fd < 0)
        return fd;

    s->gap.start = 0;
    s->gap.end = BUF_SIZE;
    int pos = 0;
    int n;

    while ((n = read(fd, s->gap.data + pos, BUF_SIZE - pos)) > 0)
    {
        pos += n;
        if (pos >= BUF_SIZE)
        {
            s->flags.truncated = 1;
            break;
        }
    }

    close(fd);

    s->gap.start = pos;
    s->gap.end = BUF_SIZE;

    gap_recount_lines(s);

    if (s->cur.lines == 0)
    {
        s->cur.lines = 1;
        s->gap.start = 0;
    }

    return 0;
}

int file_save(PicoState *s, const char *path)
{
    if (!path || !path[0])
        return EINVAL;

    char buf[ARG_LEN_MAX];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    remove(buf);
    int fd = open(buf, "w");

    if (fd < 0)
        return fd;

    int tl = gap_text_len(s);

    if (s->gap.start > 0)
    {
        int w = write(fd, (uint8_t *)s->gap.data, (uint32_t)s->gap.start);
        if (w != s->gap.start)
        {
            close(fd);
            return (w < 0) ? w : ENOSPC;
        }
    }

    int after_len = tl - s->gap.start;
    if (after_len > 0)
    {
        int w = write(fd, (uint8_t *)(s->gap.data + s->gap.end), (uint32_t)after_len);
        if (w != after_len)
        {
            close(fd);
            return (w < 0) ? w : ENOSPC;
        }
    }

    close(fd);
    return 0;
}

void file_report_failure(PicoState *s, int rc)
{
    char buf[80];
    snprintf(buf, sizeof(buf), "?%s", strerror(rc));
    file_prompt_error(s, buf);
}

int file_maybe_save(PicoState *s)
{
    if (!s->flags.file_modified)
        return 0;

    printf(CSI_SHOW);
    printf(CSI_CUP, SCREEN_ROWS, 1);
    printf("\r" CSI_EL);
    printf("Save modified file? (Y/N) ");

    int ans = toupper(getchar());

    if (ans == 'Y')
    {
        if (s->file.is_default)
        {
            if (file_prompt(s, "Save as: ") < 0)
            {
                printf(CSI_HIDE);
                scr_render(s);
                return -1;
            }
            strcpy(s->file.orig, s->file.name);
            s->file.is_default = 0;
        }

        int rc = file_save(s, s->file.name);
        if (rc < 0)
        {
            file_report_failure(s, rc);
            return -1;
        }
        s->flags.file_modified = 0;
        return 0;
    }

    if (ans == 'N')
        return 0;

    printf(CSI_HIDE);
    scr_render(s);
    return -1;
}

void file_truncate_83(char *name)
{
    char *dot = strrchr(name, '.');
    if (dot)
    {
        int base_len = dot - name;
        if (base_len > NAME83_BASE)
        {
            memmove(name + NAME83_BASE, dot, strlen(dot) + 1);
            dot = name + NAME83_BASE;
        }
        if (strlen(dot + 1) > NAME83_EXT)
            dot[NAME83_EXT + 1] = '\0';
    }
    else
    {
        int len = strlen(name);
        if (len > NAME83_BASE)
            name[NAME83_BASE] = '\0';
    }
}

int file_prompt(PicoState *s, const char *prompt)
{
    char newname[ARG_LEN_MAX];
    int pos;

    printf(CSI_CUP, SCREEN_ROWS, 1);
    printf("\r" CSI_EL);
    printf("%s", prompt);
    printf(CSI_SHOW);

    pos = 0;

    for (;;)
    {
        int c = getchar();

        if (c == '\n' || c == '\r')
        {
            if (pos == 0)
                continue;
            break;
        }

        if (c == 0x1B)
        {
            scr_render(s);
            return -1;
        }

        if ((c == 0x08 || c == 0x7F) && pos > 0)
        {
            pos--;
            printf("\b \b");
            continue;
        }

        if (c >= 0x20 && c < 0x7F && pos < (int)sizeof(newname) - 1)
        {
            newname[pos++] = (char)c;
            putchar(c);
        }
    }
    newname[pos] = '\0';

    file_truncate_83(newname);
    strcpy(s->file.name, newname);

    return 0;
}
