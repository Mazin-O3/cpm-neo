#include "basic.h"

/* Error reporting */

void ctrl_error(BasicState *s, const char *msg) {
  if (s->ctrl.lineno)
    printf("\n?%s IN LINE %d\n", msg, s->ctrl.lineno);
  else
    printf("\n?%s\n", msg);
  s->ctrl.stopped = 1;
}

/* Array access */

int var_aget(BasicState *s, int var_idx, int arr_idx, int *out_val) {
  if (s->var.dim[var_idx] == 0) {
    ctrl_error(s, "UNDIMENSIONED ARRAY");
    return -1;
  }

  if (arr_idx < 0 || arr_idx >= s->var.dim[var_idx]) {
    ctrl_error(s, "SUBSCRIPT OUT OF RANGE");
    return -1;
  }
  *out_val = s->var.arr[var_idx][arr_idx];
  return 0;
}

int var_aset(BasicState *s, int var_idx, int arr_idx, int val) {
  if (s->var.dim[var_idx] == 0) {
    ctrl_error(s, "UNDIMENSIONED ARRAY");
    return -1;
  }

  if (arr_idx < 0 || arr_idx >= s->var.dim[var_idx]) {
    ctrl_error(s, "SUBSCRIPT OUT OF RANGE");
    return -1;
  }

  s->var.arr[var_idx][arr_idx] = val;
  return 0;
}

/* String variable reading */

int var_read_str(BasicState *s, char *buf, size_t buf_size) {
  if (s->lex.type == T_STR) {
    strncpy(buf, s->lex.buf, buf_size - 1);
    buf[buf_size - 1] = '\0';
    lexer_next(s);
    return 1;
  }

  if (s->lex.type == T_VAR && s->lex.is_string) {
    strncpy(buf, s->var.str[s->lex.num], buf_size - 1);
    buf[buf_size - 1] = '\0';
    lexer_next(s);
    return 1;
  }

  return 0;
}
