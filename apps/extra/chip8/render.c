#include "chip8.h"

/* One terminal row: CH8_W cells x up to 3 UTF-8 bytes each + newline. */
#define ROW_BUF (CH8_W * 3 + 2)

static const struct
{
    char    s[4];
    uint8_t n;
} glyphs[4] = {
    {" ", 1},            /* 0: off / off        */
    {"\xE2\x96\x84", 3}, /* 1: off / on  (▄ U+2584) */
    {"\xE2\x96\x80", 3}, /* 2: on  / off (▀ U+2580) */
    {"\xE2\x96\x88", 3}, /* 3: on  / on  (█ U+2588) */
};

void chip8_clear(Chip8State *s)
{
    memset(s->gfx, 0, sizeof(s->gfx));
    s->dirty = 1;
}

void chip8_draw(Chip8State *s, uint8_t x, uint8_t y, uint8_t n)
{
    uint8_t vf = 0;

    uint8_t start_x = x % CH8_W;
    uint8_t start_y = y % CH8_H;

    uint8_t b1 = start_x >> 3;
    uint8_t b2 = (b1 + 1) % (CH8_W / 8); 
    uint8_t shift = start_x & 7;

    for (uint8_t r = 0; r < n; r++)
    {
        uint8_t sprite = s->ram[(s->i + r) & 0xFFF];
        
        if (!sprite)
            continue;

        uint8_t py = (start_y + r) % CH8_H; 
        uint8_t p1 = sprite >> shift;
        uint8_t p2 = (uint8_t)(shift == 0 ? 0 : (sprite << (8 - shift)));

        uint8_t *row_ptr = &s->gfx[py * (CH8_W / 8)];

        if (row_ptr[b1] & p1)
            vf = 1;

        row_ptr[b1] ^= p1;

        if (p2)
        {
            if (row_ptr[b2] & p2)
                vf = 1;

            row_ptr[b2] ^= p2;
        }
    }

    s->v[0xF] = vf;
    s->dirty = 1;
}

void chip8_render(Chip8State *s)
{
    if (!s->dirty)
        return;

    sys_write(FD_STDOUT, CSI_HOME, 3);

    char wbuf[ROW_BUF];
    int  len = 0;

    for (int row = 0; row < CH8_H / 2; row++)
    {
        uint8_t *top_row = &s->gfx[(row * 2) * (CH8_W / 8)];
        uint8_t *bot_row = &s->gfx[(row * 2 + 1) * (CH8_W / 8)];

        len = 0;

        for (int b = 0; b < (CH8_W / 8); b++)
        {
            uint8_t top = top_row[b];
            uint8_t bot = bot_row[b];

            for (int p = 0; p < 8; p++)
            {
                int i = ((top >> 6) & 2) | ((bot >> 7) & 1);

                top <<= 1;
                bot <<= 1;

                memcpy(wbuf + len, glyphs[i].s, glyphs[i].n);
                len += glyphs[i].n;
            }
        }

        wbuf[len++] = '\n';
        sys_write(FD_STDOUT, wbuf, len);
    }

    s->dirty = 0;
}