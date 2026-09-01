/*
 * sdk/include/syscall.h — raw syscall wrappers
 *
 * Thin static-inline wrappers over the kernel's syscall jump table.
 * Each wrapper dispatches through g_syscall_table, the SyscallTable instance
 * published by the kernel in environment slot ENV_SYSCALL_PTR; the
 * address is fixed at link time (--just-symbols kernel.elf).  Prefer
 * the fs.h wrappers for filesystem operations; this header is the
 * low-level escape hatch.
 */

#ifndef SYSCALL_H
#define SYSCALL_H

#include "../kernel/kernel_abi.h"

extern const SyscallTable g_syscall_table;

/* Open name83 for reading or writing.  Returns fd or negative errno. */
static inline int sys_open(const char *name, uint8_t writable)
{
    return g_syscall_table.open(name, writable);
}

/* Read up to len bytes into buf.  Returns bytes read or negative errno. */
static inline int sys_read(int fd, void *buf, uint32_t len)
{
    return g_syscall_table.read(fd, buf, len);
}

/* Write len bytes from buf.  Returns bytes written or negative errno. */
static inline int sys_write(int fd, const void *buf, uint32_t len)
{
    return g_syscall_table.write(fd, buf, len);
}

/* Close fd, flushing dirty data.  Returns 0 or negative errno. */
static inline int sys_close(int fd)
{
    return g_syscall_table.close(fd);
}

/* Copy argc/argv into out from the current program's arg block. */
static inline int sys_args(ArgBlock *out)
{
    return g_syscall_table.args(out);
}

/* One-shot directory scan: find pattern, fill out.
 * Returns a positive scan position for the next call on success, or a negative
 * errno (ENOENT if no match). */
static inline int sys_findfile(const char *pattern, FileInfo *out, uint16_t start_pos)
{
    return g_syscall_table.findfile(pattern, out, start_pos);
}

/* Return total size in bytes of the open file (0 if fd is invalid). */
static inline int sys_getsize(int fd)
{
    return (int)g_syscall_table.getsize(fd);
}

/* Create a new empty file.  Returns 0 or negative errno. */
static inline int sys_create(const char *name)
{
    return g_syscall_table.create(name);
}

/* Delete a file.  Returns 0 or negative errno. */
static inline int sys_delete(const char *name)
{
    return g_syscall_table.delete(name);
}

/* Rename old to new.  Returns 0 or negative errno. */
static inline int sys_rename(const char *old, const char *new)
{
    return g_syscall_table.rename(old, new);
}

/* Mount (format + bind) volume.  Returns 0 or negative errno. */
static inline int sys_mount(int8_t slot)
{
    return g_syscall_table.mount(slot);
}

/* Resize volume by delta blocks (+grow / -shrink / 0 = no-op).
 * Returns 0 or negative errno. */
static inline int sys_resize(int8_t slot, int16_t delta)
{
    return g_syscall_table.resize(slot, delta);
}

/* Unmount volume.  Returns 0 or negative errno. */
static inline int sys_unmount(int8_t slot)
{
    return g_syscall_table.unmount(slot);
}

/* Read volume metadata into stat.  Returns 0 or negative errno. */
static inline int sys_vstat(int8_t vol_id, VolStat *stat)
{
    return g_syscall_table.vstat(vol_id, stat);
}

/* Terminate program with return code rc.  Does not return. */
static inline __attribute__((noreturn)) void sys_exit(int rc)
{
    g_syscall_table.exit(rc);
    __builtin_unreachable();
}

/* Load and execute name.  Returns 0 on success, or negative errno
 * (does not return on success — the current program is replaced). */
static inline int sys_exec(const char *name, int argc, char **argv)
{
    return g_syscall_table.exec(name, argc, argv);
}

/* Read/write a memory-mapped I/O register via cmd encoding. */
static inline int sys_dev(uint32_t reg, uint32_t cmd, uint32_t *data)
{
    return g_syscall_table.dev(reg, cmd, data);
}

/* Set attributes on all extents of a file.  Returns 0 or neg errno. */
static inline int sys_fsetattr(const char *name, uint8_t attrib)
{
    return g_syscall_table.fsetattr(name, attrib);
}

/* Set attribute byte on a mounted volume.  Returns 0 or neg errno. */
static inline int sys_vsetattr(int8_t vol_id, uint8_t attr)
{
    return g_syscall_table.vsetattr(vol_id, attr);
}

/* Copy system info into out.  Returns 0 or negative errno. */
static inline int sys_info(SysInfo *out)
{
    return g_syscall_table.info(out);
}

/* Seek to offset in file.  Returns 0 or negative errno. */
static inline int sys_seek(int fd, uint32_t offset)
{
    return g_syscall_table.seek(fd, offset);
}

/* Get current filesystem context (volume + user area). */
static inline int sys_getctx(FsContext *out)
{
    return g_syscall_table.getctx(out);
}

/* Set filesystem context for subsequent operations. */
static inline int sys_setctx(FsContext ctx)
{
    return g_syscall_table.setctx(ctx);
}

/* Read an environment slot.  Returns the slot value. */
static inline uint32_t sys_getenv(uint8_t slot)
{
    return g_syscall_table.getenv(slot);
}

/* Write an environment slot.  Returns 0 or negative errno. */
static inline int sys_setenv(uint8_t slot, uint32_t value)
{
    return g_syscall_table.setenv(slot, value);
}

/* Return current time in seconds since boot. */
static inline uint32_t sys_time(void)
{
    return g_syscall_table.time();
}

/* Flush all writeback caches to disk.  Returns 0 or negative errno. */
static inline int sys_sync(void)
{
    return g_syscall_table.sync();
}

/* Query console dimensions.  Writes width and height in characters. */
static inline int sys_consize(uint8_t *cw, uint8_t *ch)
{
    return g_syscall_table.consize(cw, ch);
}

#endif /* SYSCALL_H */
