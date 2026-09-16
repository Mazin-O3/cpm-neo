/*
 * sdk/include/ccplib.h — Shared command library
 *
 * Command error handling, dispatch tables, argument-shape validation,
 * and the standard app entry point.  Used by the CCP and transient
 * commands.  Filespec/path parsing lives in path.h.
 */
#ifndef SDK_CCPLIB_H
#define SDK_CCPLIB_H

#include <cpmneo.h>
#include <path.h>

/*
 * Command error convention
 *
 * A command returns cmderr_ok() (err_code == 0) on success; err_code
 * holds a strerror() errno or CMDERR_SYNTAX (a usage error, printed as
 * "<token>?").  For transient commands exit(err_code) propagates the
 * code to ENV_RETURN_CODE on warm boot.
 */
#define CMDERR_SYNTAX 1 /* "?" or "<token>?" */

typedef struct
{
    int8_t      vol_id; /* Volume the error refers to, or VOL_INVALID */
    int         err_code;
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

/*
 * Print a CmdErr to stderr: "<token>?" for syntax errors; otherwise the
 * strerror() text, prefixed as "Bdos Err On <vol>: " when a volume is
 * attached — Except ENOENT/EEXIST, which always print plain ("No File",
 * "File Exists").  err_code is recorded in ENV_RETURN_CODE (CCP-only;
 * transient apps propagate it via exit() instead).
 */
void cmderr_print(CmdErr se);

/*
 * Command dispatch
 *
 * Command table entry.  Help metadata (usage/desc/detail/category) is
 * embedded in apps/sys/help.c, so the table is just the dispatch mapping.
 */
typedef CmdErr (*cmd_fn_t)(FsContext *ctx, int argc, char **argv);

#define CCP_NUM_CMDS 9

typedef struct
{
    const char *name;
    cmd_fn_t    fn;
} CmdEntry;

const CmdEntry *cmd_lookup(const CmdEntry *table, const char *name);

/* Argument validation / helpers */

/* Validate argument shape against a format string (see check_fmt). */
int check_fmt(int argc, char **argv, const char *fmt);

/*
 * Copy min(len,w) chars of src into out, space-pad to exactly w chars and
 * NUL-terminate (out must hold w+1 bytes).  Passing len == w just
 * NUL-terminates a view.  Used instead of printf "%-*.*s", which the
 * cpm printf does not support.
 */
void pad_field(char *out, const char *src, int len, int w);

/*
 * Strict decimal integer parse: the entire string must be consumed.
 * Returns 1 on success (with *out set), 0 on malformed input.
 */
int parse_int(const char *s, int *out);

/*
 * Console pagination
 *
 * "More" prompting shared by TYPE/DUMP-style output: pager_start() queries
 * the console size, pager_line() counts a printed line, pauses at each
 * full screen and returns 1 when the user pressed ESC to abort.
 */
typedef struct
{
    uint8_t cols;
    uint8_t rows;
    int     line_count;
} Pager;

Pager pager_start(void);
int   pager_line(Pager *p);

/*
 * Standard app entry point
 *
 * Grab the current filesystem context, run the command, print any error,
 * and return the err_code for ENV_RETURN_CODE.
 */
int ccp_run_app(cmd_fn_t fn, int argc, char **argv);

#endif /* SDK_CCPLIB_H */