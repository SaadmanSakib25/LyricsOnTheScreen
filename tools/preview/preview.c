/*
 * PC preview: renders frames of the lyric video at chosen song times using the
 * exact same renderer as the firmware (src/lyric_fx.c, fx_draw.c, scenes.c).
 *
 *   preview <out.bin> <ms> [<ms> ...]
 *
 * Writes one raw 128x160 RGB565 frame per time to out.bin. Turn them into a
 * PNG contact sheet with make_sheet.py. See tools/preview/README.md.
 */
#include <stdio.h>
#include <stdlib.h>

#include "lyric_fx.h"
#include "lyrics_data.h"

static uint16_t fb[FX_WIDTH * FX_HEIGHT];

static size_t find_line(uint32_t ms)
{
    size_t active = 0;
    for (size_t i = 0; i < LYRICS_COUNT; i++) {
        if (ms >= lyrics[i].start_ms) active = i;
        else break;
    }
    return active;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <out.bin> <ms> [<ms> ...]\n", argv[0]);
        return 1;
    }
    FILE *f = fopen(argv[1], "wb");
    if (!f) {
        perror(argv[1]);
        return 1;
    }
    fx_init();
    for (int i = 2; i < argc; i++) {
        uint32_t ms = (uint32_t)strtoul(argv[i], NULL, 10);
        /* render a few frames leading up to the target so particles settle in */
        for (int back = 8; back >= 0; back--) {
            uint32_t m = ms > back * 40u ? ms - back * 40u : 0;
            fx_render(fb, m, find_line(m), m / 40, true);
        }
        fwrite(fb, sizeof(uint16_t), FX_WIDTH * FX_HEIGHT, f);
    }
    fclose(f);
    return 0;
}
