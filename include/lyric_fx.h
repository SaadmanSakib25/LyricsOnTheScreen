#pragma once

/*
 * Lyric-video renderer: draws a full frame (black background, kinetic
 * word-by-word typography, glow, beat pulses, particles) into an RGB565
 * framebuffer. Pure software, no hardware dependencies.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FX_WIDTH  128
#define FX_HEIGHT 160

/* How long the closing title card stays up after the song ends, before looping. */
#define FX_END_HOLD_MS 5000

/* Frames that can be in flight at once (one drawing, one being finished/sent). */
#define FX_SLOTS 2

/* Call once at startup (precomputes the smoothed font). */
void fx_init(void);

/* What the finish stage needs to know about a drawn frame. */
typedef struct {
    int glow_gain;      /* bloom strength, 256 = 1x            */
    bool vignette;
    int flash;          /* white flash opacity 0..255          */
    bool paused;
    uint32_t tick;
    uint32_t song_ms;
} fx_post_t;

/*
 * Two-stage rendering, so drawing and finishing can run on different cores:
 *   fx_draw_frame()   draws the scene + lyrics into fb (slot 0..1 = in-flight frame)
 *   fx_finish_frame() adds bloom, vignette, flashes and overlays to that same fb/slot
 * Calls of one stage must not overlap each other; the two stages may overlap
 * as long as they work on different slots.
 */
void fx_draw_frame(int slot, uint16_t *fb, uint32_t song_ms, size_t line_index,
                   uint32_t frame_tick, bool is_playing, fx_post_t *post);
void fx_finish_frame(int slot, uint16_t *fb, const fx_post_t *post);

/*
 * Render one frame.
 *   fb          FX_WIDTH*FX_HEIGHT pixels, native-endian RGB565 (output)
 *   song_ms     current song position
 *   line_index  index into lyrics[] of the active line
 *   frame_tick  free-running frame counter (ambient animation, runs while paused)
 *   is_playing  false shows a small pause indicator
 */
void fx_render(uint16_t *fb, uint32_t song_ms, size_t line_index,
               uint32_t frame_tick, bool is_playing);
