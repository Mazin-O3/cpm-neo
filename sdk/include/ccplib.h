#ifndef CCPLIB_H
#define CCPLIB_H

#include <cpm.h>

/* Command error / return-code convention shared by the CCP and transient
 * commands.  A command returns cmderr_ok() (err_code == 0) on success;
 * err_code holds a strerror() errno or CMDERR_SYNTAX (a usage error, printed
 * as "<token>?").  For transient commands exit(err_code) propagates the code
 * to ENV_RETURN_CODE on warm boot. */
#define CMDERR_SYNTAX 1 /* "?" or "<token>?" */

typedef struct
{
    int8_t vol_id;    /* Volume the error refers to, or VOL_INVALID */
    int err_code;
    const char *token; /* Offending token for CMDERR_SYNTAX */
} CmdErr;

static inline CmdErr cmderr_ok(void)
{
    return (CmdErr){VOL_INVALID, 0, NULL};
}

static inline CmdErr cmderr_syntax(const char *token)
{
    return (CmdErr){VOL_INVALID, CMDERR_SYNTAX, token};
}

static inline CmdErr cmderr_errno(int err)
{
    return (CmdErr){VOL_INVALID, err, NULL};
}

static inline CmdErr cmderr_bdos(int8_t v, int r)
{
    return (CmdErr){v, r, NULL};
}

/* Print a CmdErr to stderr: "<token>?" for syntax errors; otherwise the
 * strerror() text, prefixed as "Bdos Err On <vol>: " when a volume is
 * attached — Except ENOENT/EEXIST, which always print plain ("No File",
 * "File Exists").  err_code is recorded in ENV_RETURN_CODE (CCP-only;
 * transient apps propagate it via exit() instead). */
void cmderr_print(CmdErr se);

/* Command table entry.  Help metadata (usage/desc/detail/category) is
 * embedded in apps/sys/help.c, so the table is just the dispatch mapping. */
typedef CmdErr (*cmd_fn_t)(FsContext *ctx, int argc, char **argv);

typedef struct
{
    const char *name;
    cmd_fn_t fn;
} CmdEntry;

const CmdEntry *cmd_lookup(const CmdEntry *table, const char *name);

/* A parsed file reference: the filesystem context (volume + user area) plus
 * the raw 8.3 name (possibly containing wildcards). */
typedef struct
{
    FsContext fs_ctx;
    char name[FILENAME_MAX];
} FileRef;

/* Argument / filespec helpers shared by every command. */
int8_t vol_from_arg(const char *arg, int8_t def);
int parse_fileref(FsContext *ctx, const char *arg, FileRef *out);
char *make_path(char *buf, FsContext ctx, const char *name);
int check_fmt(int argc, char **argv, const char *fmt);

/* Full path buffer size: "V15:" prefix + 8.3 name + NUL. */
#define FSPATH_MAX (ARG_LEN_MAX + 4)

/* True if the 8.3 name contains '*' or '?'. */
int has_wildcard(const char *name);

/* Copy up to n chars of src into out, always NUL-terminated. */
void name_copy(char *out, const char *src, size_t n);

/* Length of a leading volume/user prefix ("V:", "VU:", "U:"), or 0. */
int vu_prefix_len(const char *arg);

/* An 8.3 name split into base/extension views. The pointers alias |name|
 * and the fields are NOT NUL-terminated; lengths are capped at
 * NAME83_BASE/NAME83_EXT so printf "%.*s" is always in range. */
typedef struct
{
    const char *base;
    int base_len;
    const char *ext; /* "" when the name has no extension */
    int ext_len;
} SplitName;

SplitName split_name83(const char *name);

/* Copy min(len,w) chars of src into out, space-pad to exactly w chars and
 * NUL-terminate (out must hold w+1 bytes).  Passing len == w just
 * NUL-terminates a view.  Used instead of printf "%-*.*s", which the
 * cpm printf does not support. */
void pad_field(char *out, const char *src, int len, int w);

/* Strict decimal integer parse: the entire string must be consumed.
 * Returns 1 on success (with *out set), 0 on malformed input. */
int parse_int(const char *s, int *out);

/* Batch-file convention: "$$$.SUB" addressed as "<vol>0:" so it lives in
 * user 0 where the resident CCP finds it after USER switches. */
#define BATCH_NAME "$$$.SUB"
#define BATCH_PATH_LEN 12 /* "A0:" + "$$$.SUB" + NUL */
void make_batch_path(char *out, int8_t vol);

/* Console pagination ("more" prompting) shared by TYPE/DUMP-style output:
 * pager_start() queries the console size, pager_line() counts a printed
 * line, pauses at each full screen and returns 1 when the user pressed
 * ESC to abort the listing. */
typedef struct
{
    uint8_t cols;
    uint8_t rows;
    int line_count;
} Pager;

Pager pager_start(void);
int pager_line(Pager *p);

/* Standard app entry point: grab the current filesystem context, run the
 * command, print any error, and return the err_code for ENV_RETURN_CODE. */
int ccp_run_app(cmd_fn_t fn, int argc, char **argv);

#endif /* CCPLIB_H */