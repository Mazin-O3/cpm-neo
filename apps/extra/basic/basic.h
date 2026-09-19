/*
 * apps/extra/basic/basic.h — Tiny BASIC interpreter
 *
 * Token-based interpreter with Pratt parser expression evaluator.
 * Supports 26 numeric + 26 string variables, DIM arrays, PEEK/POKE,
 * DEF FN, and tokenized program storage for compact memory usage.
 */

#ifndef BASIC_H
#define BASIC_H

#include <cpmneo.h>

/* Limits */
#define BASIC_PROG_MAX    4096
#define BASIC_FOR_DEPTH   8
#define BASIC_GOSUB_DEPTH 32
#define BASIC_FN_DEPTH    8
#define BASIC_NUM_VARS    26
#define BASIC_STR_LEN     64
#define BASIC_LINE_LEN    96

#define BASIC_ARR_SZ     16
#define BASIC_MAX_DIM    (BASIC_ARR_SZ - 1)
#define BASIC_TOKEN_BASE 0x80

/* Largest legal line number */
#define BASIC_MAX_LINE 65535

/* PEEK/POKE address */
#define BASIC_ADDR(a) ((volatile uint8_t *)(uintptr_t)(unsigned)(a))

/* Token types */
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

/* Keywords — order must match kw_names[] table in lex.c */
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
    K_SAVE,
    K_MOD,
    K_NOT
};

/* Keyword table access */
int         lexer_kw_id(const char *w);
const char *lexer_kw_name(int kw);

/* State structures */

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

/*
 * jump is set by every statement that transfers control (GOTO, GOSUB,
 * RETURN, IF..THEN n, NEXT looping back).  It is what tells exec_line()
 * and the run loop that instr_ptr was deliberately changed — comparing
 * pointers cannot see a jump to the *current* line.
 */
typedef struct
{
    char *instr_ptr;
    int   stopped;
    int   lineno;
    int   jump;
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

/* stk[] = program line to return to (NULL = GOSUB issued from direct mode),
 * src[] = position inside that line just after the GOSUB target number. */
typedef struct
{
    char *stk[BASIC_GOSUB_DEPTH];
    char *src[BASIC_GOSUB_DEPTH];
    int   stack_ptr;
} BasicGosub;

typedef struct
{
    int   param_var_idx[BASIC_NUM_VARS];
    char  text[BASIC_NUM_VARS][BASIC_LINE_LEN];
    char *body[BASIC_NUM_VARS];
    int   depth;
} BasicFn;

typedef struct
{
    char *ptr;
    char  buf[BASIC_LINE_LEN];
    int   type;
    int   num;
    int   kw;
    int   is_string;
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

/* Lexer */
int  lexer_next(BasicState *s);
void lexer_skip_line(BasicState *s);
int  lex_chk_sym(BasicState *s, char ch);
int  lex_expect_sym(BasicState *s, char ch);
int  lex_expect_key(BasicState *s, int kw);

/* Expression evaluation */
int expr_eval(BasicState *s);
int expr_parse_paren(BasicState *s);

/* Statement execution */
void exec_line(BasicState *s, const char *text);
void exec_stmt(BasicState *s);

/* Program management */
void  tokenize_line(char *dst, unsigned max_dst, const char *src);
char *entry_next(char *p);
int   prog_load(BasicState *s, const char *path);
void  prog_del_line(BasicState *s, int n);
int   prog_add_line(BasicState *s, int n, const char *t);
int   prog_enter_line(BasicState *s, char *line);
char *prog_find_line(BasicState *s, int n);
void  prog_list(BasicState *s);
void  prog_new(BasicState *s);
void  prog_run(BasicState *s);
void  prog_continue(BasicState *s);
void  clr_vars(BasicState *s);

#endif /* BASIC_H */
