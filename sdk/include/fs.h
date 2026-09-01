#ifndef FS_H
#define FS_H

#include "../kernel/kernel_abi.h"
#include "errno.h"
#include "string.h"
#include "syscall.h"
#include <stdint.h>

/* Seek whence */
#define SEEK_SET 0
#define SEEK_END 1

/* Set the filesystem context (volume + user area) for subsequent operations. Returns EOK or negative errno. */
int fs_setctx(FsContext ctx);

/* Open file: "r" -> read, "w" -> write+create+trunc, "a" -> append+create; returns fd or negative errno */
int open(const char *path, const char *mode);

/* Read up to len bytes into buf; returns bytes read or negative errno */
int read(int fd, void *buf, uint32_t len);

/* Write len bytes from buf; returns bytes written or negative errno */
int write(int fd, const void *buf, uint32_t len);

/* Close fd */
int  close(int fd);

/* Read one line (up to '\n' or EOF) into buf; returns total bytes consumed from fd */
int readline(int fd, char *buf, int sz);

/* Seek: SEEK_SET -> absolute, SEEK_END -> offset from end; returns new position or negative errno */
int lseek(int fd, uint32_t offset, int whence);

/* Delete a file; returns 0 or negative errno */
int remove(const char *path);

/* Rename a file; returns 0 or negative errno */
int rename(const char *old, const char *newname);

/* Copy a file's contents from src_path to dst_path (dst must not exist or
 * is truncated). Attributes are NOT copied. Returns EOK or negative errno
 * (ENOENT if src missing; dst-side errors on open/write). */
int fcopy(const char *dst_path, const char *src_path);

/* Set file attributes (read-only, system, archive); returns 0 or negative errno */
int fsetattr(const char *path, uint8_t attrib);

/* Set volume mode (access mode); returns 0 or negative errno.
 * vol may be VOL_INVALID (-1) to probe the default volume. */
static inline int vsetattr(int8_t vol, uint8_t mode)
{
    return sys_vsetattr(vol, mode);
}

/* Mount volume (allocates default blocks and formats); returns 0 or negative errno */
static inline int mount(int8_t vol)
{
    return sys_mount(vol);
}

/* Resize mounted volume by delta blocks (+grow / -shrink / 0 = no-op);
 * returns 0 or negative errno */
static inline int resize(int8_t vol, int16_t delta)
{
    return sys_resize(vol, delta);
}

/* Unmount volume (full teardown, frees all blocks); returns 0 or negative errno */
static inline int unmount(int8_t vol)
{
    return sys_unmount(vol);
}

/* Flush all writeback caches to disk; returns 0 or negative errno */
static inline int sync(void)
{
    return sys_sync();
}

/* One-shot file lookup: does |name| exist?  Always searches from the
 * beginning of the directory.  Safe to call from anywhere — never
 * touches internal iteration state.
 *
 *   FileInfo fi;
 *   if (find("MYFILE.TXT", &fi) == EOK) { ... }
 *
 * Returns EOK or ENOENT. */
int find(const char *name, FileInfo *out);

/* Continue matching |pattern| from the last-seen position in the current
 * directory.  Call find_reset() before starting a fresh iteration.
 *
 *   find_reset();
 *   while (find_next("*.COM", &di) == EOK) {
 *       process(&di);
 *   }
 *
 * NOT safe to call from inside another find/find_next loop — use find()
 * or sys_findfile() for one-shots in callbacks.
 *
 * Returns EOK or ENOENT. */
int find_next(const char *pattern, FileInfo *out);

/* Reset find_next() iteration position back to the start.
 * Always call this before beginning a new wildcard walk. */
void find_reset(void);

/* Get volume stats (total/free bytes, label, access mode) */
int vstat(int8_t vol, VolStat *out);

#endif /* FS_H */
