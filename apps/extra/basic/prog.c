#include "basic.h"

/* Tokenization */

void tokenize_line(char *dst, unsigned max_dst, const char *src) {
  unsigned n = 0;

  while (*src && n + 1 < max_dst) {
    if (*src == '"') {
      *dst++ = *src++;
      n++;

      while (*src && *src != '"' && n + 1 < max_dst) {
        *dst++ = *src++;
        n++;
      }

      if (*src == '"' && n + 1 < max_dst) {
        *dst++ = *src++;
        n++;
      }

      continue;
    }

    if (isalpha(*src)) {
      char word[64];
      int i = 0;

      while (i < 63 && src[i] && isalpha(src[i])) {
        word[i] = toupper(src[i]);
        i++;
      }

      word[i] = 0;
      int kw = lexer_kw_id(word);

      if (kw >= 0) {
        if (n + 1 >= max_dst)
          break;
        *dst++ = (unsigned char)(BASIC_TOKEN_BASE + kw);
        n++;
        src += i;

        if (kw == K_REM) {
          while (*src && n + 1 < max_dst) {
            *dst++ = *src++;
            n++;
          }
          *dst = 0;
          return;
        }
      } else {
        if (n + (unsigned)i >= max_dst)
          break;
        memcpy(dst, src, i);
        dst += i;
        n += i;
        src += i;
      }

      continue;
    }

    if (n + 1 >= max_dst)
      break;
    *dst++ = *src++;
    n++;
  }
  *dst = 0;
}

/* Program entry traversal */

char *entry_next(char *p) {
  p += 2;

  while (*p)
    p++;
  return p + 1;
}

char *prog_find_line(BasicState *s, int n) {
  char *p = s->prog.data;

  while (p < s->prog.free_ptr) {
    int num = (unsigned char)p[0] | ((unsigned char)p[1] << 8);

    if (num == n)
      return p;
    p = entry_next(p);
  }

  return NULL;
}

/* Program line management */

void prog_del_line(BasicState *s, int n) {
  char *p = prog_find_line(s, n);

  if (!p)
    return;

  char *next = entry_next(p);

  int rest = (int)(s->prog.free_ptr - next);

  memmove(p, next, rest);

  s->prog.free_ptr -= (int)(next - p);
}

void prog_add_line(BasicState *s, int n, const char *t) {
  prog_del_line(s, n);

  if (!*t)
    return;

  char tokened[512];

  tokenize_line(tokened, sizeof(tokened), t);

  int len = 2 + strlen(tokened) + 1;

  char *prev = s->prog.data;
  char *ins = s->prog.data;

  while (ins < s->prog.free_ptr) {
    int num = (unsigned char)ins[0] | ((unsigned char)ins[1] << 8);

    if (num > n)
      break;
    prev = entry_next(ins);
    ins = prev;
  }

  if (s->prog.free_ptr + len > s->prog.data + BASIC_PROG_MAX) {
    printf("\n?PROGRAM FULL\n");
    return;
  }

  int rest = (int)(s->prog.free_ptr - ins);

  memmove(ins + len, ins, rest);

  ins[0] = n & 0xFF;

  ins[1] = (n >> 8) & 0xFF;

  memcpy(ins + 2, tokened, len - 2);

  s->prog.free_ptr += len;
}

/* Program listing */

void prog_list(BasicState *s) {
  int rows = 0;

  char *p = s->prog.data;

  while (p < s->prog.free_ptr) {
    int num = (unsigned char)p[0] | ((unsigned char)p[1] << 8);
    printf("%d ", num);
    const char *text = p + 2;

    while (*text) {
      if ((unsigned char)*text >= BASIC_TOKEN_BASE) {
        printf("%s", lexer_kw_name((unsigned char)*text - BASIC_TOKEN_BASE));
        text++;
      } else
        putchar(*text++);
    }

    printf("\n");

    if (anykey("...", &rows, CONSOLE_HEIGHT)) {
      printf("\n");
      break;
    }

    p = entry_next(p);
  }
}

/* Program initialization and control */

static void clear_vars_and_fns(BasicState *s) {
  s->loop.stack_ptr = -1;

  s->gosub.stack_ptr = -1;

  for (int i = 0; i < BASIC_NUM_VARS; i++) {
    s->var.val[i] = 0;
    s->var.str[i][0] = 0;
    s->var.dim[i] = 0;
    s->fn.param_var_idx[i] = -1;
    s->fn.body[i] = 0;
  }

  s->loop.resume = 0;
}

void prog_new(BasicState *s) {
  clear_vars_and_fns(s);
  s->prog.free_ptr = s->prog.data;
  s->ctrl.instr_ptr = NULL;
  s->ctrl.stopped = 0;
  s->ctrl.lineno = 0;
}

void clr_vars(BasicState *s) { clear_vars_and_fns(s); }

/* Program execution */

void prog_run(BasicState *s) {
  if (s->prog.free_ptr == s->prog.data) {
    printf("\n?NO PROGRAM\n");
    return;
  }

  /* RUN clears variables but keeps DEF FN definitions (classic BASIC). */

  for (int i = 0; i < BASIC_NUM_VARS; i++) {
    s->var.val[i] = 0;
    s->var.str[i][0] = 0;
    s->var.dim[i] = 0;
  }

  s->loop.stack_ptr = -1;
  s->gosub.stack_ptr = -1;
  s->ctrl.instr_ptr = s->prog.data;
  s->ctrl.stopped = 0;
  s->loop.resume = 0;

  while (s->ctrl.instr_ptr && s->ctrl.instr_ptr < s->prog.free_ptr &&
         !s->ctrl.stopped) {
    char *cur = s->ctrl.instr_ptr;

    s->ctrl.lineno = (unsigned char)cur[0] | ((unsigned char)cur[1] << 8);

    exec_line(s, cur + 2);

    if (!s->ctrl.stopped) {
      if (s->ctrl.instr_ptr == cur && !s->loop.resume)
        s->ctrl.instr_ptr = entry_next(cur);
    }
  }

  s->ctrl.lineno = 0;
}
