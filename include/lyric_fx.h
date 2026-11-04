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

/* Call once at startup (precomputes the smoothed font). */
void fx_init(void);

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
