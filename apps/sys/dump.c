/* DUMP.COM — Prints a file as a formatted hex dump.  Extracted from the
 * resident TYPE command's `h` flag; the transient app keeps the CCP small. */

#include <ccplib.h>

static CmdErr cmd_dump(FsContext *ctx, int argc, char **argv)
{
    if (!check_fmt(argc, argv, "f"))
        return cmderr_syntax(NULL);

    int fd = open(argv[1], "r");

    if (fd < 0)
    {
        if (fd == ENOENT)
            return cmderr_errno(ENOENT);

        return cmderr_bdos(vol_from_arg(argv[1], ctx->vol_id), fd);
    }

    Pager pg = pager_start();

    uint8_t buf[128];
    int n, stop = 0;
    uint32_t addr = 0;

    while (!stop && (n = read(fd, buf, sizeof(buf))) > 0)
    {
        for (int offset = 0; offset < n && !stop; offset += 16)
        {
            int chunk_size = (n - offset > 16) ? 16 : (n - offset);
            uint8_t *chunk = &buf[offset];

            printf("%04X:", addr);

            for (int i = 0; i < 16; i++)
            {
                if (i == 8 && chunk_size > 8)
                    printf(" ");

                if (i < chunk_size)
                    printf(" %02X", chunk[i]);
                else
                    printf("   ");
            }

            printf("  ");

            for (int i = 0; i < chunk_size; i++)
            {
                if (i == 8)
                    printf(" ");
                char c = (char)chunk[i];
                putchar((c >= 0x20 && c < 0x7F) ? c : '.');
            }

            printf("\n");
            addr += (uint32_t)chunk_size;

            if (pager_line(&pg))
                stop = 1;
        }
    }

    if (n < 0)
    {
        putchar('\n');
        close(fd);
        return cmderr_errno(EIO);
    }

    putchar('\n');
    close(fd);
    return cmderr_ok();
}

int main(int argc, char **argv)
{
    return ccp_run_app(cmd_dump, argc, argv);
}
