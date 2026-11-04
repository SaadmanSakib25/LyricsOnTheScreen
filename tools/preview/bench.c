/*
 * PC benchmark: average render time per frame for a representative moment of
 * each scene. PC numbers are much faster than the ESP32-S3; use them to compare
 * scenes and to check that an optimization helped, not as absolute timings.
 */
#include <stdio.h>
#include <time.h>

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

int main(void)
{
    static const uint32_t times[] = {4000, 9800, 12600, 14300, 15500, 19300, 21400, 25300, 27800, 30600,
                                     35000, 40000, 42000, 44200, 47000, 48300, 56000, 91800, 95500, 152000};
    fx_init();
    for (unsigned k = 0; k < sizeof times / sizeof times[0]; k++) {
        const int n = 400;
        clock_t c0 = clock();
        for (int i = 0; i < n; i++) {
            uint32_t m = times[k] + i * 2;
            fx_render(fb, m, find_line(m), (uint32_t)i, true);
        }
        double us = (double)(clock() - c0) / CLOCKS_PER_SEC * 1e6 / n;
        printf("%6u ms  scene %2d  %7.1f us/frame\n", (unsigned)times[k], lyrics[find_line(times[k])].scene, us);
    }
    return 0;
}
