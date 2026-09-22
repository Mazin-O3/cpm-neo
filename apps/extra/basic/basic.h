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
#define BASIC_PROG_MAX    8192
#define BASIC_FOR_DEPTH   8
#define BASIC_GOSUB_DEPTH 32
#define BASIC_FN_DEPTH    8
#define BASIC_FN_LEN      64            /* Longest DEF FN body, incl. NUL */
#define BASIC_NUM_VARS    26

/* Variable names: one letter, optionally followed by ONE digit (A, A0..A9, B, B0 ...).
 * A T_VAR token's `num` is a "name index" = letter * BASIC_SUBS + sub, with sub = 0 for a
 * plain letter and 1..10 for the digits 0..9.  Numeric scalars use the whole name index;
 * strings and arrays are letter-only and use NAME_LETTER(). */
#define BASIC_SUBS        11
#define BASIC_NUM_NAMES   (BASIC_NUM_VARS * BASIC_SUBS)
#define NAME_LETTER(n)    ((n) / BASIC_SUBS)
#define NAME_PLAIN(n)     ((n) % BASIC_SUBS == 0)
#define BASIC_STR_LEN     64
#define BASIC_LINE_LEN    96

/* Longest source line (characters) that can be typed or loaded.  Read buffers
 * are one byte bigger, so an over-long line fills the buffer and is detected
 * instead of being silently cut or split. */
#define BASIC_SRC_MAX     (BASIC_LINE_LEN - 1)
#define BASIC_READ_BUF    (BASIC_LINE_LEN + 1)

/* Set to 1 ONLY if the console getline() returns an over-long line in
 * successive chunks (like fgets).  The REPL then discards the tail chunks
 * after ?LINE TOO LONG so they are not run as a second, bogus line.  Leave at
 * 0 if getline() truncates and drops the rest: draining would then swallow
 * the next real input line. */
#ifndef BASIC_DRAIN_LONG
#define BASIC_DRAIN_LONG  0
#endif

/* Arrays share one pool of ints, carved up by DIM (A(n) or A(n,m); indices run
 * 0..n).  Only what is dimensioned uses memory, and there is no per-array cap. */
#define BASIC_POOL_INTS  1024
#define BASIC_TOKEN_BASE 0x80

/* Largest legal line number: entries store it as a little-endian u16. */
#define BASIC_MAX_LINE 65535

/* PEEK/POKE address: the full int is used (no masking).  Going through
 * unsigned first means negative values map to the top half of the address
 * space (-1 == 0xFFFFFFFF with 32-bit int) instead of sign-extending. */
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
    K_NOT,
    K_SQR
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
    int  pool_top;                           /* next free pool slot */
    int  pool[BASIC_POOL_INTS];
    int  val[BASIC_NUM_NAMES];
    char str[BASIC_NUM_VARS][BASIC_STR_LEN];
    int  a_base[BASIC_NUM_VARS];             /* first element in pool */
    int  a_rows[BASIC_NUM_VARS];             /* n+1 for the first subscript; 0 = not dimensioned */
    int  a_cols[BASIC_NUM_VARS];             /* m+1 for the second subscript; 0 = one-dimensional */
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
    char  text[BASIC_NUM_VARS][BASIC_FN_LEN];
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
int *expr_array_ref(BasicState *s, int vn);

/* Statement execution */
void exec_line(BasicState *s, const char *text);
void exec_stmt(BasicState *s);

/* Statement handlers  */
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

/* Program management */
void  tokenize_line(char *dst, unsigned max_dst, const char *src);
int   entry_line(const char *p);
char *entry_text(char *p);
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