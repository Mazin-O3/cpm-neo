/*
 * sysgen/src/main.c
 * CP/M Neo SYSGEN — Entry point & Command routing
 */

#include "commands.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>

static void usage(void)
{
    printf("\n*** CP/M Neo SYSGEN Utility ***\n\n"
           "Usage:\n\n"
            "  sysgen new <args> [opts]        Create a new disk image\n"
            "      --platform=<ID>   Platform id (8-char max; declared by ID= in\n"
            "                       the platform's config.sh; provides the ISA and RAM_SIZE)\n"
            "      --no-extra       Skip optional bundled apps (apps/extra)\n\n"
           "  sysgen install <folder|file.c> [opts]  Compile sources to .com and add to disk\n"
           "      --sys-apps    Install bundled sys apps (apps/sys)\n"
           "      --extra-apps  Install bundled extra apps (apps/extra)\n"
"  sysgen add     <file|folder> [opts]  Add a file (or flat folder) to disk\n"
            "  sysgen extract [opts]  Extract every file to <build_dir>/extract/\n"
"  sysgen dir     [Vn]     [opts]  List files\n"
            "  sysgen era     <name>   [opts]  Erase file\n\n"
           "Options [opts]:\n"
           "  --dst=<Vn>    Volume & user area (e.g., A0) [default: A0]\n"
           "  --attr=<val>  RO, RW, SYS, or SYS+RO [default: RW for add;\n"
           "                RO for install/extra-apps; SYS+RO for sys-apps]\n"
           "  --disk=<path>  Target disk (all commands except new; new writes to "
           "<build_dir>/disk.img) [default: <build_dir>/disk.img]\n");
}

typedef struct
{
    const char *name;
    int (*fn)(int argc, char **argv);
} SysgenCmd;

static const SysgenCmd g_cmds[] = {
    {.name = "new", .fn = cmd_new},         {.name = "add", .fn = cmd_add},
    {.name = "install", .fn = cmd_install}, {.name = "extract", .fn = cmd_extract},
    {.name = "dir", .fn = cmd_dir},
    {.name = "era", .fn = cmd_era},
    {0}};

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        usage();
        return 0;
    }

    sysgen_init_paths(argv[0]);

    const char *cmd = argv[1];

    if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help") || !strcmp(cmd, "help"))
    {
        usage();
        return 0;
    }

    for (int i = 0; g_cmds[i].name; i++)
    {
        if (!strcmp(cmd, g_cmds[i].name))
            return g_cmds[i].fn(argc - 1, argv + 1);
    }

    err("unknown command '%s'", cmd);
    usage();
    return 1;
}
