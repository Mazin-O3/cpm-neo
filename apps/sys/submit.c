#include <ccplib.h>
#include <stdio.h>
#include <string.h>

#define SUBMIT_LINE_MAX 128

/* SUBMIT files are always type .SUB: append when missing, reject others. */
static int sub_resolve_ext(char *name)
{
    const char *dot = strrchr(name, '.');

    if (dot && dot[1] != '\0')
        return strcasecmp(dot, ".SUB") == 0 ? 0 : EINVAL;

    int len = strlen(name);

    if (len > 0 && name[len - 1] == '.') /* Trailing dot = no extension */
        name[--len] = '\0';

    strcpy(name + len, ".SUB");

    return 0;
}

static int subst_params(char *dst, int dst_sz, const char *src, int argc, char **argv)
{
    int di = 0;

    for (int si = 0; src[si] && di < dst_sz - 1;)
    {
        if (src[si] == '$')
        {
            if (src[si + 1] >= '1' && src[si + 1] <= '9')
            {
                int n = src[si + 1] - '0';
                int idx = n + 1;

                if (idx < argc)
                {
                    const char *param = argv[idx];

                    while (*param && di < dst_sz - 1)
                        dst[di++] = *param++;
                }

                si += 2;
            }
            else if (src[si + 1] == '$')
            {
                dst[di++] = '$';
                si += 2;
            }
            else
                dst[di++] = src[si++];
        }
        else
            dst[di++] = src[si++];
    }

    dst[di] = '\0';

    return 0;
}

static int write_batch_line(int dst_fd, const char *line, int argc, char **argv)
{
    if (line[0] == '\0' || line[0] == ';')
        return 0;

    char out[SUBMIT_LINE_MAX];

    int rc = subst_params(out, sizeof(out), line, argc, argv);

    if (rc != EOK)
        return rc;

    int len = strlen(out);

    if (write(dst_fd, out, len) != len)
        return EIO;

    if (write(dst_fd, "\n", 1) != 1)
        return EIO;

    return EOK;
}

static int sub_process(int8_t vol, const char *src_name, int argc, char **argv)
{
    int src_fd = open(src_name, "r");

    if (src_fd < 0)
        return ENOENT;

    /* The batch file always lands in user 0 of the current volume so the
     * resident try_run_batch can find it even if the batch changes USER. */
    char batch_path[BATCH_PATH_LEN];

    make_batch_path(batch_path, vol);

    int dst_fd = open(batch_path, "w");

    if (dst_fd < 0)
    {
        close(src_fd);
        return EIO;
    }

    char line[SUBMIT_LINE_MAX];

    int rc = EOK;

    while (rc == EOK && readline(src_fd, line, sizeof(line)) > 0)
        rc = write_batch_line(dst_fd, line, argc, argv);

    close(src_fd);

    if (close(dst_fd) != EOK && rc == EOK)
        rc = EIO;

    if (rc != EOK)
    {
        remove(batch_path);
        return rc;
    }

    return EOK;
}

int main(int argc, char **argv)
{
    FsContext ctx;

    sys_getctx(&ctx);

    int rc = EOK;

    if (argc < 2 || argc > 11)
    {
        cmderr_print(cmderr_syntax(NULL));
        rc = EINVAL;
    }
    else if (sys_getenv(ENV_BATCH_OFFSET) != 0)
    {
        printf("SUBMIT: A batch is already running\n");
        rc = EPERM;
    }
    else
    {
        char name[FSPATH_MAX];

        name_copy(name, argv[1], sizeof(name) - 1);

        if (sub_resolve_ext(name) != EOK)
        {
            printf("SUBMIT: Not a .SUB file\n");
            rc = EINVAL;
        }
        else
        {
            rc = sub_process(ctx.vol_id, name, argc, argv);

            if (rc == ENOENT)
                printf("SUBMIT: No SUB file found\n");
            else if (rc != EOK)
                printf("SUBMIT: Cannot build $$$.SUB\n");
        }
    }

    return rc;
}
