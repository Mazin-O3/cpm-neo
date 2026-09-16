/*
 * sdk/include/sys.h — CP/M Neo system info, console and environment
 *
 * System-level conventions shared by the kernel, the CCP, and user
 * programs: system statistics (SysInfo), environment slots, and console
 * key / geometry constants.
 */

#ifndef SDK_SYS_H
#define SDK_SYS_H

#include <stdint.h>

#include "fsctx.h"

/*
 * System statistics
 */
typedef struct
{
    uint32_t tpa;                      /* Transient program area base */
    uint16_t os_version;               /* CP/M Neo version           */
    uint16_t kern_version;             /* Kernel build version       */
    uint16_t ccp_version;              /* CCP build version          */
    uint16_t disk_size_kb;             /* Block grid capacity (KB)   */
    uint16_t disk_unalloc_kb;          /* Unallocated pool (KB)      */
    uint8_t  vol_mounted[MAX_VOLUMES]; /* 1 = mounted                */
    char     platform[9];              /* Platform name, NUL-terminated */
    uint8_t  xip;                      /* 1 = XIP disk image         */
} SysInfo;

/* Environment slots  */

#define ENV_RETURN_CODE  0 /* Return code of last program/command */
#define ENV_BATCH_OFFSET 1 /* Offset of batch file in CCP        */
#define ENV_USER_DEFINED 2 /* User-defined environment slot      */
#define ENV_SLOTS_MAX    3

/* Console controls */

/* Control-key conventions shared by the kernel, SDK and apps. */

#define CH_BREAK 0x03 /* ^C — Break a running program                  */
#define CH_EOF   0x1A /* ^Z — End-of-file marker in text files         */
#define CH_ESC   0x1B /* ESC — Abort listings / quit pager             */

/* Console geometry. */

#define CONSOLE_WIDTH  80
#define CONSOLE_HEIGHT 24

#endif /* SDK_SYS_H */