#ifndef SYSGEN_UTILS_H
#define SYSGEN_UTILS_H

#include <stdint.h>
#include <stddef.h>

#define SYSGEN_PATH_MAX 1024

#define SYSGEN_FULL_PATH_MAX (SYSGEN_PATH_MAX + 64)

typedef int (*SysgenCallback)(const char *, const char *, void *);

typedef struct
{
    char build_dir[SYSGEN_PATH_MAX];
    char root_dir[SYSGEN_PATH_MAX + 8];

} SysgenPaths;

void sysgen_init_paths(const char *argv0);

const SysgenPaths *sysgen_paths(void);

const char *flag_value(int argc, char **argv, const char *name, int *seen);

int reject_unknown_flags(int argc, char **argv, const char *const *allowed);

int collect_positional(int argc, char **argv, const char **out, int max_out);

const char *resolve_disk(int argc, char **argv, char *buf, size_t n);
void sysgen_default_disk(char *buf, size_t n);

long parse_sized_kb(const char *s);
int parse_vn(const char *s, int *vol, int *user);

int dir_exists(const char *p);
int file_exists(const char *p);
void hr(char *out, size_t n, uint32_t bytes);

void err(const char *fmt, ...);
const char *err_str(int rc);

int open_disk(const char *path);
int save_disk(const char *path);
int mount_vol(int8_t vol);
int for_each_subdir(const char *dir, SysgenCallback cb, void *ud);
int for_each_source_file(const char *dir, SysgenCallback cb, void *ud);
int for_each_flat_file(const char *dir, SysgenCallback cb, void *ud);
int dir_has_sources(const char *dir);
int dir_has_subdirs(const char *dir);
int has_source_ext(const char *name);
int mkdir_p(const char *path);

int spawn_and_wait(char *const argv[]);

#endif