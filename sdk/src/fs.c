/*
 * sdk/src/fs.c — User-space filesystem API
 *
 * Thin wrappers around sys_* kernel calls.  The open() function maps
 * C-style mode strings ("r"/"w"/"a") to the corresponding BDOS flags.
 * The directory iterator (find/find_next/find_reset) tracks position in
 * g_find_pos across calls.
 *
 * Error convention: all functions return negative errno values on
 * failure, or a non-negative result (fd, byte count, 0 for success).
 */

#include "fs.h"
#include "syscall.h"

#define O_RDONLY 0x01
#define O_WRONLY 0x02
#define O_CREAT 0x04
#define O_APPEND 0x10

static int g_find_pos = 0;

int fs_setctx(FsContext ctx)
{
    return sys_setctx(ctx);
}

/*
 * open_flags — Map a C-style mode string to BDOS open flags.
 * Returns EINVAL for unknown modes (e.g. "r+").
 */
static int open_flags(const char *mode)
{
    if (strcmp(mode, "r") == 0)
        return O_RDONLY;

    if (strcmp(mode, "w") == 0)
        return O_WRONLY | O_CREAT;

    if (strcmp(mode, "a") == 0)
        return O_WRONLY | O_CREAT | O_APPEND;

    return EINVAL;
}

int open(const char *path, const char *mode)
{
    int flags = open_flags(mode);

    if (flags < 0)
        return flags;

    if (flags & O_APPEND)
    {
        int fd = sys_open(path, 1);

        if (fd == ENOENT)
            fd = sys_create(path);

        if (fd < 0)
            return fd;

        sys_seek(fd, sys_getsize(fd));

        return fd;
    }

    if (flags & O_CREAT)
    {
        FileInfo fi;

        if (sys_findfile(path, &fi, 0) > 0 && (fi.attrib & FILE_ATTR_READ_ONLY))
            return EFILERO;

        int rc = sys_delete(path);

        if (rc != EOK && rc != ENOENT)
            return rc;

        return sys_create(path);
    }

    return sys_open(path, (flags & O_WRONLY) ? 1 : 0);
}

int readline(int fd, char *buf, int sz)
{
    int pos = 0;
    int consumed = 0;
    char ch;

    while (read(fd, &ch, 1) == 1)
    {
        consumed++;

        if (ch == '\r')
            continue;

        if (ch == '\n' || ch == CH_EOF)
        {
            buf[pos] = '\0';
            return consumed;
        }

        if (pos < sz - 1)
            buf[pos++] = ch;
    }

    buf[pos] = '\0';
    return consumed;
}

int read(int fd, void *buf, uint32_t len)
{
    if (len <= 0)
        return 0;

    return sys_read(fd, buf, len);
}

int write(int fd, const void *buf, uint32_t len)
{
    if (len <= 0)
        return 0;

    return sys_write(fd, buf, len);
}

int close(int fd)
{
    return sys_close(fd);
}

int lseek(int fd, uint32_t offset, int whence)
{
    if (whence == SEEK_END)
    {
        uint32_t sz = sys_getsize(fd);
        offset = (offset > sz) ? sz : sz - offset;
    }

    return sys_seek(fd, offset);
}

int remove(const char *path)
{
    return sys_delete(path);
}

int rename(const char *old, const char *newname)
{
    return sys_rename(old, newname);
}

int fsetattr(const char *path, uint8_t attrib)
{
    return sys_fsetattr(path, attrib);
}

/*
 * find — Find the first matching file.  Resets on success.
 * Returns EOK if found, ENOENT if not.
 */
int find(const char *name, FileInfo *out)
{
    int rc = sys_findfile(name, out, 0);

    return (rc > 0) ? EOK : rc;
}

/*
 * find_next — Continue a directory scan from the previous position.
 * Advances on success, resets it to 0 on failure.
 * Call find_reset() to start from the beginning.
 */
int find_next(const char *pattern, FileInfo *out)
{
    int pos = sys_findfile(pattern, out, g_find_pos);

    if (pos > 0)
    {
        g_find_pos = pos;
        return EOK;
    }

    g_find_pos = 0;
    return pos;
}

void find_reset(void)
{
    g_find_pos = 0;
}

int fcopy(const char *dst_path, const char *src_path)
{
    int src_fd = open(src_path, "r");

    if (src_fd < 0)
        return src_fd;

    int dst_fd = open(dst_path, "w");

    if (dst_fd < 0)
    {
        close(src_fd);
        return dst_fd;
    }

    uint8_t buf[512];

    int n, rc = EOK;

    while ((n = read(src_fd, buf, sizeof(buf))) > 0)
    {
        int w = write(dst_fd, buf, n);

        if (w != n)
        {
            rc = (w < 0) ? w : ENOSPC;
            break;
        }
    }

    if (n < 0)
        rc = n;

    close(src_fd);
    close(dst_fd);

    return rc;
}

int vstat(int8_t vol, VolStat *out)
{
    return sys_vstat(vol, out);
}
