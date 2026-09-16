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

#ifndef SDK_SYSCALL_H
#define SDK_SYSCALL_H

#include <fsctx.h>
#include <sys.h>

/* File operations */

/* Open name83 for reading or writing.  Returns fd or negative errno. */
int sys_open(const char *name, uint8_t writable);

/* Read up to len bytes into buf.  Returns bytes read or negative errno. */
int sys_read(int fd, void *buf, uint32_t len);

/* Write len bytes from buf.  Returns bytes written or negative errno. */
int sys_write(int fd, const void *buf, uint32_t len);

/* Close fd, flushing dirty data.  Returns 0 or negative errno. */
int sys_close(int fd);

/* Return total size in bytes of the open file (0 if fd is invalid). */
uint32_t sys_getsize(int fd);

/* Seek to offset in file.  Returns 0 or negative errno. */
int sys_seek(int fd, uint32_t offset);

/* Create a new empty file.  Returns 0 or negative errno. */
int sys_create(const char *name);

/* Erase a file.  Returns 0 or negative errno. */
int sys_erase(const char *name);

/* Rename old to new.  Returns 0 or negative errno. */
int sys_rename(const char *old, const char *new);

/* Set attributes on all extents of a file.  Returns 0 or neg errno. */
int sys_fsetattr(const char *name, uint8_t attrib);

/* Directory scan: find pattern, fill out.  Returns a positive scan position
 * for the next call on success, or a negative errno (ENOENT if no match). */
int sys_findfile(const char *pattern, FileInfo *out, uint16_t start_pos);

/* Volume management */

/* Mount (format + bind) volume.  Returns 0 or negative errno. */
int sys_mount(int8_t slot);

/* Resize volume by delta blocks (+grow / -shrink / 0 = no-op).  Returns 0 or
 * negative errno. */
int sys_resize(int8_t slot, int16_t delta);

/* Unmount volume.  Returns 0 or negative errno. */
int sys_unmount(int8_t slot);

/* Read volume metadata into stat.  Returns 0 or negative errno. */
int sys_vstat(int8_t vol_id, VolStat *stat);

/* Set attribute byte on a mounted volume.  Returns 0 or neg errno. */
int sys_vsetattr(int8_t vol_id, uint8_t attr);

/* Process / context */

/* Copy argc/argv into out from the current program's arg block. */
int sys_args(ArgBlock *out);

/* Load and execute name; does not return on success — the current program
 * is replaced.  Returns negative errno on failure. */
int sys_exec(const char *name, int argc, char **argv);

/* Terminate program with return code rc.  Does not return. */
void sys_exit(int rc) __attribute__((noreturn));

/* Get current filesystem context (volume + user area). */
int sys_getctx(FsContext *out);

/* Set filesystem context for subsequent operations. */
int sys_setctx(FsContext ctx);

/* System services */

/* Copy system info into out.  Returns 0 or negative errno. */
int sys_info(SysInfo *out);

/* Read an environment slot.  Returns the slot value. */
uint32_t sys_getenv(uint8_t slot);

/* Write an environment slot.  Returns 0 or negative errno. */
int sys_setenv(uint8_t slot, uint32_t value);

/* Return monotonic milliseconds since boot. */
uint32_t sys_millis(void);

/* Flush all writeback caches to disk.  Returns 0 or negative errno. */
int sys_sync(void);

#endif /* SDK_SYSCALL_H */