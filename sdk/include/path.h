/*
 * sdk/include/path.h — Filespec prefix and 8.3 name parsing/building
 *
 * The single shared vocabulary for file references: the volume/user
 * prefix grammar, filespec splitting, and path construction.  Compiled
 * into the kernel, the CCP, and libc.a so every layer agrees on one rule
 * for "X:", "Xn:" and "n:" filespecs (implemented in sdk/src/path.c).
 */

#ifndef SDK_PATH_H
#define SDK_PATH_H

#include <stddef.h>
#include <stdint.h>

#include "fsctx.h"

/* Parsed file reference: the filesystem context (volume + user area) plus
 * the raw 8.3 name (possibly containing wildcards). */
typedef struct
{
    FsContext fs_ctx;
    char      name[FILENAME_MAX];
} FileRef;

/* Full path buffer size: "V15:" prefix + 8.3 name + NUL. */
#define FSPATH_MAX (ARG_LEN_MAX + 4)

/* Prefix parsing */

/* Length of a leading volume/user prefix ("V:", "VU:", "U:"), or 0. */
int vu_prefix_len(const char *arg);

/* split_prefix — split an optional volume/user prefix off a filespec.
 * Grammar: "X:" (volume only), "Xn:" (volume + user area), "n:" (user
 * area only).  Returns a pointer past the ':' when a valid prefix was
 * consumed (ctx updated in place, bounded to MAX_VOLUMES/USER_AREA_MAX);
 * returns p unchanged, ctx untouched otherwise. */
const char *split_prefix(const char *p, FsContext *ctx);

/* Volume id from a bare "X:" volume argument, or |def| when the argument
 * does not name a volume. */
int8_t vol_from_arg(const char *arg, int8_t def);

/* Filespec / path building */

/* Split an argument into a FileRef: full prefix grammar, remainder copied
 * into out->name (FILENAME_MAX - 1 chars, NUL-terminated). */
int parse_fileref(FsContext *ctx, const char *arg, FileRef *out);

/* Build "A0:name" (volume letter, user area, colon, name) into buf, which
 * must hold at least FSPATH_MAX bytes.  Returns buf. */
char *make_path(char *buf, FsContext ctx, const char *name);

/* 8.3 name helpers */

/* True if the 8.3 name contains '*' or '?'. */
int has_wildcard(const char *name);

/* An 8.3 name split into base/extension views.  The pointers alias
 * |name| and the fields are NOT NUL-terminated; lengths are capped at
 * NAME83_BASE/NAME83_EXT so printf "%.*s" is always in range. */
typedef struct
{
    const char *base;
    int         base_len;
    const char *ext; /* "" when the name has no extension */
    int         ext_len;
} SplitName;

SplitName split_name83(const char *name);

/* String helpers */

/* Copy up to n chars of src into out, always NUL-terminated. */
void name_copy(char *out, const char *src, size_t n);

/* Batch path convention */

/* "$$$.SUB" addressed as "<vol>0:" so it lives in user 0 where the
 * resident CCP finds it after USER switches. */
#define BATCH_NAME     "$$$.SUB"
#define BATCH_PATH_LEN 12 /* "A0:" + "$$$.SUB" + NUL */

void make_batch_path(char *out, int8_t vol);

#endif /* SDK_PATH_H */