/*
 * apps/sys/help.c — HELP command implementation
 *
 * Prints a grouped command listing, or detail for a named command.
 * The command records are embedded in the binary (kept in sync with
 * the resident CCP table in core/ccp/ccp.c and the transient apps).
 */

#include <ccplib.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    const char *name;
    const char *cat;
    const char *usage;
    const char *desc;
    const char *detail;
} HelpRec;

static const char *const cat_names[] = {
    "File Operations",
    "System",
    "Other",
};

static const HelpRec g_recs[] = {

    {"DIR", "file", "DIR [v[u]:][filespec]", "List directory entries",
     "Lists files on the specified volume and user area.\n"
     "Supports ? and * wildcards. SYS files are hidden."},

    {"DIRS", "file", "DIRS [v[u]:][filespec]", "List system files",
     "Lists SYS files hidden from normal DIR.\n"
     "Same wildcards and volume/user syntax as DIR."},

    {"ERA", "file", "ERA [v[u]:]filespec", "Delete file(s)",
     "Deletes matching files. Supports ? and * wildcards."},

    {"REN", "file", "REN [v[u]:]old [v[u]:]new", "Rename file(s)",
     "Renames files. Both names must use the same volume and user area.\n"
     "Supports ? and * wildcards."},

    {"TYPE", "file", "TYPE [v[u]:]filespec", "Display file contents",
     "Displays file contents as text."},

    {"DUMP", "file", "DUMP [v[u]:]filespec", "Display file contents as hex",
     "Displays a formatted hex dump with offsets, hex bytes, and text."},

    {"COPY", "file", "COPY [v[u]:]src [v[u]:]dst", "Copy file(s)",
     "Copies files from source to destination. The source supports ? and * wildcards.\n"
     "A single volume/user target copies from the current volume and user area."},

    {"SUBMIT", "system", "SUBMIT filename.sub [$1..$9]", "Execute a batch file",
     "Executes a .SUB batch file with $1-$9 parameter substitution.\n"
     "Parameters not supplied substitute as empty.\n"
     "Lines starting with ';' are comments. Use $$ for a literal $.\n"
     "Lines starting with ':' execute only if the previous command succeeded.\n"
     "Press ESC between commands to abort."},

    {"STAT", "system", "STAT [v[u]:][filespec]", "Show file or disk status",
     "With a filespec, shows file status. With no argument, shows disk statistics.\n"
     "Use STAT DSK: to show all volumes."},

    {"SET", "system", "SET [v:]file ATTR", "Set file or volume attributes",
     "Attributes:\n"
     "  RO      Read-only\n"
     "  RW      Read-write\n"
     "  SYS     System file\n"
     "  DIR     Directory file\n"
     "  MT      Mount volume\n"
     "  RZ +N   Grow volume by N KB\n"
     "  RZ -N   Shrink volume by N KB\n"
     "  UM      Unmount volume"},

    {"USER", "system", "USER [u]", "Show or set user area",
     "Shows or sets the current user area (0-15)."},

    {"SYS", "system", "SYS", "Show system information",
     "Shows OS, kernel, and CCP versions, TPA size, disk capacity,\n"
     "and mounted volumes."},

    {"CLS", "other", "CLS", "Clear screen", "Clears the console screen."},
    
    {"SYNC", "other", "SYNC", "Flush disk writes to storage",
     "Commits all pending disk writes to persistent storage. The platform\n"
     "persistence barrier flushes any write-back cache. Done automatically\n"
     "on program exit."},
     
    {"ECHO", "other", "ECHO [arg ...]", "Display arguments to console",
     "Displays the arguments, separated by a single space character\n"
     "and followed by a newline."},

    {"HELP", "other", "HELP [command]", "Show help information",
     "Lists commands, or shows detailed help for a command."},

    {0},

};

static int cat_of(const char *cat)
{
    if (strcmp(cat, "file") == 0)
        return 0;

    if (strcmp(cat, "system") == 0)
        return 1;

    if (strcmp(cat, "other") == 0)
        return 2;

    return -1;
}

/* Print detail for a named command.  Returns 1 if found, 0 if not. */
static int print_one(const char *name)
{
    for (const HelpRec *r = g_recs; r->name; r++)
    {
        if (strcasecmp(r->name, name) != 0)
            continue;

        printf("\n%s -- %s\n\n", r->name, r->desc);

        printf("  %s\n\n", r->usage);

        if (r->detail[0])
        {
            printf("%s", r->detail);
            printf("\n\n");
        }

        return 1;
    }

    return 0;
}

/* Print the full listing grouped by category. */
static void print_all(void)
{
    for (int ci = 0; ci < 3; ci++)
    {
        int header = 0;

        for (const HelpRec *r = g_recs; r->name; r++)
        {
            if (cat_of(r->cat) != ci)
                continue;

            if (!header)
            {
                printf("\n  %s:\n", cat_names[ci]);
                header = 1;
            }

            const char *u = r->usage + strlen(r->name);

            while (*u == ' ')
                u++;

            printf("    %-6s %-22s  %s\n", r->name, u, r->desc);
        }
    }

    printf("\n");
}

int main(int argc, char **argv)
{
    if (argc > 2)
    {
        cmderr_print(cmderr_syntax(NULL));
        return CMDERR_SYNTAX;
    }

    if (argc > 1)
    {
        if (!print_one(argv[1]))
        {
            printf("%s?\n", argv[1]);
            return ENOENT;
        }

        return EOK;
    }

    print_all();

    return EOK;
}
