/*
 * kernel/kernel.c — syscall implementations and program loader
 *
 * Each sys_* function is a plain kernel function that user programs call
 * directly (see syscall-reference.md).  A typical file syscall:
 *   1. Strips the volume/user prefix from the path (parse_prefix)
 *   2. Converts the filename to padded 8.3 format (make_name83)
 *   3. Calls the bd_* function and translates the return value
 *      through fd offset by FD_FILE_BASE
 *
 * Console I/O (stdin/stdout) is handled inline without going to bdos.
 * The is_ccp flag gates writes to ENV_RETURN_CODE and ENV_BATCH_OFFSET
 * so transient programs cannot corrupt CCP state.
 */

#include "kernel.h"
#include "disk.h"
#include "stdlib.h"
#include "string.h"
#include "ctype.h"
#include <limits.h>
#include "syscall.h"

/* JUMP: transfer control to a program at |addr| */
#define JUMP(addr) ((void (*)(void))(uintptr_t)(addr))()

/* Per-kernel-environment slots: indexed by ENV_* constants.
 * is_ccp gates writes so transient programs cannot corrupt CCP state. */
typedef struct
{
    uint32_t env[ENV_SLOTS_MAX];
    uint8_t is_ccp;
} KernelEnv;

/* Global kernel state: fd/fs context for the running process plus
 * environment slots. */
typedef struct
{
    ArgBlock args;
    FsContext fs_ctx;
    KernelEnv kenv;
} KernelState;

static KernelState g_kstate = {0};

/*
 * Volume/user prefixes are optional and position-dependent; scan up to
 * 4 chars for a colon delimiter.  This lets the CCP accept bare
 * filenames transparently — only an explicit "X:" or "Xn:" triggers
 * a context switch.
 */
static FsContext parse_prefix(const char **path_ptr)
{
    FsContext ctx = g_kstate.fs_ctx;

    if (!path_ptr || !*path_ptr)
        return ctx;

    const char *p = *path_ptr;

    if (isalpha((unsigned char)p[0]))
    {
        int colon = -1;

        for (int i = 1; i <= 4 && p[i]; i++)

            if (p[i] == ':')
            {
                colon = i;
                break;
            }

        if (colon < 0)
            return ctx;

        ctx.vol_id = toupper((unsigned char)p[0]) - 'A';

        if (ctx.vol_id >= VOL_MAX)
            return ctx;

        if (colon > 1)
        {
            int ua = 0;

            for (int i = 1; i < colon; i++)
            {
                if (p[i] < '0' || p[i] > '9')
                    return ctx;

                ua = ua * 10 + (p[i] - '0');
            }

            if (ua > USER_AREA_MAX)
                return ctx;

            ctx.user_area = (uint8_t)ua;
        }

        *path_ptr = p + colon + 1;
        return ctx;
    }

    if (isdigit((unsigned char)p[0]))
    {
        char *ep;
        int ua = strtoi(p, &ep, 10);

        if (ep > p && *ep == ':' && ua >= 0 && ua <= USER_AREA_MAX)
        {
            ctx.user_area = (uint8_t)ua;
            *path_ptr = ep + 1;
        }
    }

    return ctx;
}

static inline int resolve_file_fd(int sys_fd)
{
    return sys_fd - FD_FILE_BASE;
}

/*
 * Directory entries store filenames as fixed-width 8.3 with space
 * padding; callers must produce this format before passing to bd_*.
 * Splitting on the last '.' lets the base and extension be padded independently.
 */
static int make_name83(const char *src, char *out)
{
    memset(out, ' ', NAME83_LEN);
    out[NAME83_LEN] = '\0';

    if (!src || !src[0])
        return ENOENT;

    const char *dot = strchr(src, '.');

    int nbase = dot ? (int)(dot - src) : (int)strlen(src);

    if (nbase == 0)
        return EINVAL;

    if (nbase > NAME83_BASE)
        nbase = NAME83_BASE;

    /* DRI CCP rule: a '*' fills the remainder of its own field with '?',
     * and any characters after it are discarded until the field delimiter
     * ('.') or end of input.  Unspecified fields stay blank, and blanks
     * match only blanks during directory searches. */
    int i = 0;

    while (i < nbase && src[i] != '*')
    {
        out[i] = toupper((unsigned char)src[i]);
        i++;
    }

    if (i < nbase)
        memset(out + i, '?', NAME83_BASE - i);

    if (dot)
    {
        const char *q = dot + 1;
        int e = 0;

        while (e < NAME83_EXT && *q && *q != '.' && *q != '*')
        {
            out[NAME83_BASE + e] = toupper((unsigned char)*q);
            e++;
            q++;
        }

        if (*q == '*' && e < NAME83_EXT)
            memset(out + NAME83_BASE + e, '?', NAME83_EXT - e);
    }

    return EOK;
}

/*
 * Boot-time initialisation.
 * Binds every available volume; the first successfully bound volume
 * becomes the default cwd.
 */
int kernel_init(void)
{
    if (disk_init() != 0)
        return EIO;

    g_kstate.fs_ctx = (FsContext){VOL_INVALID, 0};

    for (uint8_t v = 0; v < VOL_MAX; v++)
    {
        if (bd_bind(v) == EOK && g_kstate.fs_ctx.vol_id == VOL_INVALID)
            g_kstate.fs_ctx.vol_id = v;
    }

    return EOK;
}

/*
 * Load a .COM program into the TPA and jump to it.
 * Fails with E2BIG if the file exceeds available TPA space.
 */
/* XIP address for code at raw physical sector phy_sec, or 0 if the disk is
 * not XIP-formatted.  Single place that gates on disk_xip() and owns the
 * XIP_BASE + sec*512 formula.  User .com files are always RAM-loaded (see
 * kexec); this is used only for the CCP, which is placed exactly at its link
 * origin so it runs in place. */
static uint32_t xip_addr(uint16_t phy_sec)
{
    if (!disk_xip())
        return 0;

    return (uintptr_t)XIP_BASE + (uint32_t)phy_sec * DISK_SECTOR_SIZE;
}

int kexec(const char *name83, int argc, char **argv, FsContext ctx)
{
    int fd = bd_open(name83, ctx, 0);

    if (fd < 0)
        return fd;

    uint32_t file_size = bd_size(fd);

    if (file_size == 0)
    {
        bd_close(fd);
        return ENOEXEC;
    }

    if (file_size >= (uint32_t)__kernel_base - (uintptr_t)__tpa_base)
    {
        bd_close(fd);
        return E2BIG;
    }

    g_kstate.args.argc = (argc > ARGS_MAX) ? ARGS_MAX : argc;

    if (argv)
    {
        for (int i = 0; i < g_kstate.args.argc; i++)
        {
            char *dst = g_kstate.args.argv[i];
            strncpy(dst, argv[i], ARG_LEN_MAX - 1);
            dst[ARG_LEN_MAX - 1] = '\0';
        }
    }

    for (int i = g_kstate.args.argc; i < ARGS_MAX; i++)
        g_kstate.args.argv[i][0] = '\0';

    /* User programs always load into the TPA and run there.  On XIP disks
     * only the kernel and CCP execute from flash; .com files stay
     * RAM-resident (their blocks can sit anywhere, so in-place execution
     * could never match their link origin). */

    uint8_t *dest = (uint8_t *)__tpa_base;
    uint32_t remaining = file_size;

    while (remaining > 0)
    {
        uint16_t chunk = (remaining > USHRT_MAX) ? USHRT_MAX : (uint16_t)remaining;
        int n = bd_read(fd, dest, chunk);

        if (n <= 0)
        {
            bd_close(fd);
            return EIO;
        }

        dest += (uint32_t)n;
        remaining -= (uint32_t)n;
    }

    bd_close(fd);

    g_kstate.kenv.is_ccp = 0;

    JUMP(__tpa_base);

    for (;;)
        ;
}

/*
 * Reload the CCP from disk and jump to it (sys_exit).
 * Sets is_ccp so that subsequent setenv calls are allowed to write
 * ENV_RETURN_CODE / ENV_BATCH_OFFSET.
 */
void kexec_ccp(void)
{
    g_kstate.kenv.is_ccp = 1;

    uint8_t s0[DISK_SECTOR_SIZE];

    if (bios_read(0, s0) != 0)
        goto err;

    uint16_t sec = *(uint16_t *)(s0 + S0_CCP_SEC);
    uint16_t nsecs = *(uint16_t *)(s0 + S0_CCP_SIZE);

    /* Sector 0 (boot sector) and sector 1 (volume map) can never be the
     * CCP or kernel location.  Reject a corrupt or stale header that would
     * warm-boot into the boot-sector data at XIP_BASE + 0*512. */
    if (nsecs == 0 || sec < KERN_START_SEC)
        goto err;

    /* XIP path: the CCP is written contiguously by sysgen at exactly its
     * link origin (the sector-aligned end of the kernel image), so it is
     * always eligible for direct execution when the disk is XIP-formatted.
     * S0_CCP_SEC is a physical disk sector, so entry = XIP_BASE + sec*512.
     * If it somehow crosses the XIP window top the disk geometry is broken —
     * refuse to run rather than executing XIP-origin code from the TPA. */
    uint32_t ccp_entry = xip_addr(sec);

    if (ccp_entry != 0)
    {
        if ((uintptr_t)ccp_entry + (uintptr_t)nsecs * DISK_SECTOR_SIZE <= (uintptr_t)__xip_top)
        {
            JUMP(ccp_entry);

            for (;;)
                ;
        }

        goto err;
    }

    for (uint16_t i = 0; i < nsecs; i++)

        if (bios_read(sec + i, (void *)((uintptr_t)__tpa_base + i * DISK_SECTOR_SIZE)))
            goto err;

    JUMP(__tpa_base);

err:
    puts("  CCP ERR");

    while (1)
        ;
}

int sys_open(const char *name, uint8_t writable)
{
    FsContext ctx = parse_prefix(&name);
    char n83[12];
    int err = make_name83(name, n83);

    if (err != EOK)
        return err;

    int rc = bd_open(n83, ctx, writable);
    return (rc < 0) ? rc : rc + FD_FILE_BASE;
}

int sys_read(int fd, void *buf, uint32_t len)
{
    uint8_t *p = buf;

    if (fd_is_stdin(fd))
    {
        if (len == 0)
            return bios_constat();

        uint32_t i = 0;

        while (i < len)
        {
            p[i++] = (uint8_t)bios_conin();
        }

        return (int)i;
    }

    int kfd = resolve_file_fd(fd);
    uint32_t total = 0;

    while (total < len)
    {
        uint16_t chunk = (len - total > 0xFFFFu) ? 0xFFFFu : (uint16_t)(len - total);
        int r = bd_read(kfd, p + total, chunk);

        if (r < 0)
            return total ? (int)total : r;

        total += (uint32_t)r;

        if ((uint16_t)r < chunk)
            break;
    }

    return (int)total;
}

int sys_write(int fd, const void *buf, uint32_t len)
{
    const uint8_t *p = buf;

    if (fd_is_console(fd))
    {
        for (uint32_t i = 0; i < len; i++)
        {
            bios_conout((char)p[i]);
        }

        return (int)len;
    }

    int kfd = resolve_file_fd(fd);
    uint32_t total = 0;

    while (total < len)
    {
        uint16_t chunk = (len - total > 0xFFFFu) ? 0xFFFFu : (uint16_t)(len - total);
        int w = bd_write(kfd, p + total, chunk);

        if (w < 0)
            return total ? (int)total : w;

        total += (uint32_t)w;

        if ((uint16_t)w < chunk)
            break;
    }

    return (int)total;
}

int sys_close(int fd)
{
    if (!fd_is_console(fd))
        return bd_close(resolve_file_fd(fd));
    return 0;
}

void sys_exit(int rc)
{
    g_kstate.kenv.env[ENV_RETURN_CODE] = (uint32_t)rc;
    bd_sync();
    kexec_ccp();
}

int sys_args(ArgBlock *out)
{
    memcpy(out, &g_kstate.args, sizeof(ArgBlock));
    return g_kstate.args.argc;
}

int sys_findfile(const char *pattern, FileInfo *out, uint16_t start_pos)
{
    if (!pattern)
        return EOK;

    FsContext ctx = parse_prefix(&pattern);
    char n83[12];
    int err = make_name83(pattern, n83);

    if (err != EOK)
        return err;

    return bd_find(n83, ctx, out, start_pos);
}

uint32_t sys_getsize(int fd)
{
    return bd_size(resolve_file_fd(fd));
}

int sys_create(const char *name)
{
    FsContext ctx = parse_prefix(&name);
    char n83[12];
    int err = make_name83(name, n83);

    if (err != EOK)
        return err;

    int rc = bd_create(n83, ctx);
    return (rc < 0) ? rc : rc + FD_FILE_BASE;
}

int sys_delete(const char *name)
{
    FsContext ctx = parse_prefix(&name);
    char n83[12];
    int err = make_name83(name, n83);

    if (err != EOK)
        return err;

    return bd_delete(n83, ctx);
}

int sys_rename(const char *old, const char *new)
{
    FsContext octx = parse_prefix(&old);
    FsContext nctx = parse_prefix(&new);

    /* Renames are only meaningful within one volume and user area. */

    if (octx.vol_id != nctx.vol_id || octx.user_area != nctx.user_area)
        return EINVAL;

    char old83[12], new83[12];
    int err = make_name83(old, old83);

    if (err != EOK)
        return err;

    err = make_name83(new, new83);

    if (err != EOK)
        return err;

    return bd_rename(old83, new83, octx);
}

int sys_seek(int fd, uint32_t offset)
{
    if (fd_is_console(fd))
        return EINVAL;

    return bd_seek(resolve_file_fd(fd), offset);
}

int sys_vstat(int8_t vol_id, VolStat *stat)
{
    return bd_vstat(vol_id, stat);
}

/*
 * sys_exec — execute a program.  If the name has no extension,
 * ".COM" is appended automatically.
 */
int sys_exec(const char *name, int argc, char **argv)
{
    FsContext ctx = parse_prefix(&name);

    char n83[FILENAME_MAX];
    int err = make_name83(name, n83);

    if (err != EOK)
        return err;

    if (n83[8] == ' ')
    {
        n83[8] = 'C';
        n83[9] = 'O';
        n83[10] = 'M';
    }

    return kexec(n83, argc, argv, ctx);
}

/*
 * sys_dev — memory-mapped I/O for hardware register access.
 * Validates alignment and range before dereferencing the volatile pointer.
 */
int sys_dev(uint32_t reg, uint32_t cmd, uint32_t *data)
{
    if (!data)
        return EINVAL;

    /* Check the sum's parts first: reg + delta must not wrap around. */
    uint32_t delta = cmd & IOCTL_OFF_MASK;

    if (reg > (uint32_t)IO_SIZE - 4 || delta > (uint32_t)IO_SIZE - 4 - reg ||
        ((reg + delta) & 3) != 0)
        return EINVAL;

    uint32_t off = reg + delta;

    volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)((uintptr_t)__io_base + off);

    if (cmd & IOCTL_WRITE_FLAG)
        *p = *data;
    else
        *data = *p;

    return EOK;
}

int sys_fsetattr(const char *name, uint8_t attrib)
{
    FsContext ctx = parse_prefix(&name);
    char n83[12];
    int err = make_name83(name, n83);

    if (err != EOK)
        return err;

    return bd_fsetattr(n83, ctx, attrib);
}

int sys_vsetattr(int8_t vol_id, uint8_t attr)
{
    return bd_vsetattr(vol_id, attr);
}

int sys_mount(int8_t vol_id)
{
    if (vol_id < 0 || vol_id >= VOL_MAX)
        return EINVAL;

    return bd_mount(vol_id);
}

int sys_resize(int8_t vol_id, int16_t delta)
{
    if (vol_id < 0 || vol_id >= VOL_MAX)
        return EINVAL;

    return bd_resize(vol_id, delta);
}

int sys_unmount(int8_t vol_id)
{
    if (vol_id < 0 || vol_id >= VOL_MAX)
        return EINVAL;

    return bd_unbind(vol_id);
}

int sys_info(SysInfo *out)
{
    uint8_t s0[DISK_SECTOR_SIZE];

    if (bios_read(0, s0) != 0)
        return EIO;

    out->os_version = read16(&s0[S0_OS_VER]);
    out->kern_version = read16(&s0[S0_KERN_VER]);
    out->ccp_version = read16(&s0[S0_CCP_VER]);

    memcpy(out->platform, &s0[S0_PLATFORM], 8);
    out->platform[8] = '\0';
    strupr(out->platform);

    out->tpa = ((uint32_t)__kernel_base - (uintptr_t)__tpa_base) / 1024;

    for (int8_t v = 0; v < VOL_MAX; v++)
        out->vol_mounted[v] = (volume_run_count((int8_t)v) > 0) ? 1 : 0;

    out->disk_size_kb = disk_block_count();
    out->disk_unalloc_kb = disk_free_blocks();
    out->xip = disk_xip();

    return EOK;
}

int sys_getctx(FsContext *out)
{
    *out = g_kstate.fs_ctx;

    return EOK;
}

/*
 * sys_setctx — switch the current volume/user context.
 * If the volume changes, auto-binds the new volume first (so the CCP
 * doesn't need to issue a separate bind call).
 */
int sys_setctx(FsContext ctx)
{
    if (ctx.vol_id >= VOL_MAX)
        return EINVAL;

    if (ctx.vol_id != g_kstate.fs_ctx.vol_id)
    {
        int rc = bd_bind(ctx.vol_id);

        if (rc != EOK)
            return rc;
    }

    g_kstate.fs_ctx = ctx;

    return EOK;
}

uint32_t sys_getenv(uint8_t slot)
{
    if (slot >= ENV_SLOTS_MAX)
        return UINT_MAX;

    return g_kstate.kenv.env[slot];
}

/*
 * sys_setenv — write to a kernel environment slot.
 * ENV_RETURN_CODE and ENV_BATCH_OFFSET may only be written by the CCP
 * (gated by the is_ccp flag) so transient programs cannot hijack
 * batch control or fake a return code.
 */
int sys_setenv(uint8_t slot, uint32_t value)
{
    if (slot >= ENV_SLOTS_MAX)
        return -1;

    if (slot == ENV_RETURN_CODE && !g_kstate.kenv.is_ccp)
        return -1;

    if (slot == ENV_BATCH_OFFSET && !g_kstate.kenv.is_ccp)
        return -1;

    g_kstate.kenv.env[slot] = value;

    return 0;
}

int sys_sync(void)
{
    return bd_sync();
}

uint32_t sys_time(void)
{
    return bios_time();
}

int sys_consize(uint8_t *cw, uint8_t *ch)
{
    bios_consize(cw, ch);
    return 0;
}
