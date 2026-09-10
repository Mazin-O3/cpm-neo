/*
 * sysgen/src/cmd_new.c — System generation (& new-disk assembly)
 *
 * `sysgen new <platform> [opts]` runs build_disk.sh for the platform,
 * reads the bootloader/kernel/CCP binaries, calls mkdisk_build() and
 * seeds the fresh image with the bundled apps.
 */

#include "cmd.h"
#include "commands.h"

#include "bdos.h"
#include "disk.h"
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
#define SYSGEN_MAX_APP_NAMES 64

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
static int read_build_tag(const SysgenPaths *paths, const char *name, char *buf, size_t n)
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

static int run_build_script(const SysgenPaths *paths, const char *platform, int want_xip)
{
    char script[SYSGEN_FULL_PATH_MAX];
    snprintf(script, sizeof(script), "%s/../build_disk.sh", paths->build_dir);

    char plat_flag[64];
    snprintf(plat_flag, sizeof(plat_flag), "--platform=%s", platform);

    char *argv[] = {
        (char *)"sh", script, plat_flag, (char *)(want_xip ? "--xip" : NULL), NULL,
    };

    printf("Starting disk build for %s\n", platform);

    return spawn_and_wait(argv);
}

static void report_build(const SysgenPaths *paths, const SysgenDiskCfg *cfg, uint32_t size_kb,
                         uint32_t boot_size, uint32_t kern_size, uint32_t ccp_size,
                         uint32_t kern_load, uint32_t tpa_base, uint32_t reserved,
                         const char *out_disk_path)
{
    const uint8_t *vmap = sysgen_disk() + (uint32_t)VMAP_SEC * DISK_SECTOR_SIZE;
    char           tmp[32];

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

    char xip_buf[8];
    int  have_xip = read_build_tag(paths, ".xip", xip_buf, sizeof(xip_buf)) == 0;

    if (have_xip)
        printf("  XIP              : %s\n", (xip_buf[0] == '1') ? "Yes" : "No");

    if (have_xip && xip_buf[0] == '1')
    {
        char xip_size_buf[32];

        if (read_build_tag(paths, ".xipsize", xip_size_buf, sizeof(xip_size_buf)) == 0)
        {
            hr(tmp, sizeof(tmp), (uint32_t)strtoul(xip_size_buf, NULL, 0));
            printf("  XIP window       : %s\n", tmp);
        }
    }

    printf("-------------------------------------------------------------\n");

    printf("  Bootloader size  : %u B\n", boot_size);
    printf("  Kernel base      : 0x%04X\n", kern_load);
    printf("  Kernel size      : %u B\n", kern_size);
    printf("  CCP size         : %u B\n", ccp_size);
    printf("  TPA              : %lu KB\n", (unsigned long)((kern_load - tpa_base) / 1024));
    printf("  Reserved secs    : %u (kernel + CCP)\n", reserved);
    printf("  Kernel sector    : %u\n", read16(sysgen_disk() + S0_KERN_SEC));

    printf("  Block size       : 1 KB\n");
    printf("  Blocks           : %u @ sector %u\n", read16(vmap + VMAP_NUM_BLOCKS),
           read16(vmap + VMAP_BASE_SEC));

    printf("-------------------------------------------------------------\n");

    uint16_t base_sec = read16(vmap + VMAP_BASE_SEC);
    uint16_t disk_usable_kb = 0;

    for (int8_t v = 0; v < (int8_t)cfg->vol_count; v++)
    {
        const uint8_t *vr = vmap + VMAP_VOLREC + v * VMAP_VOLREC_SIZE;
        const char    *mode = (vr[VMAP_VR_ATTR] & VOL_ATTR_RO) ? "RO" : "RW";

        uint32_t start = read16(vr + VMAP_VR_RUN0_START);

        /* Usable capacity mirrors bd_vstat: data blocks from the volume header,
         * minus the reserved sentinel block. */
        uint16_t tot_blks = read16(
            sysgen_disk() + ((uint32_t)(base_sec + start * BD_BLOCK_SECS)) * DISK_SECTOR_SIZE +
            VHDR_TOT_BLKS_OFF);
        uint32_t usable = tot_blks > 0 ? (uint32_t)(tot_blks - 1) : 0;
        disk_usable_kb += usable;

        hr(tmp, sizeof(tmp), usable * BD_BLOCK_SECS * DISK_SECTOR_SIZE);
        printf("  %c: %s, Usable: %s\n", 'A' + v, mode, tmp);
    }

    printf("\n  Total usable: %uK\n", disk_usable_kb);
    printf("=============================================================\n\n");
}

/* Whitelist of flags accepted by `sysgen new` (NULL-terminated). */
static const char *const FLAGS_NEW[] = {
    "--platform",
    "--xip",
    NULL,
};

/* Split a whitespace-separated name list in-place into out[0..n).  Returns
 * the number of names (0 for an empty string). */
static size_t split_names(char *buf, const char **out, size_t max_out)
{
    size_t n = 0;
    char  *p = buf;

    while (*p)
    {
        while (*p == ' ' || *p == '\t')
            p++;

        if (!*p)
            break;

        if (n >= max_out)
            break;

        out[n++] = p;

        while (*p && *p != ' ' && *p != '\t')
            p++;

        if (*p)
        {
            *p = '\0';
            p++;
        }
    }

    return n;
}

static bool parse_cmd_new_args(int argc, char **argv, const char **platform, int *want_xip)
{
    const char *platform_str = get_str_flag(argc, argv, "--platform");

    if (!platform_str)
    {
        err("--platform required");
        return false;
    }

    *platform = platform_str;
    *want_xip = get_bool_flag(argc, argv, "--xip") ? 1 : 0;

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
 * cmd_new — Create a fresh disk image.
 * Runs the build script (make or cmake), reads kernel/CCP/bootloader
 * binaries, calls mkdisk_build(), then installs bundled apps.
 */
int cmd_new(int argc, char **argv)
{
    if (check_flags(argc, argv, FLAGS_NEW) != 0 || check_positionals(argc, argv, 1, 1) != 0)
        return 1;

    const char *platform;
    int         want_xip = 0;

    if (!parse_cmd_new_args(argc, argv, &platform, &want_xip))
        return 1;

    const SysgenPaths *paths = sysgen_paths();

    if (!validate_build_env(paths))
        return 1;

    char path_buf[SYSGEN_FULL_PATH_MAX];

    if (run_build_script(paths, platform, want_xip) != 0)
        return 1;

    char        os_platform_buf[16];
    const char *os_platform = platform;

    if (read_build_tag(paths, ".platform_id", os_platform_buf, sizeof(os_platform_buf)) == 0)
        os_platform = os_platform_buf;

    char xip_buf[8] = "0";
    read_build_tag(paths, ".xip", xip_buf, sizeof(xip_buf));
    int is_xip = (xip_buf[0] == '1');

    /* The platform's effective disk configuration comes from the build tags
     * build_disk.sh stamps (.vol_max, .disk_size_kb).  The host binary is
     * compiled once at its ceilings, so the active values are validated
     * against those ceilings before use. */
    SysgenDiskCfg dcfg = sysgen_disk_cfg_default();
    char          tag_buf[32];

    if (read_build_tag(paths, ".vol_max", tag_buf, sizeof(tag_buf)) == 0)
    {
        long v = strtol(tag_buf, NULL, 10);

        if (v > 0 && v <= MAX_VOLUMES)
            dcfg.vol_count = (uint16_t)v;
        else
        {
            err("platform '%s' volume count %ld exceeds the host ceiling %d "
                "or is invalid",
                os_platform, v, MAX_VOLUMES);
            return 1;
        }
    }

    if (read_build_tag(paths, ".disk_size_kb", tag_buf, sizeof(tag_buf)) == 0)
    {
        long v = strtol(tag_buf, NULL, 10);

        if (v > 0 && v <= BD_VOL_MAX_BLOCKS)
            dcfg.disk_size_kb = (uint16_t)v;
        else
        {
            err("platform '%s' disk size %ldK exceeds the host ceiling %dK "
                "or is invalid",
                os_platform, v, BD_VOL_MAX_BLOCKS);
            return 1;
        }
    }

    int      ret = 1;
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

    /* Size the disk image exactly as the platform declares: CONFIG_DISK_SIZE
     * (read into dcfg.disk_size_kb above) is the TOTAL image size in KB,
     * overhead included.  mkdisk_build fails if the bootloader, kernel, CCP
     * and every volume's minimum block count cannot fit in that size. */
    int min_kb = mkdisk_min_size_kb(&dcfg, ksz, ccpsz, is_xip);

    /* Record the resolved disk size in bytes for XIP builds: the flash
     * window is the disk image itself (there is no configured XIP_SIZE),
     * so the tag lets `sysgen report` show the window size. */
    if (is_xip)
    {
        char xip_path[SYSGEN_FULL_PATH_MAX];
        snprintf(xip_path, sizeof(xip_path), "%s/.xipsize", paths->build_dir);

        FILE *f = fopen(xip_path, "w");
        if (f == NULL)
        {
            err("cannot write .xipsize tag to %s", xip_path);
            goto cleanup;
        }

        fprintf(f, "%lu", (unsigned long)dcfg.disk_size_kb * 1024);
        fclose(f);
    }

    int reserved = mkdisk_build(&dcfg, dcfg.disk_size_kb, kern, ksz, ccp, ccpsz, kern_load, OS_VER,
                                KERN_VER, CCP_VER, os_platform, is_xip);

    if (reserved < 0)
    {
        err("mkdisk_build failed: CONFIG_DISK_SIZE %uK is too small (minimum %dK for all %d "
            "volumes)",
            dcfg.disk_size_kb, min_kb, dcfg.vol_count);
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

    /* Per-platform app selection: build_disk.sh stamps CONFIG_SYS_APPS /
     * CONFIG_EXTRA_APPS into build/.sys_apps / build/.extra_apps.  For each
     * knob a tag of "*" means install every bundled app, a space-separated
     * list filters to those apps, and an empty or missing tag means install
     * none. */
    char sys_apps_buf[SYSGEN_FULL_PATH_MAX] = "";
    char extra_apps_buf[SYSGEN_FULL_PATH_MAX] = "";

    read_build_tag(paths, ".sys_apps", sys_apps_buf, sizeof(sys_apps_buf));
    read_build_tag(paths, ".extra_apps", extra_apps_buf, sizeof(extra_apps_buf));

    const char *sys_names[SYSGEN_MAX_APP_NAMES];
    const char *extra_names[SYSGEN_MAX_APP_NAMES];
    size_t      sys_n = 0, extra_n = 0;

    if (strcmp(sys_apps_buf, "*") == 0)
    {
        if (install_sys_apps(paths, &sys_opts, NULL, 0) != 0)
            goto cleanup;
    }
    else
    {
        sys_n = split_names(sys_apps_buf, sys_names, SYSGEN_MAX_APP_NAMES);

        if (sys_n > 0 && install_sys_apps(paths, &sys_opts, sys_names, sys_n) != 0)
            goto cleanup;
    }

    if (strcmp(extra_apps_buf, "*") == 0)
    {
        if (install_extra_apps(paths, &extra_opts, NULL, 0) != 0)
            goto cleanup;
    }
    else
    {
        extra_n = split_names(extra_apps_buf, extra_names, SYSGEN_MAX_APP_NAMES);

        if (extra_n > 0 && install_extra_apps(paths, &extra_opts, extra_names, extra_n) != 0)
            goto cleanup;
    }

    bd_sync();

    char out_disk_buf[SYSGEN_FULL_PATH_MAX];
    sysgen_default_disk(out_disk_buf, sizeof(out_disk_buf));
    const char *out_disk_path = out_disk_buf;

    if (save_disk(out_disk_path) != 0)
        goto cleanup;

    report_build(paths, &dcfg, dcfg.disk_size_kb, bsz, ksz, ccpsz, kern_load, tpa_base,
                 (uint32_t)reserved, out_disk_path);
    ret = 0;

cleanup:
    free(kern);
    free(elf);
    free(ccp);
    return ret;
}