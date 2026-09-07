#include "utils.h"
#include "bdos.h"
#include "disk.h"
#include "sysgen.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#include <errno.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
extern char **environ;
#endif

static SysgenPaths g_paths = {
    .build_dir = "sysgen/build",
    .root_dir = ".",
};

void sysgen_init_paths(const char *argv0)
{
    const char *slash = strrchr(argv0, '/');
#if defined(_WIN32)
    const char *bslash = strrchr(argv0, '\\');

    if (bslash && (!slash || bslash > slash))
        slash = bslash;
#endif

    if (!slash)
        return;

    int len = (int)(slash - argv0);

    if (len >= SYSGEN_PATH_MAX)
        len = SYSGEN_PATH_MAX - 1;

    snprintf(g_paths.build_dir, sizeof(g_paths.build_dir), "%.*s", len, argv0);
    snprintf(g_paths.root_dir, sizeof(g_paths.root_dir), "%s/../..", g_paths.build_dir);
}

const SysgenPaths *sysgen_paths(void)
{
    return &g_paths;
}

void sysgen_default_disk(char *buf, size_t n)
{
    snprintf(buf, n, "%s/disk.img", g_paths.build_dir);
}

void err(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "ERROR: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

const char *err_str(int rc)
{
    switch (rc)
    {
    case EIO:
        return "I/O Error";
    case EBADFS:
        return "Bad Filesystem";
    case ENOVOL:
        return "No Volume";
    case ENFILE:
        return "No Free File Handle";
    case ENOENT:
        return "Not Found";
    case EBADF:
        return "Bad File Handle";
    case ENOSPC:
        return "Disk Full";
    case EVOLRO:
        return "Volume Read-only";
    case EEXIST:
        return "File Exists";
    case EFILERO:
        return "File Read-only";
    case EDIRFULL:
        return "Directory Full";
    case EPERM:
        return "Permission Denied";
    default:
        return "Unknown";
    }
}

const char *flag_value(int argc, char **argv, const char *name, int *seen)
{
    size_t n = strlen(name);
    *seen = 0;

    for (int i = 0; i < argc; i++)
    {
        if (argv[i][0] != '-')
            continue;

        if (strncmp(argv[i], name, n) == 0 && argv[i][n] == '=')
        {
            *seen = 1;
            return argv[i] + n + 1;
        }

        if (strncmp(argv[i], name, n) == 0 && argv[i][n] == '\0')
        {
            *seen = 1;
            return NULL;
        }
    }

    return NULL;
}

int reject_unknown_flags(int argc, char **argv, const char *const *allowed)
{
    for (int i = 0; i < argc; i++)
    {
        if (argv[i][0] != '-')
            continue;

        int ok = 0;

        for (int a = 0; allowed[a]; a++)
        {
            size_t n = strlen(allowed[a]);

            if (strncmp(argv[i], allowed[a], n) == 0 && (argv[i][n] == '=' || argv[i][n] == '\0'))
            {
                ok = 1;
                break;
            }
        }

        if (!ok)
        {
            err("unknown option '%s'", argv[i]);
            return 1;
        }
    }

    return 0;
}

int collect_positional(int argc, char **argv, const char **out, int max_out)
{
    int n = 0;

    for (int i = 0; i < argc; i++)
    {
        if (argv[i][0] == '-' && argv[i][1] == '-')
            continue;

        if (n < max_out)
            out[n] = argv[i];
        n++;
    }

    return n;
}

/* Resolve the target disk path (--disk=..., else the default) into buf.
 * Always writes into buf and returns buf, so callers never need to
 * re-copy the returned pointer. */
const char *resolve_disk(int argc, char **argv, char *buf, size_t n)
{
    int has = 0;
    const char *v = flag_value(argc, argv, "--disk", &has);

    if (has && v && *v)
        snprintf(buf, n, "%s", v);
    else
        sysgen_default_disk(buf, n);

    return buf;
}

int parse_vn(const char *s, int *vol, int *user)
{
    if (!s || !*s)
    {
        *vol = VOL_A;
        *user = 0;
        return 0;
    }

    char c = (char)toupper((unsigned char)s[0]);

    if (c < 'A' || c > 'A' + MAX_VOLUMES - 1)
        return -1;

    *vol = c - 'A';
    int u = 0;

    for (int i = 1; s[i]; i++)
    {
        if (!isdigit((unsigned char)s[i]))
            return -1;

        u = u * 10 + (s[i] - '0');

        if (u > USER_AREA_MAX)
            return -1;
    }

    *user = u;
    return 0;
}

#if defined(_WIN32)
int dir_exists(const char *p)
{
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

int file_exists(const char *p)
{
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

#else
int dir_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

int file_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

#endif

void hr(char *out, size_t n, uint32_t bytes)
{
    if (bytes >= 1024 * 1024)
    {
        uint32_t mb = bytes / (1024 * 1024);

        if (bytes % (1024 * 1024) == 0)
            snprintf(out, n, "%u MB", mb);
        else
            snprintf(out, n, "%.2f MB", (double)bytes / (1024.0 * 1024.0));
    }
    else if (bytes >= 1024)
    {
        uint32_t kb = bytes / 1024;

        if (bytes % 1024 == 0)
            snprintf(out, n, "%u KB", kb);
        else
            snprintf(out, n, "%.1f KB", (double)bytes / 1024.0);
    }
    else
    {
        snprintf(out, n, "%u B", bytes);
    }
}

typedef struct
{
    char path[1024];
    char name[256];
    uint8_t is_dir;
} ComEntry;

static int com_name_cmp(const void *a, const void *b)
{
    return strcmp(((const ComEntry *)a)->name, ((const ComEntry *)b)->name);
}

/* Scan dir for all non-dot entries (files and subdirectories). Returns the
 * entry count (sorted by name) or 0. */
static int scan_dir(const char *dir, ComEntry *list, int cap)
{
    int n = 0;

#if defined(_WIN32)
    char pat[1024];
    _snprintf(pat, sizeof pat, "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);

    if (h == INVALID_HANDLE_VALUE)
        return 0;

    do
    {
        if (n >= cap)
            break;

        if (!fd.cFileName[0] || fd.cFileName[0] == '.')
            continue;

        _snprintf(list[n].path, sizeof list[n].path, "%s\\%s", dir, fd.cFileName);
        _snprintf(list[n].name, sizeof list[n].name, "%s", fd.cFileName);
        list[n].is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        n++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);

    if (!d)
        return 0;

    struct dirent *de;

    while ((de = readdir(d)) && n < cap)
    {
        if (de->d_name[0] == '.')
            continue;

        char full[1024];
        snprintf(full, sizeof full, "%s/%s", dir, de->d_name);

        struct stat st;

        if (stat(full, &st) != 0)
            continue;

        snprintf(list[n].path, sizeof list[n].path, "%s", full);
        snprintf(list[n].name, sizeof list[n].name, "%s", de->d_name);
        list[n].is_dir = S_ISDIR(st.st_mode) != 0;
        n++;
    }

    closedir(d);

#endif

    qsort(list, (size_t)n, sizeof(ComEntry), com_name_cmp);
    return n;
}

int has_source_ext(const char *name)
{
    const char *e = strrchr(name, '.');

    if (!e)
        return 0;

    return strcmp(e, ".c") == 0 || strcmp(e, ".s") == 0 || strcmp(e, ".S") == 0;
}

/* Iterate the subdirectories of dir (bundled apps). */
int for_each_subdir(const char *dir, SysgenCallback cb, void *ud)
{
    ComEntry list[512];
    int n = scan_dir(dir, list, 512);

    for (int i = 0; i < n; i++)

        if (list[i].is_dir)
            cb(list[i].path, list[i].name, ud);

    return n;
}

/* Iterate the top-level .c/.s/.S files in a directory (single-file apps). */
int for_each_source_file(const char *dir, SysgenCallback cb, void *ud)
{
    ComEntry list[512];
    int n = scan_dir(dir, list, 512);

    for (int i = 0; i < n; i++)

        if (!list[i].is_dir && has_source_ext(list[i].name))
            cb(list[i].path, list[i].name, ud);

    return n;
}

int dir_has_sources(const char *dir)
{
    ComEntry list[512];
    int n = scan_dir(dir, list, 512);

    for (int i = 0; i < n; i++)
    {
        if (list[i].is_dir)
        {
            if (dir_has_sources(list[i].path))
                return 1;
        }
        else if (has_source_ext(list[i].name))
        {
            return 1;
        }
    }

    return 0;
}

/* Return 1 if dir contains any subdirectory (used to enforce flat folders). */
int dir_has_subdirs(const char *dir)
{
    ComEntry list[512];
    int n = scan_dir(dir, list, 512);

    for (int i = 0; i < n; i++)

        if (list[i].is_dir)
            return 1;

    return 0;
}

/* Iterate the immediate non-directory entries of dir (flat folder add). */
int for_each_flat_file(const char *dir, SysgenCallback cb, void *ud)
{
    ComEntry list[512];
    int n = scan_dir(dir, list, 512);

    int files = 0;

    for (int i = 0; i < n; i++)
    {
        if (list[i].is_dir)
            continue;

        cb(list[i].path, list[i].name, ud);
        files++;
    }

    return files;
}

int mkdir_p(const char *path)
{
#if defined(_WIN32)
    char tmp[SYSGEN_PATH_MAX];
    _snprintf(tmp, sizeof tmp, "%s", path);
    size_t len = strlen(tmp);

    if (len == 0)
        return -1;

    for (char *p = tmp + 1; *p; p++)
    {
        if (*p == '\\' || *p == '/')
        {
            char save = *p;
            *p = '\0';
            _mkdir(tmp);
            *p = save;
        }
    }

    if (_mkdir(tmp) != 0 && errno != EEXIST)
        return -1;

    return 0;
#else
    char tmp[SYSGEN_PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s", path);
    size_t len = strlen(tmp);

    if (len == 0)
        return -1;

    for (char *p = tmp + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';

            if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
            {
                *p = '/';
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        return -1;

    return 0;
#endif
}

int open_disk(const char *path)
{
    uint8_t *buf;
    uint32_t len;

    if (read_file(path, &buf, &len) != 0)
    {
        err("cannot read '%s'", path);
        return -1;
    }

    if (len < DISK_SECTOR_SIZE || (len % DISK_SECTOR_SIZE) != 0)
    {
        free(buf);
        err("'%s' is not a valid disk image", path);
        return -1;
    }

    sysgen_set_disk(buf, len);

    if (read16(sysgen_disk() + S0_MAGIC) != DISK_MAGIC)
    {
        err("'%s' is not a CP/M Neo disk image (bad magic)", path);
        return -1;
    }

    return 0;
}

int save_disk(const char *path)
{
    if (write_file(path, sysgen_disk(), sysgen_disk_size()) != 0)
    {
        err("cannot write '%s'", path);
        return -1;
    }

    return 0;
}

int mount_vol(int8_t vol)
{
    if (volume_run_count(vol) == 0)
    {
        if (bd_mount(vol) != EOK)
        {
            err("volume %c: cannot auto-mount", 'A' + vol);
            return -1;
        }

        return 0;
    }

    if (bd_bind(vol) != EOK)
    {
        err("volume %c: cannot mount", 'A' + vol);
        return -1;
    }

    return 0;
}

#if defined(_WIN32)

static int append_quoted_arg(char *cmdline, size_t cap, const char *arg)
{
    size_t len = strlen(cmdline);

    if (len && len + 1 < cap)
        cmdline[len++] = ' ';

    if (len >= cap)
        return -1;

    size_t pos = len;

    if (pos + 1 >= cap)
        return -1;
    cmdline[pos++] = '"';

    for (const char *p = arg; *p;)
    {
        size_t backslashes = 0;

        while (*p == '\\')
        {
            backslashes++;
            p++;
        }


        if (*p == '\0')
        {
            for (size_t i = 0; i < backslashes * 2; i++)
            {
                if (pos + 1 >= cap)
                    return -1;
                cmdline[pos++] = '\\';
            }

            break;
        }


        if (*p == '"')
        {
            for (size_t i = 0; i < backslashes * 2 + 1; i++)
            {
                if (pos + 1 >= cap)
                    return -1;
                cmdline[pos++] = '\\';
            }


            if (pos + 1 >= cap)
                return -1;
            cmdline[pos++] = '"';
            p++;
        }
        else
        {
            for (size_t i = 0; i < backslashes; i++)
            {
                if (pos + 1 >= cap)
                    return -1;
                cmdline[pos++] = '\\';
            }


            if (pos + 1 >= cap)
                return -1;
            cmdline[pos++] = *p++;
        }
    }


    if (pos + 2 >= cap)
        return -1;
    cmdline[pos++] = '"';
    cmdline[pos] = '\0';
    return 0;
}

int spawn_and_wait(char *const argv[])
{
    char cmdline[8192];
    cmdline[0] = '\0';

    for (int i = 0; argv[i]; i++)
    {
        if (append_quoted_arg(cmdline, sizeof cmdline, argv[i]) != 0)
        {
            err("command line too long to launch '%s'", argv[0]);
            return -1;
        }
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    ZeroMemory(&pi, sizeof pi);

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        err("failed to launch '%s' (error %lu)", argv[0], (unsigned long)GetLastError());
        return -1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (code != 0)
    {
        err("'%s' exited with status %lu", argv[0], (unsigned long)code);
        return -1;
    }

    return 0;
}

#else /* POSIX */

int spawn_and_wait(char *const argv[])
{
    pid_t pid;
    int rc = posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ);

    if (rc != 0)
    {
        err("failed to launch '%s': %s", argv[0], strerror(rc));
        return -1;
    }

    int status = 0;

    if (waitpid(pid, &status, 0) < 0)
    {
        err("waitpid on '%s' failed: %s", argv[0], strerror(errno));
        return -1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        err("'%s' exited abnormally or with a nonzero status", argv[0]);
        return -1;
    }

    return 0;
}

#endif
