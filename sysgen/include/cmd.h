#ifndef SYSGEN_CMD_H
#define SYSGEN_CMD_H

#include <stdbool.h>
#include <stdint.h>

#include "bdos.h"
#include "utils.h"

/* Disk image path + filesystem context for host-side operations. */
typedef struct
{
    char disk_path[SYSGEN_FULL_PATH_MAX];
    FsContext ctx;
} ImageTarget;

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

/* CLI option parsing (shared by the command implementations) */
const char *get_str_flag(int argc, char **argv, const char *flag_name);
bool get_bool_flag(int argc, char **argv, const char *flag_name);
int check_flags(int argc, char **argv, const char *const *allowed);
int check_positionals(int argc, char **argv, int min_pos, int max_pos);
int parse_dst(int argc, char **argv, int *vol, int *user);
int parse_attr_dflt(int argc, char **argv, uint8_t *attr, uint8_t dflt);
int setup_disk_target(int argc, char **argv, const char *vn_arg, ImageTarget *tgt);

/* Shared path/name helpers */
const char *extract_basename(const char *path);

/* Add a single host file to the currently mounted disk image.
 * Returns 0 on success, 1 on error, 2 on skip (already exists). */
int add_data_open(const char *file, const AddFileOpts *opts);

/* Bundled-app build & install (shared by cmd_new and cmd_install) */
int build_folder_com(const SysgenPaths *paths, const char *src, const BuildFolderOpts *opts);
int install_sys_apps(const SysgenPaths *paths, const AddFileOpts *opts);
int install_extra_apps(const SysgenPaths *paths, const AddFileOpts *opts);

#endif /* SYSGEN_CMD_H */