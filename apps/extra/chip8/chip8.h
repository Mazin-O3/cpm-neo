#ifndef CHIP8_H
#define CHIP8_H

#include <ansi.h>
#include <stdint.h>

#define CH8_W 64
#define CH8_H 32
#define CH8_RAM 0x1000
#define CH8_PROG 0x200
#define CH8_FONT 0x000
#define CH8_MAX (CH8_RAM - CH8_PROG)
#define CH8_STACK 16
#define CH8_NKEYS 16
#define CH8_DEFAULT_STEP 10 /* instructions per 60 Hz frame tick = 600 Hz */
#define CH8_MIN_STEP 1
#define CH8_MAX_STEP 64
#define CH8_FRAME_MS 16

typedef struct
{
    uint8_t ram[CH8_RAM];
    uint8_t v[CH8_NKEYS];

    uint16_t i;
    uint16_t pc;
    uint16_t sp;
    uint16_t stack[CH8_STACK];

    uint8_t delay;

    uint8_t gfx[CH8_W * CH8_H / 8];
    uint8_t dirty;

    uint8_t keys[CH8_NKEYS];

    uint8_t waiting;
    uint8_t wait_reg;

    uint8_t step;
} Chip8State;

/* VM */
void chip8_reset(Chip8State *s);
void chip8_frame(Chip8State *s);

/* Video */
void chip8_clear(Chip8State *s);
void chip8_draw(Chip8State *s, uint8_t x, uint8_t y, uint8_t n);
void chip8_render(Chip8State *s);

/* Front end */
int chip8_load(Chip8State *s, const char *path);
int chip8_mapkey(int c);

#endif /* CHIP8_H */