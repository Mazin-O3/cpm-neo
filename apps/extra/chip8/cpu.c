#include "chip8.h"

static void alu_ld(Chip8State *s, uint8_t x, uint8_t y)
{
    s->v[x] = s->v[y];
}

static void alu_or(Chip8State *s, uint8_t x, uint8_t y)
{
    s->v[x] |= s->v[y];
}

static void alu_and(Chip8State *s, uint8_t x, uint8_t y)
{
    s->v[x] &= s->v[y];
}

static void alu_xor(Chip8State *s, uint8_t x, uint8_t y)
{
    s->v[x] ^= s->v[y];
}

static void alu_add(Chip8State *s, uint8_t x, uint8_t y)
{
    uint16_t sum = s->v[x] + s->v[y];

    s->v[0xF] = sum > 0xFF;
    s->v[x] = sum & 0xFF;
}

static void alu_sub(Chip8State *s, uint8_t x, uint8_t y)
{
    s->v[0xF] = s->v[x] >= s->v[y];
    s->v[x] -= s->v[y];
}

static void alu_shr(Chip8State *s, uint8_t x, uint8_t y)
{
    (void)y;

    s->v[0xF] = s->v[x] & 1;
    s->v[x] >>= 1;
}

static void alu_subn(Chip8State *s, uint8_t x, uint8_t y)
{
    s->v[0xF] = s->v[y] >= s->v[x];
    s->v[x] = s->v[y] - s->v[x];
}

static void alu_shl(Chip8State *s, uint8_t x, uint8_t y)
{
    (void)y;

    s->v[0xF] = s->v[x] >> 7;
    s->v[x] <<= 1;
}

typedef void (*AluFn)(Chip8State *s, uint8_t x, uint8_t y);

static const AluFn alu_tab[16] = {
    [0x0] = alu_ld,  [0x1] = alu_or,  [0x2] = alu_and,  [0x3] = alu_xor, [0x4] = alu_add,
    [0x5] = alu_sub, [0x6] = alu_shr, [0x7] = alu_subn, [0xE] = alu_shl,
};

static void fx_delay_get(Chip8State *s, uint8_t x, uint8_t op)
{
    (void)op;

    s->v[x] = s->delay;
}

static void fx_wait_key(Chip8State *s, uint8_t x, uint8_t op)
{
    (void)op;

    s->waiting = 1;
    s->wait_reg = x;
}

static void fx_delay_set(Chip8State *s, uint8_t x, uint8_t op)
{
    (void)op;

    s->delay = s->v[x];
}

static void fx_i_add_vx(Chip8State *s, uint8_t x, uint8_t op)
{
    (void)op;

    s->i += s->v[x];
}

static void fx_i_digit(Chip8State *s, uint8_t x, uint8_t op)
{
    (void)op;

    s->i = CH8_FONT + (s->v[x] & 0xF) * 5;
}

static void fx_bcd(Chip8State *s, uint8_t x, uint8_t op)
{
    (void)op;

    s->ram[s->i] = s->v[x] / 100;
    s->ram[s->i + 1] = (s->v[x] / 10) % 10;
    s->ram[s->i + 2] = s->v[x] % 10;
}

static void fx_store(Chip8State *s, uint8_t x, uint8_t op)
{
    (void)op;

    for (uint8_t k = 0; k <= x; k++)
        s->ram[s->i + k] = s->v[k];
}

static void fx_load(Chip8State *s, uint8_t x, uint8_t op)
{
    (void)op;

    for (uint8_t k = 0; k <= x; k++)
        s->v[k] = s->ram[s->i + k];
}

typedef void (*FxFn)(Chip8State *s, uint8_t x, uint8_t op);

typedef struct
{
    uint8_t sub;
    FxFn    fn;
} FxRow;

static const FxRow fx_tab[] = {
    {0x07, fx_delay_get}, {0x0A, fx_wait_key}, {0x15, fx_delay_set}, {0x1E, fx_i_add_vx},
    {0x29, fx_i_digit},   {0x33, fx_bcd},      {0x55, fx_store},     {0x65, fx_load},
};

static void op_zero(Chip8State *s, uint16_t op)
{
    if ((op & 0x00FF) == 0x00E0)
    {
        chip8_clear(s);
    }
    else if ((op & 0x00FF) == 0x00EE)
    {
        if (s->sp > 0)
        {
            s->sp--;
            s->pc = s->stack[s->sp];
        }
    }
}

static void op_jump(Chip8State *s, uint16_t op)
{
    s->pc = op & 0x0FFF;
}

static void op_call(Chip8State *s, uint16_t op)
{
    if (s->sp < CH8_STACK)
    {
        s->stack[s->sp++] = s->pc;
    }

    s->pc = op & 0x0FFF;
}

static void op_skip_eq(Chip8State *s, uint16_t op)
{
    uint8_t x = (op >> 8) & 0xF;

    if (s->v[x] == (op & 0xFF))
        s->pc += 2;
}

static void op_skip_ne(Chip8State *s, uint16_t op)
{
    uint8_t x = (op >> 8) & 0xF;

    if (s->v[x] != (op & 0xFF))
        s->pc += 2;
}

static void op_skip_eq_r(Chip8State *s, uint16_t op)
{
    uint8_t x = (op >> 8) & 0xF;
    uint8_t y = (op >> 4) & 0xF;

    if (s->v[x] == s->v[y])
        s->pc += 2;
}

static void op_ld_k(Chip8State *s, uint16_t op)
{
    s->v[(op >> 8) & 0xF] = op & 0xFF;
}

static void op_add_k(Chip8State *s, uint16_t op)
{
    s->v[(op >> 8) & 0xF] += op & 0xFF;
}

static void op_alu(Chip8State *s, uint16_t op)
{
    uint8_t x = (op >> 8) & 0xF;
    uint8_t y = (op >> 4) & 0xF;

    if (alu_tab[op & 0xF])
        alu_tab[op & 0xF](s, x, y);
}

static void op_skip_ne_r(Chip8State *s, uint16_t op)
{
    uint8_t x = (op >> 8) & 0xF;
    uint8_t y = (op >> 4) & 0xF;

    if (s->v[x] != s->v[y])
        s->pc += 2;
}

static void op_ld_i(Chip8State *s, uint16_t op)
{
    s->i = op & 0x0FFF;
}

static void op_jp_v0(Chip8State *s, uint16_t op)
{
    s->pc = (op & 0x0FFF) + s->v[0];
}

static void op_rnd(Chip8State *s, uint16_t op)
{
    uint8_t x = (op >> 8) & 0xF;
    uint8_t kk = op & 0xFF;

    s->v[x] = rand() & kk;
}

static void op_draw(Chip8State *s, uint16_t op)
{
    uint8_t x = (op >> 8) & 0xF;
    uint8_t y = (op >> 4) & 0xF;

    chip8_draw(s, s->v[x], s->v[y], op & 0xF);
}

static void op_key(Chip8State *s, uint16_t op)
{
    uint8_t x = (op >> 8) & 0xF;

    if ((op & 0x00FF) == 0x009E)
    {
        if (s->keys[s->v[x]])
            s->pc += 2;
    }
    else if ((op & 0x00FF) == 0x00A1)
    {
        if (!s->keys[s->v[x]])
            s->pc += 2;
    }
}

static void op_fx(Chip8State *s, uint16_t op)
{
    uint8_t x = (op >> 8) & 0xF;

    for (uint8_t i = 0; i < sizeof(fx_tab) / sizeof(fx_tab[0]); i++)
    {
        if (fx_tab[i].sub == (op & 0xFF))
        {
            fx_tab[i].fn(s, x, op);

            break;
        }
    }
}

typedef void (*OpFn)(Chip8State *s, uint16_t op);

static const OpFn op_tab[16] = {
    [0x0] = op_zero,    [0x1] = op_jump,      [0x2] = op_call, [0x3] = op_skip_eq,
    [0x4] = op_skip_ne, [0x5] = op_skip_eq_r, [0x6] = op_ld_k, [0x7] = op_add_k,
    [0x8] = op_alu,     [0x9] = op_skip_ne_r, [0xA] = op_ld_i, [0xB] = op_jp_v0,
    [0xC] = op_rnd,     [0xD] = op_draw,      [0xE] = op_key,  [0xF] = op_fx,
};

void chip8_reset(Chip8State *s)
{
    static const uint8_t font[80] = {
        0xF0, 0x90, 0x90, 0x90, 0xF0, /* 0 */
        0x20, 0x60, 0x20, 0x20, 0x70, /* 1 */
        0xF0, 0x10, 0xF0, 0x80, 0xF0, /* 2 */
        0xF0, 0x10, 0xF0, 0x10, 0xF0, /* 3 */
        0x90, 0x90, 0xF0, 0x10, 0x10, /* 4 */
        0xF0, 0x80, 0xF0, 0x10, 0xF0, /* 5 */
        0xF0, 0x80, 0xF0, 0x90, 0xF0, /* 6 */
        0xF0, 0x10, 0x20, 0x40, 0x40, /* 7 */
        0xF0, 0x90, 0xF0, 0x90, 0xF0, /* 8 */
        0xF0, 0x90, 0xF0, 0x10, 0xF0, /* 9 */
        0xF0, 0x90, 0xF0, 0x90, 0x90, /* A */
        0xE0, 0x90, 0xE0, 0x90, 0xE0, /* B */
        0xF0, 0x80, 0x80, 0x80, 0xF0, /* C */
        0xE0, 0x90, 0x90, 0x90, 0xE0, /* D */
        0xF0, 0x80, 0xF0, 0x80, 0xF0, /* E */
        0xF0, 0x80, 0xF0, 0x80, 0x80, /* F */
    };

    memset(s, 0, sizeof *s);
    memcpy(s->ram + CH8_FONT, font, sizeof font);

    s->pc = CH8_PROG;
    s->step = CH8_DEFAULT_STEP;
}

void chip8_frame(Chip8State *s)
{
    if (s->waiting)
    {
        for (uint8_t k = 0; k < CH8_NKEYS; k++)
        {
            if (s->keys[k])
            {
                s->v[s->wait_reg] = k;
                s->keys[k] = 0;
                s->waiting = 0;

                break;
            }
        }
    }
    else
    {
        for (int i = 0; i < s->step; i++)
        {
            uint16_t op = (s->ram[s->pc] << 8) | s->ram[s->pc + 1];
            s->pc += 2;
            op_tab[(op >> 12) & 0xF](s, op);
        }
    }

    if (s->delay)
        s->delay--;
}