/*
 * sdk/include/syscall.h — Raw syscall wrappers
 *
 * System calls are plain kernel functions (sys_open, sys_read, ...) that
 * user programs call directly.  Applications and the CCP resolve them at
 * link time against kernel.elf (--just-symbols), so a call site is a
 * direct jump to the kernel's function — There is no jump table, trap,
 * or ecall.  Prefer the fs.h wrappers for filesystem operations; this
 * header is the low-level escape hatch.
 */

#ifndef SYSCALL_H
#define SYSCALL_H

#include "abi.h"

/* Open name83 for reading or writing.  Returns fd or negative errno. */
int sys_open(const char *name, uint8_t writable);

/* Read up to len bytes into buf.  Returns bytes read or negative errno. */
int sys_read(int fd, void *buf, uint32_t len);

/* Write len bytes from buf.  Returns bytes written or negative errno. */
int sys_write(int fd, const void *buf, uint32_t len);

/* Close fd, flushing dirty data.  Returns 0 or negative errno. */
int sys_close(int fd);

/* Copy argc/argv into out from the current program's arg block. */
int sys_args(ArgBlock *out);

/* One-shot directory scan: find pattern, fill out.
 * Returns a positive scan position for the next call on success, or a negative
 * errno (ENOENT if no match). */
int sys_findfile(const char *pattern, FileInfo *out, uint16_t start_pos);

/* Return total size in bytes of the open file (0 if fd is invalid). */
uint32_t sys_getsize(int fd);

/* Create a new empty file.  Returns 0 or negative errno. */
int sys_create(const char *name);

/* Delete a file.  Returns 0 or negative errno. */
int sys_delete(const char *name);

/* Rename old to new.  Returns 0 or negative errno. */
int sys_rename(const char *old, const char *new);

/* Mount (format + bind) volume.  Returns 0 or negative errno. */
int sys_mount(int8_t slot);

/* Resize volume by delta blocks (+grow / -shrink / 0 = no-op).
 * Returns 0 or negative errno. */
int sys_resize(int8_t slot, int16_t delta);

/* Unmount volume.  Returns 0 or negative errno. */
int sys_unmount(int8_t slot);

/* Read volume metadata into stat.  Returns 0 or negative errno. */
int sys_vstat(int8_t vol_id, VolStat *stat);

/* Terminate program with return code rc.  Does not return. */
void sys_exit(int rc) __attribute__((noreturn));

/* Load and execute name.  Returns 0 on success, or negative errno
 * (does not return on success — The current program is replaced). */
int sys_exec(const char *name, int argc, char **argv);

/* Set attributes on all extents of a file.  Returns 0 or neg errno. */
int sys_fsetattr(const char *name, uint8_t attrib);

/* Set attribute byte on a mounted volume.  Returns 0 or neg errno. */
int sys_vsetattr(int8_t vol_id, uint8_t attr);

/* Copy system info into out.  Returns 0 or negative errno. */
int sys_info(SysInfo *out);

/* Seek to offset in file.  Returns 0 or negative errno. */
int sys_seek(int fd, uint32_t offset);

/* Get current filesystem context (volume + user area). */
int sys_getctx(FsContext *out);

/* Set filesystem context for subsequent operations. */
int sys_setctx(FsContext ctx);

/* Read an environment slot.  Returns the slot value. */
uint32_t sys_getenv(uint8_t slot);

/* Write an environment slot.  Returns 0 or negative errno. */
int sys_setenv(uint8_t slot, uint32_t value);

/* Return current time in seconds since boot. */
uint32_t sys_time(void);

/* Flush all writeback caches to disk.  Returns 0 or negative errno. */
int sys_sync(void);

/* Query console dimensions.  Writes width and height in characters. */
int sys_consize(uint8_t *cw, uint8_t *ch);

#endif /* SYSCALL_H */
