/*
 * core/ccp/ccp.c — Console Command Processor
 *
 * Maintains the command loop, dispatches internal commands (DIR/DIRS/ERA/
 * REN/TYPE/USER/CLS/ECHO), and falls back to try_implicit_run() for transient
 * programs (.COM files loaded from disk). A batch mode reads commands
 * from $$$.SUB when it exists.
 */

#include "ccp.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <path.h>

typedef struct
{
    FsContext ctx;
    char      input[CCP_LINE_MAX];
    char     *argv[CCP_ARGC_MAX];
    CmdEntry  cmds[CCP_MAX_CMDS];
} CCPState;

static CCPState g_ccp;

static void init_commands(void)
{
    g_ccp.cmds[0] = (CmdEntry){.name = "DIR",  .fn = cmd_dir};
    g_ccp.cmds[1] = (CmdEntry){.name = "DIRS", .fn = cmd_dirs};
    g_ccp.cmds[2] = (CmdEntry){.name = "ERA",  .fn = cmd_era};
    g_ccp.cmds[3] = (CmdEntry){.name = "REN",  .fn = cmd_ren};
    g_ccp.cmds[4] = (CmdEntry){.name = "TYPE", .fn = cmd_type};
    g_ccp.cmds[5] = (CmdEntry){.name = "USER", .fn = cmd_user};
    g_ccp.cmds[6] = (CmdEntry){.name = "ECHO", .fn = cmd_echo};
    g_ccp.cmds[7] = (CmdEntry){.name = "CLS",  .fn = cmd_cls};
    g_ccp.cmds[8] = (CmdEntry){.name = "SYNC", .fn = cmd_sync};

    g_ccp.cmds[CCP_MAX_CMDS - 1] = (CmdEntry){0};
}

/*
 * Splits input in-place into space-delimited tokens.
 * Argv pointers alias directly into line, which is modified with NUL terminators.
 */
static int tokenise(char *line, char *argv[], int max)
{
    int   argc = 0;
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
    FsContext  new_ctx = g_ccp.ctx;
    const char *endptr = split_prefix(tok, &new_ctx);

    if (endptr == tok || *endptr != '\0')
        return cmderr_syntax(NULL);

    int rc = fs_setctx(new_ctx);
    
    if (rc != EOK)
        return cmderr_bdos(new_ctx.vol_id, rc);

    g_ccp.ctx = new_ctx;
    return cmderr_ok();
}

static void print_prompt(void)
{
    putchar('A' + g_ccp.ctx.vol_id);

    if (g_ccp.ctx.user_area)
    {
        if (g_ccp.ctx.user_area >= 10)
            putchar('0' + g_ccp.ctx.user_area / 10);
        putchar('0' + g_ccp.ctx.user_area % 10);
    }

    putchar('>');
}

static void ccp_init(void)
{
    init_commands();
    sys_getctx(&g_ccp.ctx);
    try_run_batch(&g_ccp.ctx);
}

int ccp_setuser(FsContext *ctx, uint8_t ua)
{
    if (ua > USER_AREA_MAX)
        return EINVAL;

    ctx->user_area = ua;
    return fs_setctx(*ctx);
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
                erase(batch_path);
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

        int n = readline(fd, g_ccp.input, sizeof(g_ccp.input));

        if (n <= 0)
        {
            close(fd);
            erase(batch_path);
            sys_setenv(ENV_BATCH_OFFSET, 0);
            return;
        }

        sys_setenv(ENV_BATCH_OFFSET, offset + n);
        close(fd);

        if (g_ccp.input[0] == '\0' || g_ccp.input[0] == ';')
            continue;

        if (g_ccp.input[0] == ':')
        {
            if ((int)sys_getenv(ENV_RETURN_CODE) != 0)
                continue;

            memmove(g_ccp.input, g_ccp.input + 1, CCP_LINE_MAX - 1);
        }

        ccp_dispatch(g_ccp.input);
    }
}

CmdErr ccp_dispatch(char *line)
{
    sys_setenv(ENV_RETURN_CODE, 0);

    int argc = tokenise(line, g_ccp.argv, CCP_ARGC_MAX);

    if (argc == 0)
        return cmderr_ok();

    if (argc < CCP_ARGC_MAX)
        g_ccp.argv[argc] = NULL;

    CmdErr ce = try_ctx_switch(g_ccp.argv[0]);

    if (ce.err_code != CMDERR_SYNTAX || ce.token != NULL)
    {
        if (ce.err_code)
            cmderr_print(ce);

        return ce;
    }

    find_reset();

    const CmdEntry *e = cmd_lookup(g_ccp.cmds, g_ccp.argv[0]);

    if (e)
    {
        CmdErr se = e->fn(&g_ccp.ctx, argc, g_ccp.argv);

        if (se.err_code != 0)
            cmderr_print(se);

        return se;
    }

    CmdErr se = try_implicit_run(&g_ccp.ctx, argc, g_ccp.argv);

    if (se.err_code == 0)
    {
        printf("%s?\n", g_ccp.argv[0]);
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

        if (getline(g_ccp.input, CCP_LINE_MAX) < 0)
            continue;

        ccp_dispatch(g_ccp.input);
    }

    return 0;
}