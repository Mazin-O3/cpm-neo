#include "chip8.h"

static Chip8State s;

int chip8_load(Chip8State *s, const char *path)
{
    int fd = open(path, "r");

    if (fd < 0)
        return fd;

    int off = 0;

    for (;;)
    {
        int n = read(fd, s->ram + CH8_PROG + off, CH8_MAX - off);

        if (n < 0)
        {
            close(fd);

            return n;
        }

        if (n == 0)
            break;

        off += n;

        if (off >= CH8_MAX)
        {
            close(fd);

            return E2BIG;
        }
    }

    close(fd);

    return off == 0 ? ENOEXEC : EOK;
}

int chip8_mapkey(int c)
{
    const char *keys = "1234QWERASDFZXCV";

    for (int i = 0; i < CH8_NKEYS; i++)
    {
        if (keys[i] == toupper(c))
            return i;
    }

    return -1;
}

int chip8_getinput()
{
    while (peekchar())
    {
        int c = getchar();

        if (c == CH_ESC)
        {
            printf(CSI_CLS CSI_HOME CSI_SHOW);
            return 1;
        }

        int key = chip8_mapkey(c);

        if (key >= 0)
            s.keys[key] = 1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3)
    {
        printf("Use: CHIP8 <GAME.CH8> [SPEED]\n\n"
               "Keys: 1 2 3 4    CHIP-8: 1 2 3 C\n"
               "      Q W E R            4 5 6 D\n"
               "      A S D F            7 8 9 E\n"
               "      Z X C V            A 0 B F\n\n"
               "ESC exits\n");

        return 1;
    }

    chip8_reset(&s);

    if (argc == 3)
    {
        int speed = atoi(argv[2]);

        if (speed >= CH8_MIN_STEP && speed <= CH8_MAX_STEP)
            s.step = (uint8_t)speed;
    }

    int rc = chip8_load(&s, argv[1]);

    if (rc != EOK)
    {
        printf("?%s\n", strerror(rc));

        return 1;
    }

    printf(CSI_CLS CSI_HIDE);

    s.dirty = 1;

    int should_quit = 0;
    
    uint32_t last_time = sys_millis();
    
    while (!should_quit)
    {
        uint32_t cur_time = sys_millis();
        
        if (cur_time - last_time >= CH8_FRAME_MS)
        {
            last_time += CH8_FRAME_MS;

            chip8_tick(&s);
            chip8_render(&s);

            memset(s.keys, 0, CH8_NKEYS);
        }

        should_quit = chip8_getinput();
    }

    return 0;
}