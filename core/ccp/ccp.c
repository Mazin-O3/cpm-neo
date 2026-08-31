/*
 * core/ccp/ccp.c — Console Command Processor
 *
 * Maintains the command loop, dispatches internal commands (DIR/DIRS/ERA/
 * REN/TYPE/USER/CLS/ECHO), and falls back to try_implicit_run() for transient
 * programs (.COM files loaded from disk).  A batch mode reads commands
 * from $$$.SUB when it exists.
 */

#include "ccp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char input[CCP_LINE_MAX];
    char *argv[CCP_ARGC_MAX];
} CcpBuf;

static FsContext g_ctx;
static CcpBuf g_buf;

/* Resident commands — everything else falls through to try_implicit_run(). */
static const CmdEntry g_cmds[] = {{.name = "DIR", .fn = cmd_dir},
                                  {.name = "DIRS", .fn = cmd_dirs},
                                  {.name = "ERA", .fn = cmd_era},
                                  {.name = "REN", .fn = cmd_ren},
                                  {.name = "TYPE", .fn = cmd_type},
                                  {.name = "USER", .fn = cmd_user},
                                  {.name = "ECHO", .fn = cmd_echo},
                                  {.name = "CLS", .fn = cmd_cls},
                                  {.name = "SYNC", .fn = cmd_sync},
                                  {0}};

int ccp_setuser(FsContext *ctx, uint8_t ua)
{
    if (ua > USER_AREA_MAX)
        return EINVAL;

    ctx->user_area = ua;
    return fs_setctx(*ctx);
}

/*
 * The CCP's input buffer is reused across dispatches, so splitting
 * in-place avoids a separate allocation — argv pointers alias into
 * the same buffer that gets NUL-terminated on each space.
 */
static int tokenise(char *line, char *argv[], int max)
{
    int argc = 0;
    char *p = line;

    while (*p && argc < max)
    {
        while (*p == ' ')
            p++;

        if (!*p)
            break;

        argv[argc++] = p;

        while (*p && *p != ' ')
            p++;

        if (*p)
            *p++ = '\0';
    }

    return argc;
}

static CmdErr try_ctx_switch(const char *tok)
{
    const char *digits = NULL;
    int cp = 0;
    int8_t old_vol = g_ctx.vol_id;
    uint8_t old_ua = g_ctx.user_area;

    if (isalpha((unsigned char)tok[0]))
    {
        cp = 1;

        while (isdigit((unsigned char)tok[cp]))
            cp++;

        if (tok[cp] != ':' || tok[cp + 1] != '\0')
            return cmderr_syntax(NULL);

        int8_t idx = (int8_t)(toupper((unsigned char)tok[0]) - 'A');

        if (idx >= VOL_MAX)
            return cmderr_bdos(idx, EINVAL);

        g_ctx.vol_id = idx;
        digits = tok + 1;
    }
    else if (isdigit((unsigned char)tok[0]))
    {
        cp = 0;

        while (tok[cp] >= '0' && tok[cp] <= '9')
            cp++;

        if (tok[cp] != ':' || tok[cp + 1] != '\0')
            return cmderr_syntax(NULL);

        digits = tok;
    }
    else
    {
        return cmderr_syntax(NULL);
    }

    if (cp > (digits - tok))
    {
        char *ep;
        int ua = strtoi(digits, &ep, 10);

        if (ep != tok + cp || ua < 0 || ua > USER_AREA_MAX)
        {
            g_ctx.vol_id = old_vol; /* undo the vol_id set above, if any */
            return cmderr_syntax(tok);
        }

        g_ctx.user_area = (uint8_t)ua;
    }

    int rc = fs_setctx(g_ctx);

    if (rc != EOK)
    {
        int8_t err_vol = g_ctx.vol_id;
        g_ctx.vol_id = old_vol;
        g_ctx.user_area = old_ua;

        return cmderr_bdos(err_vol, rc);
    }

    return cmderr_ok();
}

void try_run_batch(FsContext *ctx)
{
    char batch_path[BATCH_PATH_LEN];
    make_batch_path(batch_path, ctx->vol_id);

    sys_setenv(ENV_RETURN_CODE, 0);

    for (;;)
    {
        fs_setctx(*ctx);

        while (peekchar())
        {
            if (getchar() == CH_ESC)
            {
                remove(batch_path);
                sys_setenv(ENV_BATCH_OFFSET, 0);
                return;
            }
        }

        uint32_t offset = sys_getenv(ENV_BATCH_OFFSET);

        int fd = open(batch_path, "r");

        if (fd < 0)
        {
            sys_setenv(ENV_BATCH_OFFSET, 0);
            return;
        }

        lseek(fd, offset, SEEK_SET);

        int n = readline(fd, g_buf.input, sizeof(g_buf.input));

        if (n <= 0)
        {
            close(fd);
            remove(batch_path);
            sys_setenv(ENV_BATCH_OFFSET, 0);
            return;
        }

        sys_setenv(ENV_BATCH_OFFSET, offset + n);
        close(fd);

        if (g_buf.input[0] == '\0' || g_buf.input[0] == ';')
            continue;

        if (g_buf.input[0] == ':')
        {
            if ((int)sys_getenv(ENV_RETURN_CODE) != 0)
                continue;

            memmove(g_buf.input, g_buf.input + 1, CCP_LINE_MAX - 1);
        }

        ccp_dispatch(g_buf.input);
    }
}

static void ccp_init(void)
{
    sys_getctx(&g_ctx);
    try_run_batch(&g_ctx);
}

static void print_prompt(void)
{
    putchar('A' + g_ctx.vol_id);

    if (g_ctx.user_area)
    {
        if (g_ctx.user_area >= 10)
            putchar('0' + g_ctx.user_area / 10);
        putchar('0' + g_ctx.user_area % 10);
    }

    putchar('>');
}

CmdErr ccp_dispatch(char *line)
{
    sys_setenv(ENV_RETURN_CODE, 0);

    int argc = tokenise(line, g_buf.argv, CCP_ARGC_MAX);

    if (argc == 0)
        return cmderr_ok();

    if (argc < CCP_ARGC_MAX)
        g_buf.argv[argc] = NULL;

    CmdErr ce = try_ctx_switch(g_buf.argv[0]);

    if (ce.err_code != CMDERR_SYNTAX || ce.token != NULL)
    {
        if (ce.err_code)
            cmderr_print(ce);

        return ce;
    }

    find_reset();

    const CmdEntry *e = cmd_lookup(g_cmds, g_buf.argv[0]);

    if (e)
    {
        CmdErr se = e->fn(&g_ctx, argc, g_buf.argv);

        if (se.err_code != 0)
            cmderr_print(se);

        return se;
    }

    CmdErr se = try_implicit_run(&g_ctx, argc, g_buf.argv);

    if (se.err_code == 0)
    {
        printf("%s?\n", g_buf.argv[0]);
        return cmderr_ok();
    }

    cmderr_print(se);
    return se;
}

int main(void)
{
    ccp_init();

    for (;;)
    {
        print_prompt();

        if (getline(g_buf.input, CCP_LINE_MAX) < 0)
            continue;

        ccp_dispatch(g_buf.input);
    }

    return 0;
}
