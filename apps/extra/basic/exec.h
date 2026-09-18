/*
 * apps/extra/basic/exec.h — Internal declarations for statement executors
 *
 * Bridges the split execution files: exec_core.c owns the statement
 * dispatcher and exec_flow.c implements the individual handlers, so each
 * side needs the other's entry points.
 */

#ifndef BASIC_EXEC_H
#define BASIC_EXEC_H

#include "basic.h"

/* Statement handlers (exec_flow.c) */
void exec_print(BasicState *s);
void exec_input(BasicState *s);
void exec_goto(BasicState *s);
void exec_gosub(BasicState *s);
void exec_return(BasicState *s);
void exec_for(BasicState *s);
void exec_next(BasicState *s);
void exec_poke(BasicState *s);
void exec_dim(BasicState *s);
void exec_def(BasicState *s);

#endif /* BASIC_EXEC_H */