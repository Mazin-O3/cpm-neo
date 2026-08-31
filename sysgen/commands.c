#include "commands.h"
#include "bdos.h"
#include "disk.h"
#include "kernel_abi.h"
#include "sysgen.h"
#include "utils.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define OS_VER 0x0100
#define KERN_VER 0x0100
#define CCP_VER 0x0100

/*
 * sysgen/commands.c — host-side SYSGEN command implementations
 *
 * Implements the `sysgen` CLI subcommands: new, add, install, extract,
 * dir, type, era, ren, stat.  These operate on a raw disk image using
 * the bd_* functions (the BDOS layer is compiled into the host tool
 * directly, not via syscalls).
 *
 * The file is divided into:
 *   1. Data structures & forward declarations
 *   2. CLI option parsers & target context
 *   3. Internal helpers (add_data, add_file, install_bundled, etc.)
 *   4. Public cmd_* entry points
 */

/* ========================================================================= *
 * Data Structures & Types
 * ========================================================================= */

/* Options for adding a file to the disk image. */
typedef struct
{
    int vol;
    int user;
    uint8_t attr;
    const char *verb;
} AddFileOpts;

/* Output paths for a compiled .COM file and the platform string. */
typedef struct
{
    char *out_com;
    size_t out_n;
    char *platform;
    size_t platform_n;
} BuildFolderOpts;

/* Walk-state for recursively installing bundled apps. */
typedef struct
{
    const SysgenPaths *paths;
    const AddFileOpts *opts;
    int failed;
} BundledScan;

/* Walk-state for flat-folder add; counts added/skipped files. */
typedef struct
{
    const AddFileOpts *opts;
    int failed;
    int added;
    int skipped;
} AddFolderScan;

/* Parsed arguments for the `sysgen new` command. */
typedef struct
{
    long disk_size_kb;
    const char *platform;
    bool no_extra;
} CmdNewConfig;

/* Disk image path + filesystem context for host-side operations. */
typedef struct
{
    char disk_path[SYSGEN_FULL_PATH_MAX];
    FsContext ctx;
} ImageTarget;

/* ========================================================================= *
 * Forward Declarations & Utility Helpers
 * ========================================================================= */

static int add_data_open(const char *file, const AddFileOpts *opts);
static int add_file(const char *disk, const char *file, const AddFileOpts *opts);
static int build_folder_com(const SysgenPaths *paths, const char *folder,
                            const BuildFolderOpts *opts);
static int install_bundled_app(const char *dir, const char *name, void *ud);
static int install_bundled_source(const char *path, const char *name, void *ud);
static int install_sys_apps(const SysgenPaths *paths, const AddFileOpts *opts);
static int install_extra_apps(const SysgenPaths *paths, const AddFileOpts *opts);

/* Handles both POSIX and Windows path separators; returns a pointer
 * into the original string (no allocation). */
static const char *extract_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *base = path;

    if (slash && slash >= base)
        base = slash + 1;

    if (backslash && backslash >= base)
        base = backslash + 1;

    return base;
}

static void n83_dot(const char *n83, char *out, size_t n)
{
    int nb = NAME83_BASE;
    int ne = NAME83_EXT;
    int o = 0;

    while (nb > 0 && n83[nb - 1] == ' ')
        nb--;

    while (ne > 0 && n83[NAME83_BASE + ne - 1] == ' ')
        ne--;

    if (nb > 0 && nb < (int)n)
    {
        memcpy(out, n83, (size_t)nb);
        o = nb;
    }

    if (ne > 0 && o + 1 + ne < (int)n)
    {
        out[o++] = '.';
        memcpy(out + o, n83 + NAME83_BASE, (size_t)ne);
        o += ne;
    }

    out[o] = '\0';
}

static uint32_t get_file_size(const char *path)
{
    uint8_t *tmp = NULL;
    uint32_t size = 0;

    if (read_file(path, &tmp, &size) == 0)
        free(tmp);
    return size;
}

/* Read a build tag (file named 'name' in the build dir, stamped by
 * build_disk.sh) into buf.  Returns 0 on success, -1 if the tag is missing
 * or unreadable. */
static int read_build_tag(const SysgenPaths *paths, const char *name,
                          char *buf, size_t n)
{
    char path[SYSGEN_FULL_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", paths->build_dir, name);

    FILE *f = fopen(path, "r");

    if (!f)
        return -1;

    buf[0] = '\0';
    int rc = 0;

    if (n > 1 && fgets(buf, (int)n, f) == NULL)
        rc = -1;

    fclose(f);

    buf[strcspn(buf, "\r\n")] = '\0';

    return rc;
}

/* Extract the ISA variant, i.e. the -march=... value, from an ARCH_CFLAGS
 * string like "-march=rv32im -mabi=ilp32".  Writes the token into out.
 * Returns 0 on success, -1 if no -march= is present. */
static int isa_variant_from_flags(const char *flags, char *out, size_t n)
{
    const char *p = flags;

    while ((p = strstr(p, "-march=")) != NULL)
    {
        p += (int)strlen("-march=");

        const char *end = strchr(p, ' ');

        if (!end)
            end = p + strlen(p);

        size_t len = (size_t)(end - p);

        if (len > 0 && len < n)
        {
            memcpy(out, p, len);
            out[len] = '\0';
            return 0;
        }
    }

    return -1;
}

/* ========================================================================= *
 * CLI Option Parsers & Target Context
 * ========================================================================= */

static int parse_dst(int argc, char **argv, int *vol, int *user)
{
    int seen = 0;
    const char *v = flag_value(argc, argv, "--dst", &seen);

    if (!seen || !v || !*v)
    {
        *vol = VOL_A;
        *user = 0;
        return 0;
    }

    if (parse_vn(v, vol, user) != 0)
    {
        err("invalid --dst '%s' (expected Vn, e.g. A0, B7)", v);
        return 1;
    }

    return 0;
}

/*
 * Parse --attr (RO, RW, SYS, or combinations).  When the flag is
 * omitted, |attr| is set to |dflt| instead of defaulting to RW.
 * Returns 0 on success.
 */
static int parse_attr_dflt(int argc, char **argv, uint8_t *attr, uint8_t dflt)
{
    int seen = 0;
    const char *v = flag_value(argc, argv, "--attr", &seen);
    *attr = 0;

    if (!seen || !v || !*v)
    {
        *attr = dflt;
        return 0;
    }

    char buf[32];
    strncpy(buf, v, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    for (char *tok = strtok(buf, "+,"); tok; tok = strtok(NULL, "+,"))
    {
        if (strcasecmp(tok, "RW") == 0 || strcasecmp(tok, "R/W") == 0)
            continue;

        if (strcasecmp(tok, "RO") == 0 || strcasecmp(tok, "R/O") == 0)
            *attr |= FILE_ATTR_READ_ONLY;
        else if (strcasecmp(tok, "SYS") == 0)
            *attr |= FILE_ATTR_SYSTEM;
        else
        {
            err("invalid --attr '%s' (expected RO, RW, SYS, or a SYS+RO combination)", v);
            return 1;
        }
    }

    return 0;
}

static int setup_disk_target(int argc, char **argv, const char *vn_arg, ImageTarget *tgt)
{
    const char *disk = resolve_disk(argc, argv, tgt->disk_path, sizeof(tgt->disk_path));

    int vol = VOL_A, user = 0;

    if (vn_arg && parse_vn(vn_arg, &vol, &user) != 0)
    {
        err("invalid volume '%s'", vn_arg);
        return 1;
    }

    if (open_disk(disk) != 0 || disk_init() != 0 || mount_vol((int8_t)vol) != 0)
        return 1;

    tgt->ctx.vol_id = (int8_t)vol;
    tgt->ctx.user_area = (uint8_t)user;
    return 0;
}

/* ========================================================================= *
 * System Generation & Build Logic
 * ========================================================================= */

static int run_build_script(const SysgenPaths *paths, const char *platform)
{
    char script[SYSGEN_FULL_PATH_MAX];
    snprintf(script, sizeof(script), "%s/../build_disk.sh", paths->build_dir);

    char plat_flag[64];
    snprintf(plat_flag, sizeof(plat_flag), "--platform=%s", platform);

    char *argv[] = {
        (char *)"sh", script, plat_flag, NULL,
    };

    printf("Starting disk build for %s\n", platform);

    return spawn_and_wait(argv);
}

static int disk_size_over_cap(long size_kb, uint32_t ksz, uint32_t ccpsz)
{
    int max_kb = mkdisk_max_size_kb(ksz, ccpsz);

    if (size_kb <= max_kb)
        return 0;

    err("--disk-size %ldK exceeds the %dK maximum useful disk size", size_kb, max_kb);
    return 1;
}

static void report_build(const SysgenPaths *paths, uint32_t size_kb,
                         uint32_t boot_size, uint32_t kern_size,
                         uint32_t ccp_size, uint32_t kern_load, uint32_t tpa_base,
                         uint32_t reserved, const char *out_disk_path)
{
    const uint8_t *vmap = sysgen_disk() + (uint32_t)VMAP_LBA * DISK_SECTOR_SIZE;
    char tmp[32];

    printf("\n=============================================================\n");
    printf("  CP/M Neo Disk Build Report\n");
    printf("=============================================================\n");
    printf("  Output file      : %s\n", out_disk_path);

    hr(tmp, sizeof(tmp), size_kb * 1024);
    printf("  Image size       : %s\n", tmp);

    char arch[64];
    char flags[256];

    if (read_build_tag(paths, ".arch", arch, sizeof(arch)) == 0)
    {
        if (read_build_tag(paths, ".archflags", flags, sizeof(flags)) != 0 ||
            isa_variant_from_flags(flags, tmp, sizeof(tmp)) != 0)
            tmp[0] = '\0';

        printf("  Architecture     : %s", arch);

        if (tmp[0] != '\0')
            printf(" (%s)", tmp);

        printf("\n");
    }

    printf("-------------------------------------------------------------\n");

    printf("  Bootloader size  : %u B\n", boot_size);
    printf("  Kernel size      : %u B\n", kern_size);
    printf("  CCP size         : %u B\n", ccp_size);
    printf("  Kernel base      : 0x%04X\n", kern_load);
    printf("  TPA              : %lu KB\n", (unsigned long)((kern_load - tpa_base) / 1024));
    printf("  Reserved secs    : %u (kernel + CCP)\n", reserved);
    printf("  Kernel LBA       : %u\n", read16(sysgen_disk() + S0_KERN_LBA));

    printf("  Block size       : 1 KB\n");
    printf("  Blocks           : %u @ LBA %u\n", read16(vmap + VMAP_NUM_BLOCKS),
           read16(vmap + VMAP_BLOCK_BASE));

    printf("-------------------------------------------------------------\n");

    uint16_t block_base = read16(vmap + VMAP_BLOCK_BASE);
    uint16_t disk_usable_kb = 0;

    for (int v = 0; v < VOL_MAX; v++)
    {
        const uint8_t *vr = vmap + VMAP_VOLREC + v * VMAP_VOLREC_SIZE;
        const char *mode = (vr[VMAP_VR_ATTR] & VOL_ATTR_RO) ? "RO" : "RW";

        uint32_t start = read16(vr + VMAP_VR_EXT0_START);

        /* Usable capacity mirrors bd_vstat: data blocks from the volume header,
         * minus the reserved sentinel block. */
        uint16_t tot_blks = read16(
            sysgen_disk() + ((uint32_t)(block_base + start * BD_BLOCK_SECS)) * DISK_SECTOR_SIZE +
            VHDR_TOT_BLKS_OFF);
        uint32_t usable = tot_blks > 0 ? (uint32_t)(tot_blks - 1) : 0;
        disk_usable_kb += usable;

        hr(tmp, sizeof(tmp), usable * BD_BLOCK_SECS * DISK_SECTOR_SIZE);
        printf("  %c: %s, Usable: %s\n", 'A' + v, mode, tmp);
    }

    printf("\n  Total usable: %uk\n", disk_usable_kb);
    printf("=============================================================\n\n");
}

static const char *get_str_flag(int argc, char **argv, const char *flag_name)
{
    int is_present = 0;
    const char *value = flag_value(argc, argv, flag_name, &is_present);
    return (is_present && value && *value != '\0') ? value : NULL;
}

static bool get_bool_flag(int argc, char **argv, const char *flag_name)
{
    int is_present = 0;
    flag_value(argc, argv, flag_name, &is_present);
    return is_present != 0;
}

/* Whitelists of flags accepted by each command (NULL-terminated). */
static const char *const FLAGS_NEW[] = {
    "--disk-size", "--platform", "--no-extra", NULL,
};
static const char *const FLAGS_FILE[] = {
    "--dst",
    "--attr",
    "--disk",
    NULL,
};
static const char *const FLAGS_INSTALL[] = {
    "--dst", "--attr", "--disk", "--sys-apps", "--extra-apps", NULL,
};
static const char *const FLAGS_DISK[] = {
    "--disk",
    NULL,
};

static int check_flags(int argc, char **argv, const char *const *allowed)
{
    return reject_unknown_flags(argc, argv, allowed);
}

static int check_positionals(int argc, char **argv, int min_pos, int max_pos)
{
    const char *pos[8];
    int n = collect_positional(argc, argv, pos, 8);

    if (n < min_pos || n > max_pos)
    {
        err("unexpected arguments");
        return 1;
    }

    return 0;
}

static bool parse_cmd_new_args(int argc, char **argv, CmdNewConfig *cfg)
{
    const char *disk_size_str = get_str_flag(argc, argv, "--disk-size");
    const char *platform_str = get_str_flag(argc, argv, "--platform");

    cfg->no_extra = get_bool_flag(argc, argv, "--no-extra");

    cfg->disk_size_kb = 0; /* 0 means "unset"; resolved to max after build */

    if (!platform_str)
    {
        err("--platform required");
        return false;
    }

    cfg->platform = platform_str;

    if (disk_size_str)
    {
        cfg->disk_size_kb = parse_sized_kb(disk_size_str);

        if (cfg->disk_size_kb <= 0)
        {
            err("invalid --disk-size '%s' (K suffix required)", disk_size_str);
            return false;
        }
    }

    return true;
}

static bool validate_build_env(const SysgenPaths *paths)
{
    char path_buf[SYSGEN_FULL_PATH_MAX];

    snprintf(path_buf, sizeof(path_buf), "%s/core/kernel/bdos.c", paths->root_dir);

    if (!file_exists(path_buf))
    {
        err("Cannot locate CP/M Neo root directory at '%s'", paths->root_dir);
        return false;
    }

    return true;
}

/*
 * cmd_new — create a fresh disk image.
 * Runs the build script (make or cmake), reads kernel/CCP/bootloader
 * binaries, calls mkdisk_build(), then installs bundled apps.
 */
int cmd_new(int argc, char **argv)
{
    if (check_flags(argc, argv, FLAGS_NEW) != 0 || check_positionals(argc, argv, 1, 1) != 0)
        return 1;

    CmdNewConfig cfg;

    if (!parse_cmd_new_args(argc, argv, &cfg))
        return 1;

    const SysgenPaths *paths = sysgen_paths();

    if (!validate_build_env(paths))
        return 1;

    char path_buf[SYSGEN_FULL_PATH_MAX];
    snprintf(path_buf, sizeof(path_buf), "%s/core/int/kernel.bin", paths->build_dir);
    uint32_t pk = get_file_size(path_buf);

    snprintf(path_buf, sizeof(path_buf), "%s/core/int/ccp.bin", paths->build_dir);
    uint32_t pc = get_file_size(path_buf);

    if (cfg.disk_size_kb > 0 && pk > 0 && pc > 0 &&
        disk_size_over_cap(cfg.disk_size_kb, pk, pc) != 0)
        return 1;

    if (run_build_script(paths, cfg.platform) != 0)
        return 1;

    char os_platform_buf[16];
    const char *os_platform = cfg.platform;

    if (read_build_tag(paths, ".platform_id", os_platform_buf, sizeof(os_platform_buf)) == 0)
        os_platform = os_platform_buf;

    int ret = 1;
    uint8_t *kern = NULL, *elf = NULL, *ccp = NULL;
    uint32_t ksz = 0, esz = 0, ccpsz = 0, bsz = 0;
    uint32_t kern_load = 0, tpa_base = 0;

    /* Bootloader */
    snprintf(path_buf, sizeof(path_buf), "%s/bootloader.bin", paths->build_dir);
    bsz = get_file_size(path_buf);

    if (bsz == 0)
    {
        err("%s missing", path_buf);
        goto cleanup;
    }

    /* Kernel image */
    snprintf(path_buf, sizeof(path_buf), "%s/core/int/kernel.bin", paths->build_dir);

    if (read_file(path_buf, &kern, &ksz) != 0)
    {
        err("%s missing", path_buf);
        goto cleanup;
    }

    /* Kernel ELF (for link-time symbols) */
    snprintf(path_buf, sizeof(path_buf), "%s/core/int/kernel.elf", paths->build_dir);

    if (read_file(path_buf, &elf, &esz) != 0)
    {
        err("%s missing", path_buf);
        goto cleanup;
    }

    if (elf32_symbol(elf, esz, "__kernel_base", &kern_load) != 0)
    {
        err("cannot find __kernel_base");
        goto cleanup;
    }

    if (elf32_symbol(elf, esz, "__tpa_base", &tpa_base) != 0)
    {
        err("cannot find __tpa_base");
        goto cleanup;
    }

    free(elf);
    elf = NULL;

    /* CCP image (optional) */
    snprintf(path_buf, sizeof(path_buf), "%s/core/int/ccp.bin", paths->build_dir);

    if (read_file(path_buf, &ccp, &ccpsz) != 0)
    {
        ccp = NULL;
        ccpsz = 0;
    }

    /* Size the disk image */
    int min_kb = mkdisk_min_size_kb(ksz, ccpsz);

    if (cfg.disk_size_kb == 0)
        cfg.disk_size_kb = mkdisk_max_size_kb(ksz, ccpsz);

    if (disk_size_over_cap(cfg.disk_size_kb, ksz, ccpsz) != 0)
        goto cleanup;

    int reserved = mkdisk_build((uint32_t)cfg.disk_size_kb, kern, ksz, ccp, ccpsz, kern_load,
                                OS_VER, KERN_VER, CCP_VER, os_platform);

    if (reserved < 0)
    {
        err("mkdisk_build failed: --disk-size %ldK is too small (minimum %dK for all %d volumes)",
            cfg.disk_size_kb, min_kb, VOL_MAX);
        goto cleanup;
    }

    if (disk_init() != 0)
    {
        err("disk_init failed");
        goto cleanup;
    }

    if (bd_bind(VOL_A) != EOK)
    {
        err("cannot mount A:");
        goto cleanup;
    }

    char apps_dir[SYSGEN_FULL_PATH_MAX];
    snprintf(apps_dir, sizeof(apps_dir), "%s/apps", paths->root_dir);

    if (!dir_exists(apps_dir))
    {
        err("apps directory not found at '%s'", apps_dir);
        goto cleanup;
    }

    AddFileOpts sys_opts = {VOL_A, 0, FILE_ATTR_SYSTEM | FILE_ATTR_READ_ONLY, "installed"};
    AddFileOpts extra_opts = {VOL_A, 0, FILE_ATTR_READ_ONLY, "installed"};

    if (install_sys_apps(paths, &sys_opts) != 0)
        goto cleanup;

    if (!cfg.no_extra && install_extra_apps(paths, &extra_opts) != 0)
        goto cleanup;

    bd_sync();

    char out_disk_buf[SYSGEN_FULL_PATH_MAX];
    sysgen_default_disk(out_disk_buf, sizeof(out_disk_buf));
    const char *out_disk_path = out_disk_buf;

    if (save_disk(out_disk_path) != 0)
        goto cleanup;

    report_build(paths, (uint32_t)cfg.disk_size_kb, bsz, ksz, ccpsz, kern_load,
                 tpa_base, (uint32_t)reserved, out_disk_path);
    ret = 0;

cleanup:
    free(kern);
    free(elf);
    free(ccp);
    return ret;
}

static int build_folder_com(const SysgenPaths *paths, const char *src, const BuildFolderOpts *opts)
{
    char platform_path[SYSGEN_FULL_PATH_MAX];
    snprintf(platform_path, sizeof(platform_path), "%s/.platform_dir", paths->build_dir);

    FILE *f = fopen(platform_path, "r");

    if (!f)
    {
        err("no system build found in '%s' -- run 'sysgen new' first", paths->build_dir);
        return 1;
    }

    if (fgets(opts->platform, opts->platform_n, f) == NULL)
        opts->platform[0] = '\0';
    fclose(f);

    opts->platform[strcspn(opts->platform, "\r\n")] = '\0';

    if (!*opts->platform)
    {
        err("missing platform record '%s' -- re-run 'sysgen new'", platform_path);
        return 1;
    }

    size_t len = strlen(src);

    while (len > 0 && (src[len - 1] == '/' || src[len - 1] == '\\'))
        len--;

    if (len == 0 || len >= SYSGEN_FULL_PATH_MAX)
    {
        err("invalid or path too long: '%s'", src);
        return 1;
    }

    char dirbuf[SYSGEN_FULL_PATH_MAX];
    memcpy(dirbuf, src, len);
    dirbuf[len] = '\0';

    /* App name: folder basename, or a single source file's basename minus
     * its extension (mycmd.c -> mycmd.com -> MYCMD.COM). */
    const char *base;
    char basebuf[SYSGEN_FULL_PATH_MAX];

    if (!dir_exists(dirbuf))
    {
        snprintf(basebuf, sizeof(basebuf), "%s", extract_basename(dirbuf));
        char *dot = strrchr(basebuf, '.');

        if (dot && dot != basebuf)
            *dot = '\0';
        base = basebuf;
    }
    else
    {
        base = extract_basename(dirbuf);
    }

    if (!*base)
    {
        err("cannot derive app name from '%s'", src);
        return 1;
    }

    snprintf(opts->out_com, opts->out_n, "%s/apps/com/%s.com", paths->build_dir, base);

    char script[SYSGEN_FULL_PATH_MAX];
    snprintf(script, sizeof(script), "%s/../app_build.sh", paths->build_dir);

    char *argv_run[] = {
        (char *)"sh", script, opts->platform, dirbuf, (char *)"-o", opts->out_com, NULL,
    };

    return spawn_and_wait(argv_run);
}

/* Build |src| (a folder or a single source file) to a .com and add it to
 * the disk.  Returns 1 on failure (also marks the scan failed). */
static int build_and_add(BundledScan *scan, const char *src)
{
    char out_com[SYSGEN_FULL_PATH_MAX + 256];
    char platform_buf[64];
    BuildFolderOpts bfo = {out_com, sizeof(out_com), platform_buf, sizeof(platform_buf)};

    if (build_folder_com(scan->paths, src, &bfo) != 0)
    {
        scan->failed = 1;
        return 1;
    }

    if (add_data_open(out_com, scan->opts) == 1)
    {
        scan->failed = 1;
        return 1;
    }

    return 0;
}

static int install_bundled_app(const char *dir, const char *name, void *ud)
{
    BundledScan *scan = ud;

    if (scan->failed)
        return 1;

    if (!dir_has_sources(dir))
    {
        printf("  Skip %s (no .c/.s/.S sources)\n", name);
        return 0;
    }

    return build_and_add(scan, dir);
}

static int install_bundled_source(const char *path, const char *name, void *ud)
{
    BundledScan *scan = ud;
    (void)name;

    if (scan->failed)
        return 1;

    return build_and_add(scan, path);
}

static int install_sys_apps(const SysgenPaths *paths, const AddFileOpts *opts)
{
    BundledScan scan = {paths, opts, 0};
    char sys_dir[SYSGEN_FULL_PATH_MAX];
    snprintf(sys_dir, sizeof(sys_dir), "%s/apps/sys", paths->root_dir);

    if (!dir_exists(sys_dir))
    {
        err("sys apps directory not found at '%s'", sys_dir);
        return 1;
    }

    printf("  \nInstalling sys apps...\n");

    for_each_source_file(sys_dir, install_bundled_source, &scan);

    return scan.failed ? 1 : 0;
}

static int install_extra_apps(const SysgenPaths *paths, const AddFileOpts *opts)
{
    BundledScan scan = {paths, opts, 0};
    char extra_dir[SYSGEN_FULL_PATH_MAX];
    snprintf(extra_dir, sizeof(extra_dir), "%s/apps/extra", paths->root_dir);

    if (!dir_exists(extra_dir))
        return 0;

    printf("  \nInstalling extra apps...\n");

    for_each_subdir(extra_dir, install_bundled_app, &scan);

    return scan.failed ? 1 : 0;
}

/* ========================================================================= *
 * Disk & File Operations (Commands)
 * ========================================================================= */

static int add_data_open(const char *file, const AddFileOpts *opts)
{
    uint8_t *data = NULL;
    uint32_t len = 0;

    if (read_file(file, &data, &len) != 0)
    {
        err("cannot read '%s'", file);
        return 1;
    }

    const char *base = extract_basename(file);
    char n83[NAME83_LEN + 1];
    to_name83(base, n83);
    n83[NAME83_LEN] = '\0';

    FsContext ctx = {(int8_t)opts->vol, (uint8_t)opts->user};

    char dot[NAME83_LEN + 2];
    n83_dot(n83, dot, sizeof(dot));

    /* Skip files that already exist on the destination volume/user area. */
    FileInfo fi;

    if (bd_find(n83, ctx, &fi, 0) > 0)
    {
        free(data);
        printf("  %s %-13s -> %c:%u  (already exists, skipped)\n", opts->verb, dot, 'A' + opts->vol,
               opts->user);
        return 2;
    }

    int fd = bd_create(n83, ctx);

    if (fd < 0)
    {
        free(data);
        err("create %s (%s)", dot, err_str(fd));
        return 1;
    }

    uint32_t off = 0;
    int rc = EOK;

    while (off < len)
    {
        uint16_t chunk = ((len - off) > 1024) ? 1024 : (uint16_t)(len - off);
        rc = bd_write(fd, data + off, chunk);

        if (rc < 0)
            break;

        off += (uint32_t)rc;
    }

    bd_close(fd);

    if (rc >= 0 && opts->attr != 0)
        rc = bd_fsetattr(n83, ctx, opts->attr);

    free(data);

    if (rc < 0)
    {
        err("write %s (%s)", dot, err_str(rc));
        return 1;
    }

    const char *tag = (opts->attr & FILE_ATTR_SYSTEM)      ? "  [SYS]"
                      : (opts->attr & FILE_ATTR_READ_ONLY) ? "  [RO]"
                                                           : "";
    char h[24];
    hr(h, sizeof(h), len);
    printf("  %s %-13s %9s  -> %c:%u%s\n", opts->verb, dot, h, 'A' + opts->vol, opts->user, tag);
    return 0;
}

static int add_folder_file(const char *path, const char *name, void *ud)
{
    AddFolderScan *scan = ud;
    (void)name;

    if (scan->failed)
        return 1;

    int rc = add_data_open(path, scan->opts);

    if (rc == 1)
    {
        scan->failed = 1;
        return 1;
    }

    if (rc == 2)
        scan->skipped++;
    else
        scan->added++;

    return 0;
}

static int add_file(const char *disk, const char *file, const AddFileOpts *opts)
{
    if (open_disk(disk) != 0 || disk_init() != 0 || mount_vol((int8_t)opts->vol) != 0)
        return 1;

    if (add_data_open(file, opts) == 1)
        return 1;

    bd_sync();
    return save_disk(disk);
}

/* Parse the shared add/install target options: the source positional, the
 * destination volume/user area, the file attributes, and the disk path.
 * When --attr is omitted, |dflt_attr| is used. */
static int parse_file_target(int argc, char **argv, const char *usage, const char **src, int *vol,
                             int *user, uint8_t *attr, uint8_t dflt_attr, char *disk_buf,
                             size_t disk_n)
{
    const char *pos[4];

    if (check_flags(argc, argv, FLAGS_FILE) != 0 || check_positionals(argc, argv, 2, 2) != 0)
        return 1;

    if (collect_positional(argc, argv, pos, 4) < 2)
    {
        err("usage: %s", usage);
        return 1;
    }

    *src = pos[1];

    resolve_disk(argc, argv, disk_buf, disk_n);

    if (parse_dst(argc, argv, vol, user) != 0)
        return 1;

    return parse_attr_dflt(argc, argv, attr, dflt_attr);
}

/*
 * cmd_add — add a file or flat folder to the disk image.
 * Supports --dst=Vn for volume/user targeting, --attr for file attributes.
 * Folder mode iterates all files in the folder and skips duplicates.
 */
int cmd_add(int argc, char **argv)
{
    const char *src;
    int vol, user;
    uint8_t attr;
    char disk_buf[SYSGEN_FULL_PATH_MAX];

    if (parse_file_target(
            argc, argv,
            "sysgen add <file|folder> [--dst=Vn] [--attr=RO|RW|SYS|SYS+RO] [--disk=path]", &src,
            &vol, &user, &attr, 0, disk_buf, sizeof(disk_buf)) != 0)
        return 1;

    AddFileOpts afo = {vol, user, attr, "added"};

    /* Folder mode: add every file from a flat folder, skipping duplicates. */

    if (dir_exists(src))
    {
        if (dir_has_subdirs(src))
        {
            err("'%s' contains subdirectories; 'sysgen add' requires a flat folder", src);
            return 1;
        }

        if (open_disk(disk_buf) != 0 || disk_init() != 0 || mount_vol((int8_t)vol) != 0)
            return 1;

        AddFolderScan scan = {&afo, 0, 0, 0};
        for_each_flat_file(src, add_folder_file, &scan);

        if (scan.failed)
            return 1;

        bd_sync();

        if (save_disk(disk_buf) != 0)
            return 1;

        printf("\nAdded %d file(s) to %c:%u (%d already existed)\n", scan.added, 'A' + vol, user,
               scan.skipped);
        return 0;
    }

    if (!file_exists(src))
    {
        err("'%s' not found", src);
        return 1;
    }

    return add_file(disk_buf, src, &afo);
}

/*
 * cmd_install — install bundled apps or user-specified source.
 * --sys-apps / --extra-apps: install pre-built apps from the SDK tree.
 * Otherwise: compile a folder of .c files into .COM and add to the image.
 */
int cmd_install(int argc, char **argv)
{
    int sys_apps = get_bool_flag(argc, argv, "--sys-apps");
    int extra_apps = get_bool_flag(argc, argv, "--extra-apps");

    /* Bundled-app mode: install the sys/extra apps from the SDK tree. */

    if (sys_apps || extra_apps)
    {
        if (check_flags(argc, argv, FLAGS_INSTALL) != 0 || check_positionals(argc, argv, 1, 1) != 0)
            return 1;

        char disk_buf[SYSGEN_FULL_PATH_MAX];
        resolve_disk(argc, argv, disk_buf, sizeof(disk_buf));

        int vol, user;

        if (parse_dst(argc, argv, &vol, &user) != 0)
            return 1;

        if (open_disk(disk_buf) != 0 || disk_init() != 0 || mount_vol((int8_t)vol) != 0)
            return 1;

        if (sys_apps)
        {
            uint8_t attr;

            if (parse_attr_dflt(argc, argv, &attr, FILE_ATTR_SYSTEM | FILE_ATTR_READ_ONLY) != 0)
                return 1;

            AddFileOpts afo = {vol, user, attr, "installed"};

            if (install_sys_apps(sysgen_paths(), &afo) != 0)
                return 1;
        }

        if (extra_apps)
        {
            uint8_t attr;

            if (parse_attr_dflt(argc, argv, &attr, FILE_ATTR_READ_ONLY) != 0)
                return 1;

            AddFileOpts afo = {vol, user, attr, "installed"};

            if (install_extra_apps(sysgen_paths(), &afo) != 0)
                return 1;
        }

        bd_sync();
        return save_disk(disk_buf);
    }

    const char *src;
    int vol, user;
    uint8_t attr;
    char disk_buf[SYSGEN_FULL_PATH_MAX];

    if (parse_file_target(
            argc, argv,
            "sysgen install <folder|file.c> [--dst=Vn] [--attr=RO|RW|SYS|SYS+RO] [--disk=path]",
            &src, &vol, &user, &attr, FILE_ATTR_READ_ONLY, disk_buf, sizeof(disk_buf)) != 0)
        return 1;

    if (!dir_exists(src) && !file_exists(src))
    {
        err("'%s' not found; source must be a folder or a .c/.s/.S file", src);
        return 1;
    }

    if (file_exists(src) && !has_source_ext(src))
    {
        err("'%s' is not a .c/.s/.S source file", src);
        return 1;
    }

    char out_com[SYSGEN_PATH_MAX + 256];
    char platform[64];
    BuildFolderOpts bfo = {out_com, sizeof(out_com), platform, sizeof(platform)};

    if (build_folder_com(sysgen_paths(), src, &bfo) != 0)
        return 1;

    AddFileOpts afo = {vol, user, attr, "installed"};
    return add_file(disk_buf, out_com, &afo);
}

/*
 * cmd_extract — extract all files from every volume/user into a flat folder.
 * Works even on damaged images (loads the raw image buffer without
 * validating the boot sector, then uses bd_* for file I/O).
 */
int cmd_extract(int argc, char **argv)
{
    if (check_flags(argc, argv, FLAGS_DISK) != 0 || check_positionals(argc, argv, 1, 1) != 0)
        return 1;

    char disk_buf[SYSGEN_FULL_PATH_MAX];
    resolve_disk(argc, argv, disk_buf, sizeof(disk_buf));

    /* Load the image without validating the boot sector: extraction must work
     * even when the system area is damaged. */
    uint8_t *buf = NULL;
    uint32_t len = 0;

    if (read_file(disk_buf, &buf, &len) != 0)
    {
        err("cannot read '%s'", disk_buf);
        return 1;
    }

    if (len < DISK_SECTOR_SIZE || (len % DISK_SECTOR_SIZE) != 0)
    {
        free(buf);
        err("'%s' is not a valid disk image", disk_buf);
        return 1;
    }

    sysgen_set_disk(buf, len);

    if (disk_init() != 0)
    {
        err("disk_init failed: '%s' has no valid volume map", disk_buf);
        free(buf);
        return 1;
    }

    const SysgenPaths *paths = sysgen_paths();
    char out_dir[SYSGEN_FULL_PATH_MAX];
    snprintf(out_dir, sizeof(out_dir), "%s/extract", paths->build_dir);

    if (mkdir_p(out_dir) != 0)
    {
        err("cannot create extract directory '%s'", out_dir);
        free(buf);
        return 1;
    }

    int total = 0, skipped = 0, errors = 0;

    for (int v = 0; v < VOL_MAX; v++)
    {
        if (bd_bind((int8_t)v) != EOK)
            continue;

        for (uint8_t u = 0; u <= USER_AREA_MAX; u++)
        {
            FsContext ctx = {(int8_t)v, u};
            FileInfo fi;
            char allpat[NAME83_LEN + 1] = "***********"; /* 8 base + 3 ext */
            uint16_t pos = 0;
            int rc;

            while ((rc = bd_find(allpat, ctx, &fi, pos)) > 0)
            {
                pos = (uint16_t)rc;

                char path[SYSGEN_FULL_PATH_MAX + 256];
                snprintf(path, sizeof(path), "%s/%s", out_dir, fi.name);

                if (file_exists(path))
                {
                    printf("  skip %c:%u %s (already extracted)\n", 'A' + v, u, fi.name);
                    skipped++;
                    continue;
                }

                char n83[NAME83_LEN + 1];
                to_name83(fi.name, n83);
                n83[NAME83_LEN] = '\0';

                int fd = bd_open(n83, ctx, 0);

                if (fd < 0)
                {
                    printf("  error opening %c:%u %s (%s)\n", 'A' + v, u, fi.name, err_str(fd));
                    errors++;
                    continue;
                }

                uint8_t *data = NULL;
                uint32_t size = 0, cap = 0;
                uint8_t chunk[1024];
                int r;

                while ((r = bd_read(fd, chunk, sizeof(chunk))) > 0)
                {
                    if (size + (uint32_t)r > cap)
                    {
                        cap = cap ? cap * 2 : 4096;

                        while (cap < size + (uint32_t)r)
                            cap *= 2;
                        data = realloc(data, cap);
                    }

                    memcpy(data + size, chunk, (size_t)r);
                    size += (uint32_t)r;
                }

                bd_close(fd);

                if (r < 0)
                {
                    free(data);
                    printf("  error reading %c:%u %s\n", 'A' + v, u, fi.name);
                    errors++;
                    continue;
                }

                if (write_file(path, data, size) != 0)
                {
                    free(data);
                    printf("  error writing '%s'\n", path);
                    errors++;
                    continue;
                }

                free(data);

                char h[24];
                hr(h, sizeof(h), size);
                printf("  %c:%u %-13s %9s  -> %s\n", 'A' + v, u, fi.name, h, path);
                total++;
            }
        }
    }

    free(buf);
    printf("\nExtracted %d file(s) to '%s' (%d skipped, %d errors)\n", total, out_dir, skipped,
           errors);
    return errors ? 1 : 0;
}

/*
 * cmd_dir — list files on the host-side disk image (like the CCP's DIR
 * but operates on the raw image file).
 */
int cmd_dir(int argc, char **argv)
{
    const char *pos[3];

    if (check_flags(argc, argv, FLAGS_DISK) != 0 || check_positionals(argc, argv, 1, 2) != 0)
        return 1;

    int npos = collect_positional(argc, argv, pos, 3);

    ImageTarget tgt;

    if (setup_disk_target(argc, argv, (npos > 1) ? pos[1] : NULL, &tgt) != 0)
        return 1;

    FileInfo fi;
    uint16_t pos_lba = 0;
    int count = 0;
    int rc = 0;
    char allpat[NAME83_LEN + 1] = "***********"; /* 8 base + 3 ext */

    while ((rc = bd_find(allpat, tgt.ctx, &fi, pos_lba)) > 0)
    {
        const char *sys = (fi.attrib & FILE_ATTR_SYSTEM) ? "  [SYS]" : "";
        char h[24];
        hr(h, sizeof(h), fi.size);
        printf("  %-13s %9s%s\n", fi.name, h, sys);
        count++;
        pos_lba = (uint16_t)rc;
    }

    if (rc != ENOENT)
    {
        err("dir: scan failed (%s)", err_str(rc));
        return 1;
    }

    printf("  %d file(s)\n", count);
    return 0;
}

/*
 * cmd_type — display a file from the disk image on stdout.
 */
int cmd_type(int argc, char **argv)
{
    const char *pos[3];

    if (check_flags(argc, argv, FLAGS_DISK) != 0 || check_positionals(argc, argv, 2, 3) != 0)
        return 1;

    int npos = collect_positional(argc, argv, pos, 3);

    char n83[NAME83_LEN + 1];
    to_name83(pos[1], n83);
    n83[NAME83_LEN] = '\0';

    ImageTarget tgt;

    if (setup_disk_target(argc, argv, (npos > 2) ? pos[2] : NULL, &tgt) != 0)
        return 1;

    int fd = bd_open(n83, tgt.ctx, 0);

    if (fd < 0)
    {
        err("type: %s (%s)", n83, err_str(fd));
        return 1;
    }

    uint8_t buf[DISK_SECTOR_SIZE];
    int rc = 0;

    while ((rc = bd_read(fd, buf, DISK_SECTOR_SIZE)) > 0)
        fwrite(buf, 1, (size_t)rc, stdout);

    bd_close(fd);

    if (rc < 0)
    {
        err("type: read failed (%s)", err_str(rc));
        return 1;
    }

    return 0;
}

/*
 * cmd_era — delete a file from the disk image.
 */
int cmd_era(int argc, char **argv)
{
    const char *pos[3];

    if (check_flags(argc, argv, FLAGS_DISK) != 0 || check_positionals(argc, argv, 2, 3) != 0)
        return 1;

    int npos = collect_positional(argc, argv, pos, 3);

    char n83[NAME83_LEN + 1];
    to_name83(pos[1], n83);
    n83[NAME83_LEN] = '\0';

    ImageTarget tgt;

    if (setup_disk_target(argc, argv, (npos > 2) ? pos[2] : NULL, &tgt) != 0)
        return 1;

    int rc = bd_delete(n83, tgt.ctx);

    if (rc != EOK)
    {
        err("era: %s (%s)", n83, err_str(rc));
        return 1;
    }

    bd_sync();

    if (save_disk(tgt.disk_path) != 0)
        return 1;

    printf("  deleted %s\n", n83);
    return 0;
}

/*
 * cmd_ren — rename a file on the disk image.
 */
int cmd_ren(int argc, char **argv)
{
    const char *pos[4];

    if (check_flags(argc, argv, FLAGS_DISK) != 0 || check_positionals(argc, argv, 3, 4) != 0)
        return 1;

    int npos = collect_positional(argc, argv, pos, 4);

    char old83[NAME83_LEN + 1], new83[NAME83_LEN + 1];
    to_name83(pos[1], old83);
    to_name83(pos[2], new83);
    old83[NAME83_LEN] = '\0';
    new83[NAME83_LEN] = '\0';

    ImageTarget tgt;

    if (setup_disk_target(argc, argv, (npos > 3) ? pos[3] : NULL, &tgt) != 0)
        return 1;

    int rc = bd_rename(old83, new83, tgt.ctx);

    if (rc != EOK)
    {
        err("ren: %s (%s)", old83, err_str(rc));
        return 1;
    }

    bd_sync();

    if (save_disk(tgt.disk_path) != 0)
        return 1;

    printf("  renamed %s -> %s\n", old83, new83);
    return 0;
}

/*
 * cmd_stat — display volume statistics (block count, free space, etc.)
 * for all volumes in the disk image.
 */
int cmd_stat(int argc, char **argv)
{
    if (check_flags(argc, argv, FLAGS_DISK) != 0 || check_positionals(argc, argv, 1, 1) != 0)
        return 1;

    char disk_buf[SYSGEN_FULL_PATH_MAX];
    resolve_disk(argc, argv, disk_buf, sizeof(disk_buf));

    if (open_disk(disk_buf) != 0 || disk_init() != 0)
        return 1;

    printf("  disk: %s\n", disk_buf);
    printf("  block: 1K, blocks: %u, block base: LBA %u\n", disk_blocks(), disk_block_base());

    for (int v = 0; v < VOL_MAX; v++)
    {
        if (disk_vruns((int8_t)v) == 0)
        {
            printf("  %c: unmounted\n", 'A' + v);
            continue;
        }

        if (bd_bind((int8_t)v) != EOK)
        {
            printf("  %c: mount failed\n", 'A' + v);
            continue;
        }

        VolStat st;
        bd_vstat((int8_t)v, &st);
        printf("  %c: %uk total, %uk free, %s\n", 'A' + v, st.total_blocks, st.free_blocks,
               st.read_only ? "RO" : "RW");
    }

    return 0;
}
