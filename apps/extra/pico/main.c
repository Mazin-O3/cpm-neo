#include "pico.h"

static PicoState g_state;

static int gap_idx_from_rc(PicoState *s, int row, int *col)
{
    int start, end;
    if (!gap_line_range(s, row, &start, &end))
    {
        *col = 0;
        return gap_text_len(s);
    }
    int line_len = end - start;
    if (*col > line_len)
        *col = line_len;
    return start + *col;
}

void file_prompt_error(PicoState *s, const char *msg)
{
    printf(CSI_SHOW);
    printf(CSI_CUP, SCREEN_ROWS, 1);
    printf("\r" CSI_EL);
    printf("%s", msg);
    getchar();
    scr_render(s);
}

static int parse_args(PicoState *s, ArgBlock *args)
{
    if (args->argc < 2)
    {
        s->file.is_default = 1;
        s->file.name[0] = '\0';
        return 0;
    }

    if (args->argc == 3 && strcasecmp(args->argv[1], "c") == 0)
    {
        char name[ARG_LEN_MAX];
        strncpy(name, args->argv[2], sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';

        FileInfo fi;
        if (find(name, &fi) == 0)
        {
            printf("?File Exists\n");
            return 1;
        }

        int fd = open(name, "w");
        if (fd < 0)
        {
            printf("?%s\n", strerror(fd));
            return 1;
        }
        close(fd);
        return 1;
    }

    if (args->argc == 2)
    {
        s->file.is_default = 0;
        strncpy(s->file.name, args->argv[1], sizeof(s->file.name) - 1);
        s->file.name[sizeof(s->file.name) - 1] = '\0';
        return 0;
    }

    printf("PICO [C] <FILENAME>\n");
    return 1;
}

static int load_file(PicoState *s)
{
    strcpy(s->file.orig, s->file.name);
    gap_init(s);

    if (!s->file.is_default)
    {
        int rc = file_load(s, s->file.name);

        if (rc < 0 && rc != -ENOENT && rc != ENOENT)
        {
            printf("?%s\n", strerror(rc));
            return 1;
        }

        if (rc == ENOENT || rc == -ENOENT)
        {
            s->flags.file_modified = 1;
            s->flags.scr_dirty = 1;
        }

        if (s->file.name[0])
        {
            FileInfo fi;
            if (find(s->file.name, &fi) == 0 && (fi.attrib & (FILE_ATTR_READ_ONLY | FILE_ATTR_SYSTEM)))
                s->file.readonly = 1;
        }
    }

    return 0;
}

static void handle_escape(PicoState *s)
{
    for (;;)
    {
        int timeout = 2000;
        while (!peekchar() && timeout > 0)
            timeout--;

        if (!peekchar())
            break;

        int c = getchar();
        if (c == KEY_ESC)
            continue;

        if (c != '[')
            break;

        int param = 0;
        c = getchar();
        if (c == KEY_ESC)
            continue;

        while (isdigit(c))
        {
            param = param * 10 + (c - '0');
            c = getchar();
            if (c == KEY_ESC)
                continue;
        }

        if (c == ';')
        {
            while (isdigit((c = getchar())))
                ;
            if (c == KEY_ESC)
                continue;
        }

        if (c == '~')
        {
            switch (param)
            {
            case 5:
                s->cur.row -= PICO_ROWS;
                if (s->cur.row < 0)
                    s->cur.row = 0;
                gap_move_to(s, gap_idx_from_rc(s, s->cur.row, &s->cur.col));
                gap_update_cursor(s);
                s->cur.top = s->cur.row;
                scr_render(s);
                continue;
            case 6:
                s->cur.row += PICO_ROWS;
                if (s->cur.row >= s->cur.lines)
                    s->cur.row = s->cur.lines - 1;
                gap_move_to(s, gap_idx_from_rc(s, s->cur.row, &s->cur.col));
                gap_update_cursor(s);
                s->cur.top = s->cur.row - PICO_ROWS + 1;
                if (s->cur.top < 0)
                    s->cur.top = 0;
                scr_render(s);
                continue;
            case 1:
                gap_move_to(s, 0);
                gap_update_cursor(s);
                s->cur.top = 0;
                scr_render(s);
                continue;
            case 4:
                gap_move_to(s, gap_text_len(s));
                gap_update_cursor(s);
                s->cur.top = s->cur.row - PICO_ROWS + 1;
                if (s->cur.top < 0)
                    s->cur.top = 0;
                scr_render(s);
                continue;
            }
        }
        else
        {
            switch (c)
            {
            case 'A':
                edit_cursor_up(s);
                break;
            case 'B':
                edit_cursor_down(s);
                break;
            case 'C':
                edit_cursor_right(s);
                break;
            case 'D':
                edit_cursor_left(s);
                break;
            }
        }

        scr_ensure_visible(s);
        scr_move_cursor(s);
        if (s->flags.scr_dirty && !peekchar())
            scr_render(s);

        break;
    }
}

static void handle_open(PicoState *s)
{
    char prev_filename[13], prev_orig[13], errbuf[80];
    int  prev_default = s->file.is_default;
    strcpy(prev_filename, s->file.name);
    strcpy(prev_orig, s->file.orig);

    if (file_maybe_save(s) >= 0 && file_prompt(s, "Open file: ") >= 0)
    {
        int fd = open(s->file.name, "r");
        if (fd < 0)
        {
            strcpy(s->file.name, prev_filename);
            strcpy(s->file.orig, prev_orig);
            s->file.is_default = prev_default;
            snprintf(errbuf, sizeof(errbuf), "?%s", strerror(fd));
            file_prompt_error(s, errbuf);
            return;
        }
        close(fd);

        gap_init(s);
        file_load(s, s->file.name);
        FileInfo fi2;
        s->file.readonly = (find(s->file.name, &fi2) == 0 && (fi2.attrib & (FILE_ATTR_READ_ONLY | FILE_ATTR_SYSTEM)));
        strcpy(s->file.orig, s->file.name);
        s->file.is_default = 0;
        s->cur.row = 0;
        s->cur.col = 0;
        s->cur.top = 0;
        s->flags.file_modified = 0;
        gap_move_to(s, 0);
        gap_update_cursor(s);
        scr_render(s);
    }
}

static void handle_save(PicoState *s)
{
    if (!s->flags.file_modified && !s->file.is_default)
        return;

    if (s->file.is_default)
    {
        char prev_name[ARG_LEN_MAX];
        strcpy(prev_name, s->file.name);
        int prev_default = 1;

        if (file_prompt(s, "Save as: ") < 0)
            return;

        int rc = file_save(s, s->file.name);
        if (rc < 0)
        {
            strcpy(s->file.name, prev_name);
            strcpy(s->file.orig, prev_name);
            s->file.is_default = prev_default;
            file_report_failure(s, rc);
            return;
        }

        strcpy(s->file.orig, s->file.name);
        s->file.is_default = 0;
    }
    else
    {
        int rc = file_save(s, s->file.name);
        if (rc < 0)
        {
            file_report_failure(s, rc);
            return;
        }
    }

    s->flags.file_modified = 0;
    s->flags.truncated = 0;
    scr_batch_start(s);
    scr_batch_draw_banner(s);
    scr_invalidate_status();
    scr_batch_status(s);
    scr_batch_printf(s, CSI_CUP CSI_SHOW, scr_row(s) + 2, scr_col(s) + 1);
    scr_batch_flush(s);
}

static int handle_quit(PicoState *s)
{
    if (file_maybe_save(s) < 0)
        return 0;
    return 1;
}

static void handle_regular_char(PicoState *s, int c)
{
    if (s->file.readonly)
        return;

    if (c == '\n' || c == '\r')
    {
        edit_insert_char(s, '\n');
        return;
    }

    if (c == KEY_BS || c == KEY_DEL)
    {
        edit_backspace(s);
        return;
    }

    if (c >= ' ' && c < KEY_DEL)
        edit_insert_char(s, (char)c);
}

int main(void)
{
    ArgBlock args;

    getargs(&args);

    if (parse_args(&g_state, &args))
        return 1;

    if (load_file(&g_state))
        return 1;

    g_state.cur.row = 0;
    g_state.cur.col = 0;
    g_state.cur.top = 0;

    gap_move_to(&g_state, 0);
    gap_update_cursor(&g_state);
    scr_render(&g_state);

    for (;;)
    {
        int c = getchar();

        switch (c)
        {
        case KEY_ESC:
            handle_escape(&g_state);
            break;
        case KEY_CTRL_O:
            handle_open(&g_state);
            break;
        case KEY_CTRL_S:
            handle_save(&g_state);
            break;
        case KEY_CTRL_Q:
            if (handle_quit(&g_state))
            {
                goto done;
            }
            break;
        default:
            handle_regular_char(&g_state, c);
            break;
        }

        scr_ensure_visible(&g_state);
        scr_move_cursor(&g_state);
        if (g_state.flags.scr_dirty && !peekchar())
            scr_render(&g_state);
    }

done:
    printf(CSI_SHOW);
    printf(CSI_CLS CSI_HOME);
    return 0;
}
