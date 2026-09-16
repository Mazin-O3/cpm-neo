/*
 * apps/extra/basic/basic.h — BASIC interpreter
 *
 * Token/keyword enums, state structures, and the lexer / expression /
 * statement / program-management API.
 */

#ifndef BASIC_H
#define BASIC_H

#include <cpmneo.h>

#define BASIC_PROG_MAX    4096
#define BASIC_FOR_DEPTH   8
#define BASIC_GOSUB_DEPTH 32
#define BASIC_NUM_VARS    26
#define BASIC_STR_LEN     64
#define BASIC_LINE_LEN    96

#define BASIC_ARR_SZ     16
#define BASIC_MAX_DIM    (BASIC_ARR_SZ - 1)
#define BASIC_TOKEN_BASE 0x80

enum
{
    T_NUM,
    T_VAR,
    T_STR,
    T_SYM,
    T_KEY,
    T_FN,
    T_EOF
};

enum
{
    K_LET,
    K_PRINT,
    K_INPUT,
    K_GOTO,
    K_GOSUB,
    K_RETURN,
    K_IF,
    K_THEN,
    K_FOR,
    K_TO,
    K_STEP,
    K_NEXT,
    K_END,
    K_REM,
    K_AND,
    K_OR,
    K_LIST,
    K_LOAD,
    K_RUN,
    K_NEW,
    K_POKE,
    K_EXIT,
    K_PEEK,
    K_ABS,
    K_SGN,
    K_RND,
    K_DEF,
    K_DIM,
    K_FRE,
    K_CLR,
    K_SAVE
};

/* Keyword table access */
int         lexer_kw_id(const char *w);
const char *lexer_kw_name(int kw);

typedef struct
{
    char  data[BASIC_PROG_MAX];
    char *free_ptr;
} BasicProg;

typedef struct
{
    int  val[BASIC_NUM_VARS];
    char str[BASIC_NUM_VARS][BASIC_STR_LEN];
    int  arr[BASIC_NUM_VARS][BASIC_ARR_SZ];
    int  dim[BASIC_NUM_VARS];
} BasicVar;

typedef struct
{
    char *instr_ptr;
    int   stopped;
    int   lineno;
} BasicCtrl;

typedef struct
{
    int   var_idx[BASIC_FOR_DEPTH];
    int   tgt[BASIC_FOR_DEPTH];
    int   step[BASIC_FOR_DEPTH];
    char *ret_instr_ptr[BASIC_FOR_DEPTH];
    char *ret_src[BASIC_FOR_DEPTH];
    int   stack_ptr;
    char *resume;
} BasicLoop;

typedef struct
{
    char *stk[BASIC_GOSUB_DEPTH];
    int   stack_ptr;
} BasicGosub;

typedef struct
{
    int   param_var_idx[BASIC_NUM_VARS];
    char  text[BASIC_NUM_VARS][BASIC_LINE_LEN];
    char *body[BASIC_NUM_VARS];
} BasicFn;

typedef struct
{
    char *ptr;
    char  buf[BASIC_STR_LEN];
    int   type;
    int   num;
    int   kw;
    int   is_string; /* Replaced ambiguous 'quote' */
} BasicLex;

typedef struct
{
    BasicProg  prog;
    BasicVar   var;
    BasicCtrl  ctrl;
    BasicLoop  loop;
    BasicGosub gosub;
    BasicFn    fn;
    BasicLex   lex;
} BasicState;

/* Error and control */
void ctrl_error(BasicState *s, const char *msg);
int  ctrl_break_key(BasicState *s);
void exec_syntax_err(BasicState *s);

/* Lexer */
int  lexer_next(BasicState *s);
void lexer_skip_line(BasicState *s);

/* Variables */
int var_aget(BasicState *s, int var_idx, int arr_idx, int *out_val);
int var_aset(BasicState *s, int var_idx, int arr_idx, int val);
int var_read_str(BasicState *s, char *buf, size_t buf_size);

/* Expression evaluation */
int expr_parse_paren(BasicState *s);
int expr_eval(BasicState *s);

/* Statement execution */
void exec_line(BasicState *s, const char *text);
void exec_stmt(BasicState *s);

/* Program management */
void  tokenize_line(char *dst, unsigned max_dst, const char *src);
char *entry_next(char *p);
int   prog_load(BasicState *s, const char *path);
void  prog_del_line(BasicState *s, int n);
void  prog_add_line(BasicState *s, int n, const char *t);
char *prog_find_line(BasicState *s, int n);
void  prog_list(BasicState *s);
void  prog_new(BasicState *s);
void  prog_run(BasicState *s);
void  clr_vars(BasicState *s);

#endif /* BASIC_H */