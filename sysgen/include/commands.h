/*
 * sysgen/include/commands.h — Sysgen host CLI commands
 *
 * Dispatches the top-level sysgen verbs: new, add, era, install, extract
 * and dir.
 */

#ifndef SYSGEN_COMMANDS_H
#define SYSGEN_COMMANDS_H

int cmd_new(int argc, char **argv);
int cmd_add(int argc, char **argv);
int cmd_era(int argc, char **argv);
int cmd_install(int argc, char **argv);
int cmd_extract(int argc, char **argv);
int cmd_dir(int argc, char **argv);

#endif