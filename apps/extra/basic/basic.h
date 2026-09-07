#ifndef BASIC_H
#define BASIC_H

#include <cpm.h>

#define MAX_TEXT  4096
#define FOR_MAX   8
#define GS_MAX    32
#define NVARS     26
#define STR_SZ    64
#define BASIC_LINE_SZ   96
#define ARR_MAX   16
#define DIM_MAX   (ARR_MAX - 1)  /* Largest subscript accepted by DIM */
#define TOK_BASE  0x80

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

extern const char *lex_kw_names[];

typedef struct
{
    char data[MAX_TEXT];
    char *free;
} BasicProg;

typedef struct
{
    int val[NVARS];
    char str[NVARS][STR_SZ];
    int arr[NVARS][ARR_MAX];
    int dim[NVARS];
} BasicVar;

typedef struct
{
    char *ip;
    int stopped, lineno;
} BasicCtl;

typedef struct
{
    int var[FOR_MAX], tgt[FOR_MAX], step[FOR_MAX];
    char *ret_ip[FOR_MAX];
    char *ret_src[FOR_MAX];
    int sp;
    char *resume;
} BasicLoop;

typedef struct
{
    char *stk[GS_MAX];
    int sp;
} BasicGosub;

typedef struct
{
    int param[NVARS];
    char text[NVARS][BASIC_LINE_SZ]; /* Owned copy of each body (stable across line edits) */
    char *body[NVARS];
} BasicFn;

typedef struct
{
    char *p;
    char buf[STR_SZ];
    int type, num, kw, quote;
} BasicLex;

typedef struct
{
    BasicProg prog;
    BasicVar var;
    BasicCtl ctl;
    BasicLoop loop;
    BasicGosub gosub;
    BasicFn fn;
    BasicLex lex;
} BasicState;

/* Error and control */
void ctl_error       (BasicState *s, const char *msg);
int  ctl_break_key   (BasicState *s);
void exec_syntax_err (BasicState *s);

/* Tokeniser */
int  lex_kw_id       (const char *w);
int  lex_next        (BasicState *s);
void lex_skip_line   (BasicState *s);

/* Variables */
int  var_aget        (BasicState *s, int vn, int idx, int *v);
int  var_aset        (BasicState *s, int vn, int idx, int val);
int  var_read_str    (BasicState *s, char *buf);

/* Expression evaluation */
int expr_paren       (BasicState *s);
int expr_eval        (BasicState *s);

/* Statement execution */
void exec_line       (BasicState *s, const char *t);
void exec_stmt       (BasicState *s);

/* Tokeniser */
void tokenize_line(char *dst, unsigned max_dst, const char *src);

/* Program management */
void prog_del_line   (BasicState *s, int n);
void prog_add_line   (BasicState *s, int n, const char *t);
char *prog_find_line  (BasicState *s, int n);
char *entry_next      (char *p);
void prog_list       (BasicState *s);
void prog_new        (BasicState *s);
void clr_vars        (BasicState *s);
void prog_run        (BasicState *s);
int  prog_load       (BasicState *s, const char *path);

#endif
