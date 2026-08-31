#ifndef CCP_H
#define CCP_H

#include <cpm.h>
#include <ccplib.h>

/* Limits */
#define CCP_LINE_MAX 128
#define CCP_ARGC_MAX 32

int main(void);

CmdErr ccp_dispatch(char *line);
int ccp_setuser(FsContext *ctx, uint8_t ua);

CmdErr try_implicit_run(FsContext *ctx, int argc, char **argv);
void try_run_batch(FsContext *ctx);

/* Resident commands */
CmdErr cmd_era(FsContext *ctx, int argc, char **argv);
CmdErr cmd_ren(FsContext *ctx, int argc, char **argv);
CmdErr cmd_type(FsContext *ctx, int argc, char **argv);

CmdErr cmd_dir(FsContext *ctx, int argc, char **argv);
CmdErr cmd_dirs(FsContext *ctx, int argc, char **argv);
CmdErr cmd_user(FsContext *ctx, int argc, char **argv);

CmdErr cmd_cls(FsContext *ctx, int argc, char **argv);
CmdErr cmd_echo(FsContext *ctx, int argc, char **argv);
CmdErr cmd_sync(FsContext *ctx, int argc, char **argv);

#endif /* CCP_H */