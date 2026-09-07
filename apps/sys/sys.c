#include <ccplib.h>

static CmdErr cmd_sys(FsContext *ctx, int argc, char **argv)
{
    (void)ctx;
    (void)argv;

    if (argc > 1)
        return cmderr_syntax(NULL);

    SysInfo si;

    if (sys_info(&si) != EOK)
        return cmderr_bdos(ctx->vol_id, EIO);
    
    const char *sep = "------------------------------------";

    printf("\nCP/M Neo v%u.%u\n", si.os_version >> 8, si.os_version & 0xFF);
    printf("%s\n", sep);
    printf("%-16s : ", "Platform");

    for (int i = 0; si.platform[i]; i++)
    {
        char c = si.platform[i];
        putchar((c >= 'a' && c <= 'z') ? (char)(c - ('a' - 'A')) : c);
    }

    putchar('\n');
    printf("%-16s : %s\n", "XIP", si.xip ? "Yes" : "No");
    printf("%-16s : %uK\n", "Disk", si.disk_size_kb);
    printf("%-16s : %uK\n", "TPA", si.tpa);
    printf("%-16s : [", "Volumes");

    for (int8_t v = 0; v < MAX_VOLUMES; v++)
    {
        if (!si.vol_mounted[v])
            continue;

        printf("%s%c:", v ? ", " : "", 'A' + v);
    }

    printf("]\n");
    printf("%s\n", sep);
    printf("%-16s : v%u.%u\n", "Kernel", si.kern_version >> 8, si.kern_version & 0xFF);
    printf("%-16s : v%u.%u\n", "CCP", si.ccp_version >> 8, si.ccp_version & 0xFF);
    printf("%s\n\n", sep);

    return cmderr_ok();
}

int main(int argc, char **argv)
{
    return ccp_run_app(cmd_sys, argc, argv);
}
