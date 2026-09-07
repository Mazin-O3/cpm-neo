#include "stdio.h"
#include "errno.h"
#include "string.h"
#include "ctype.h"
#include "syscall.h"

int putchar(int c)
{
    char ch = (char)c;
    sys_write(FD_STDOUT, &ch, 1);
    return c;
}

int puts(const char *s)
{
    while (*s)
    {
        putchar(*s++);
    }

    putchar('\n');
    return 0;
}

int peekchar(void)
{
    return sys_read(FD_STDIN, 0, 0) != 0;
}

int getchar(void)
{
    unsigned char c;
    int bytes_read = sys_read(FD_STDIN, &c, 1);

    if (bytes_read <= 0)
    {
        return EOF;
    }

    return c;
}

int getline(char *buf, int maxlen)
{
    int pos = 0, ch;

    while ((ch = getchar()) != EOF)
    {
        char c = (char)ch;

        if (c == '\033')
        {
            if (!peekchar())
            {
                continue;
            }

            char next1 = (char)getchar();

            if (next1 == '[' || next1 == 'O')
            {
                int next2;
                do
                {
                    next2 = getchar();
                } while (next2 != EOF && !isalpha((char)next2) && next2 != '~');
            }

            continue;
        }

        if (c == '\b' || c == 127)
        {
            if (pos > 0)
            {
                pos--;
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }

            continue;
        }

        if (c == '\n' || c == '\r')
        {
            putchar('\r');
            putchar('\n');
            break;
        }

        if (c < 0x20)
            continue;

        if (pos < maxlen - 1)
        {
            buf[pos++] = c;
            putchar(c);
        }
    }

    buf[pos] = '\0';
    return (pos == 0 && ch == EOF) ? -1 : pos;
}

const char *strerror(int err)
{
    switch (err)
    {
    case 0:
        return "OK";
    case ENOENT:
        return "No File";
    case EEXIST:
        return "File Exists";
    case ENOSPC:
        return "No Space";
    case EVOLRO:
        return "R/O";
    case EFILERO:
        return "File R/O";
    case ENFILE:
        return "No FCB";
    case EIO:
        return "Bad Sector";
    case EINVAL:
        return "Invalid";
    case ENOEXEC:
        return "Bad Load";
    case E2BIG:
        return "Too Big";
    case EPERM:
        return "Not Allowed";
    case ENOVOL:
        return "Not Mounted";
    case EBADF:
        return "Bad File Handle";
    case EBADFS:
        return "Bad FileSystem";
    case EDIRFULL:
        return "Directory Full";
    default:
        return "Unknown";
    }
}

static char *fmt_uint(char *end, uint32_t val, int base, int upper)
{
    static const char lo[] = "0123456789abcdef";
    static const char hi[] = "0123456789ABCDEF";
    const char *digits = upper ? hi : lo;
    *--end = '\0';

    if (val == 0)
    {
        *--end = '0';
        return end;
    }

    while (val)
    {
        *--end = digits[val % (uint32_t)base];
        val /= (uint32_t)base;
    }

    return end;
}

typedef struct
{
    char *buf;
    int pos;     /* Characters currently buffered (flush pointer) */
    int limit;   /* Buffer capacity */
    int total;   /* Characters produced so far */
    int bounded; /* 1 = stop at limit-1; 0 = flush to stdout at limit */
} Writer;

static void w_flush(Writer *w)
{
    if (w->buf && w->pos > 0)
    {
        sys_write(FD_STDOUT, w->buf, w->pos);
        w->pos = 0;
    }
}

static void w_putc(Writer *w, char c)
{
    w->total++;

    if (w->bounded)
    {
        /* Count-only when buf is NULL (snprintf(NULL, 0, ...)) */
        if (w->buf && w->pos < w->limit - 1)
            w->buf[w->pos] = c;

        w->pos++;
    }
    else if (w->buf)
    {
        if (w->pos >= w->limit)
            w_flush(w);
            
        w->buf[w->pos++] = c;
    }
    else
    {
        putchar(c);
    }
}

static void w_puts_n(Writer *w, const char *s, int len, int width, int left)
{
    int pad = width - len;

    if (!left)
    {
        while (pad-- > 0)
            w_putc(w, ' ');
    }

    for (int i = 0; i < len; i++)
        w_putc(w, s[i]);

    if (left)
    {
        while (pad-- > 0)
            w_putc(w, ' ');
    }
}

/* Emit a zero-padded, then left/right-padded, numeric string. */
static void emit_num(Writer *w, const char *p, int width, int left, int zpad)
{
    int len = (int)strlen(p);
    int pad = width - len;

    if (!left && zpad)
    {
        while (pad-- > 0)
            w_putc(w, '0');
        width = len;
    }

    w_puts_n(w, p, len, width, left);
}

static int do_vprintf(Writer *w, const char *fmt, va_list ap)
{
    char tmp[16];

    for (; *fmt; fmt++)
    {
        if (*fmt != '%')
        {
            w_putc(w, *fmt);
            continue;
        }

        fmt++;

        int left = 0, zpad = 0;

        if (*fmt == '-')
        {
            left = 1;
            fmt++;
        }

        if (*fmt == '0')
        {
            zpad = 1;
            fmt++;
        }

        int width = 0;

        while (isdigit(*fmt))
        {
            width = width * 10 + (*fmt++ - '0');
        }

        char spec = *fmt;

        if (!spec)
            break;

        switch (spec)
        {
        case 'c':
        {
            char c = (char)va_arg(ap, int);
            w_puts_n(w, &c, 1, width, left);
            break;
        }

        case 's':
        {
            const char *s = va_arg(ap, const char *);

            if (!s)
                s = "(null)";
            int len = 0;

            while (s[len])
                len++;
            w_puts_n(w, s, len, width, left);
            break;
        }

        case 'd':
        case 'i':
        {
            int32_t v = va_arg(ap, int32_t);
            char *p;

            if (v < 0)
            {
                p = fmt_uint(tmp + sizeof(tmp), -(uint32_t)v, 10, 0);
                *--p = '-';
            }
            else
            {
                p = fmt_uint(tmp + sizeof(tmp), (uint32_t)v, 10, 0);
            }

            emit_num(w, p, width, left, zpad);
            break;
        }

        case 'u':
        {
            char *p = fmt_uint(tmp + sizeof(tmp), va_arg(ap, uint32_t), 10, 0);
            emit_num(w, p, width, left, zpad);
            break;
        }

        case 'x':
        case 'X':
        {
            char *p = fmt_uint(tmp + sizeof(tmp), va_arg(ap, uint32_t), 16, spec == 'X');
            emit_num(w, p, width, left, zpad);
            break;
        }

        case '%':
            w_putc(w, '%');
            break;
        default:
            w_putc(w, '%');
            w_putc(w, spec);
            break;
        }
    }

    if (w->buf && w->limit > 0)
    {
        if (w->bounded)
        {
            int end = w->pos < w->limit ? w->pos : w->limit - 1;
            w->buf[end] = '\0';
        }
        else if (w->pos < w->limit)
        {
            w->buf[w->pos] = '\0';
        }
    }

    return w->total;
}

int vprintf(const char *fmt, va_list ap)
{
    char buf[64];
    Writer w = {.buf = buf, .pos = 0, .limit = 64, .total = 0, .bounded = 0};
    int n = do_vprintf(&w, fmt, ap);
    w_flush(&w);
    return n;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    Writer w = {.buf = buf, .pos = 0, .limit = (int)size, .total = 0, .bounded = 1};
    return do_vprintf(&w, fmt, ap);
}

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

int anykey(const char *msg, int *row, int screen_rows)
{
    if (++(*row) < screen_rows - 1)
        return 0;
        
    *row = 0;
    printf(msg);
    int c = getchar();
    printf("\r     \r");
    return c == CH_ESC;
}
