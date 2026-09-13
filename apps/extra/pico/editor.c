#include "pico.h"

static int prev_status_row = -1, prev_status_col = -1;

void scr_invalidate_status(void) { prev_status_row = -1; }

int scr_row(PicoState *s) { return s->cur.row - s->cur.top; }
int scr_col(PicoState *s) { return s->cur.col; }

static void clamp_scroll(PicoState *s)
{
    while (s->cur.row < s->cur.top)
        s->cur.top--;
    while (s->cur.row >= s->cur.top + PICO_ROWS)
        s->cur.top++;
    if (s->cur.top < 0)
        s->cur.top = 0;
}

void scr_batch_start(PicoState *s) { s->ansi.pos = 0; }

void scr_batch_printf(PicoState *s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int space_left = BATCH_LEN - s->ansi.pos - 1;
    if (space_left > 0)
    {
        int n = vsnprintf(s->ansi.buf + s->ansi.pos, space_left, fmt, ap);
        if (n > 0)
            s->ansi.pos += n;
    }
    va_end(ap);
}

void scr_batch_flush(PicoState *s)
{
    if (s->ansi.pos > 0)
    {
        sys_write(FD_STDOUT, s->ansi.buf, s->ansi.pos);
        s->ansi.pos = 0;
    }
}

void scr_batch_status(PicoState *s)
{
    if (s->cur.row == prev_status_row && s->cur.col == prev_status_col)
        return;
        
    prev_status_row = s->cur.row;
    prev_status_col = s->cur.col;
    char sbuf[128];

    int n = snprintf(sbuf, sizeof(sbuf), " Ln %d Col %d", s->cur.row + 1, s->cur.col + 1);
    int pad = SCREEN_WIDTH - n - 28;
    if (pad < 1)
        pad = 1;

    memset(sbuf + n, ' ', pad);
    n += pad;

    snprintf(sbuf + n, sizeof(sbuf) - (size_t)n,
        CSI_REVERSE "^S" CSI_RESET "=Save   "
        CSI_REVERSE "^O" CSI_RESET "=Open   "
        CSI_REVERSE "^Q" CSI_RESET "=Quit");

    scr_batch_printf(s, CSI_CUP_ROW CSI_EL "%s", SCREEN_ROWS, sbuf);
}

static int parse_volref(const char *name, uint8_t *vol, uint8_t *ua, const char **base)
{
    if (!isalpha((unsigned char)name[0]))
        return 0;
    int cp = -1;
    for (int i = 1; i <= 4 && name[i]; i++)
        if (name[i] == ':') { cp = i; break; }
    if (cp < 0)
        return 0;
    *vol = toupper((unsigned char)name[0]) - 'A';
    *ua = 0;
    if (cp > 1)
    {
        int n = 0;
        for (int i = 1; i < cp; i++)
        {
            if (name[i] < '0' || name[i] > '9') break;
            n = n * 10 + (name[i] - '0');
        }
        if (n <= USER_AREA_MAX)
            *ua = (uint8_t)n;
    }
    *base = name + cp + 1;
    return 1;
}

void scr_batch_draw_banner(PicoState *s)
{
    char tag[SCREEN_WIDTH + 1];
    char mod = s->flags.file_modified ? '*' : ' ';

    if (s->file.is_default)
    {
        snprintf(tag, sizeof(tag), " %cuntitled ", mod);
    }
    else
    {
        char loc_buf[8] = "";
        const char *display = s->file.name;
        uint8_t vol, ua;
        const char *base;
        if (parse_volref(s->file.name, &vol, &ua, &base))
        {
            snprintf(loc_buf, sizeof(loc_buf), "[%c%u:] ", 'A' + vol, (unsigned)ua);
            display = base;
        }
        snprintf(tag, sizeof(tag), " %s%c%s%s ", loc_buf, mod, display,
                 s->file.readonly ? "  [R/O]" : "");
    }

    int tag_len = strlen(tag);
    if (tag_len > SCREEN_WIDTH)
        tag_len = SCREEN_WIDTH;
    char buf[SCREEN_WIDTH + 1];
    memset(buf, ' ', SCREEN_WIDTH);
    int left = (SCREEN_WIDTH - tag_len) / 2;
    memcpy(buf + left, tag, tag_len);
    buf[SCREEN_WIDTH] = '\0';
    scr_batch_printf(s, CSI_REVERSE "\x1B[H%s" CSI_RESET, buf);
}

void scr_move_cursor(PicoState *s)
{
    scr_batch_start(s);
    scr_batch_status(s);
    scr_batch_printf(s, CSI_CUP, scr_row(s) + 2, scr_col(s) + 1);
    scr_batch_flush(s);
}

void scr_redraw_line(PicoState *s, int row)
{
    int r = row - s->cur.top;

    if (r < 0 || r >= PICO_ROWS)
        return;

    char line_buf[MAX_VISIBLE_LINE + 1];

    gap_get_line(s, row, line_buf, sizeof(line_buf));
    scr_batch_start(s);
    scr_batch_printf(s, CSI_HIDE);
    scr_batch_draw_banner(s);
    scr_batch_printf(s, CSI_CUP_ROW CSI_EL "%s", r + 2, line_buf);
    scr_batch_status(s);
    scr_batch_printf(s, CSI_CUP CSI_SHOW, scr_row(s) + 2, scr_col(s) + 1);
    scr_batch_flush(s);
    s->flags.scr_dirty = 0;
}

void scr_render(PicoState *s)
{
    clamp_scroll(s);

    int tl = gap_text_len(s);
    char line_buf[MAX_VISIBLE_LINE + 1];
    int cur_line = 0, start = 0, vis_row = 0;
    int li;

    scr_batch_start(s);
    scr_batch_printf(s, CSI_HIDE);
    scr_batch_draw_banner(s);

    for (li = 0; li <= tl && vis_row < PICO_ROWS; li++)
    {
        int is_nl = (li < tl) && (s->gap.data[gap_buf_idx(s, li)] == '\n');
        if (is_nl || li == tl)
        {
            if (cur_line >= s->cur.top)
            {
                int len = li - start;
                int r = cur_line - s->cur.top;
                if (len > MAX_VISIBLE_LINE)
                    len = MAX_VISIBLE_LINE;
                for (int j = 0; j < len; j++)
                    line_buf[j] = s->gap.data[gap_buf_idx(s, start + j)];
                line_buf[len] = '\0';
                scr_batch_printf(s, CSI_CUP_ROW CSI_EL "%s", r + 2, line_buf);
                vis_row++;
            }
            cur_line++;
            start = li + 1;
        }
    }
    for (int r = vis_row; r < PICO_ROWS; r++)
        scr_batch_printf(s, CSI_CUP_ROW CSI_EL, r + 2);
    scr_batch_printf(s, CSI_CUP_ROW CSI_EL, SCREEN_ROWS - 1);

    prev_status_row = -1;
    scr_batch_status(s);
    scr_batch_printf(s, CSI_CUP CSI_SHOW, scr_row(s) + 2, scr_col(s) + 1);
    scr_batch_flush(s);
    s->flags.scr_dirty = 0;
}

void scr_ensure_visible(PicoState *s)
{
    int old_top = s->cur.top;
    clamp_scroll(s);
    int delta = s->cur.top - old_top;
    if (delta == 0)
        return;
    s->flags.scr_dirty = 0;

    if (delta < -1 || delta > 1)
    {
        s->flags.scr_dirty = 1;
        return;
    }

    scr_batch_start(s);
    scr_batch_printf(s, CSI_HIDE);
    scr_batch_printf(s, CSI_DECSTBM, 2, SCREEN_ROWS - 2);

    if (delta == -1)
    {
        scr_batch_printf(s, CSI_CUP CSI_RI CSI_RST_SCR, 2, 1);
        char line_buf[MAX_VISIBLE_LINE + 1];
        gap_get_line(s, s->cur.top, line_buf, sizeof(line_buf));
        scr_batch_printf(s, CSI_CUP_ROW CSI_EL "%s", 2, line_buf);
    }
    else
    {
        scr_batch_printf(s, CSI_CUP CSI_IND CSI_RST_SCR, SCREEN_ROWS - 2, 1);
        int bottom_idx = s->cur.top + PICO_ROWS - 1;
        char line_buf[MAX_VISIBLE_LINE + 1];
        gap_get_line(s, bottom_idx, line_buf, sizeof(line_buf));
        if (bottom_idx < s->cur.lines)
            scr_batch_printf(s, CSI_CUP_ROW CSI_EL "%s", SCREEN_ROWS - 2, line_buf);
        else
            scr_batch_printf(s, CSI_CUP_ROW CSI_EL, SCREEN_ROWS - 2);
    }

    scr_batch_status(s);
    scr_batch_printf(s, CSI_CUP CSI_SHOW, scr_row(s) + 2, scr_col(s) + 1);
    scr_batch_flush(s);
}
