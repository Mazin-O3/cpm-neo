/*
 * libc/errno.h
 * CP/M Neo — Unified error code table
 *
 * Single set of error codes used by:
 *   - kernel syscall return values
 *   - libc/fs.h function return values
 *   - libc/stdlib.h function return values
 *   - strerror() in libc/stdio.c
 *
 * Rules:
 *   0        = success  (EOK)
 *   positive = valid fd or count (from open/read/write)
 *   negative = error
 */

#ifndef ERRNO_H
#define ERRNO_H

#define EOK 0        /* Success                            */
#define ENOENT -1    /* No such file or directory          */
#define EEXIST -2    /* File already exists                */
#define ENOSPC -3    /* No space left on device            */
#define EVOLRO -4    /* Volume is read-only                */
#define ENFILE -5    /* No free file descriptors           */
#define EIO -6       /* I/O error                          */
#define EINVAL -7    /* Invalid argument / bad parameter   */
#define ENOEXEC -8   /* Exec format error / empty program  */
#define E2BIG -9     /* Program image too large to load    */
#define EFILERO -10  /* File is read-only                  */
#define EDIRFULL -11 /* Root directory full                */
#define EPERM -12    /* Operation not permitted            */
#define ENOVOL -13   /* Volume not available (N/A)         */
#define EBADF -14    /* Bad file descriptor / handle        */
#define EBADFS -15   /* Bad filesystem                      */

#endif /* ERRNO_H */