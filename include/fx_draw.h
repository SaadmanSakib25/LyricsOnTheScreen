#pragma once

/*
 * Software 2D drawing for the lyric renderer: anti-aliased shapes,
 * smoothed bitmap text, colored glow (bloom), camera, vignette.
 * Everything draws into an RGB565 framebuffer set with fx_begin().
 */

#include <stdbool.h>
#include <stdint.h>

#include "lyric_fx.h"

#define FXW    FX_WIDTH
#define FXH    FX_HEIGHT
#define FX_PI  3.14159265f
#define FX_TAU 6.28318531f

/* ---------------- small math / color helpers ---------------- */
static inline float fx_clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
static inline float fx_lerp(float a, float b, float k) { return a + (b - a) * k; }
static inline float fx_ease_out(float k) { float u = 1.0f - k; return 1.0f - u * u * u; }
static inline float fx_ease_in(float k) { return k * k * k; }
static inline float fx_ease_inout(float k) { return k < 0.5f ? 4.0f * k * k * k : 1.0f - (-2.0f * k + 2.0f) * (-2.0f * k + 2.0f) * (-2.0f * k + 2.0f) / 2.0f; }
static inline float fx_ease_back(float k) { const float c = 1.9f; float u = k - 1.0f; return 1.0f + (c + 1.0f) * u * u * u + c * u * u; }
/* 0..1 progress of t through [start, start+len] */
static inline float fx_prog(float t, float start, float len) { return fx_clamp01((t - start) / len); }

static inline uint32_t fx_hash(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
static inline float fx_hashf(uint32_t x) { return (float)(fx_hash(x) >> 8) * (1.0f / 16777216.0f); }

static inline uint16_t fx_rgb(int r, int g, int b)
{
    r = r < 0 ? 0 : (r > 255 ? 255 : r);
    g = g < 0 ? 0 : (g > 255 ? 255 : g);
    b = b < 0 ? 0 : (b > 255 ? 255 : b);
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t fx_hsv(float h, float s, float v);
uint16_t fx_blend(uint16_t dst, uint16_t src, int a);   /* alpha blend, a 0..255   */
uint16_t fx_add(uint16_t dst, uint16_t src, int a);     /* additive light, a 0..255 */
uint16_t fx_mix(uint16_t a, uint16_t b, float k);       /* color lerp               */

/* ---------------- frame ----------------
 * A frame is drawn in two stages that may run on different cores:
 *   draw   : fx_begin(fb, slot) + shapes/text  (uses the drawing state below)
 *   finish : fx_finish(slot, fb, ...) + fx_post_*  (touches only that slot + fb)
 * Each in-flight frame uses its own slot (glow + text-coverage buffers). */
void fx_draw_init(void);
void fx_begin(uint16_t *fb, int slot);       /* bind framebuffer + slot, clear its masks */
void fx_fill(uint16_t color);
/* blur + add the slot's glow (gain 256 = 1x), optional vignette */
void fx_finish(int slot, uint16_t *fb, int glow_gain256, bool vignette);
/* finish-stage overlays (explicit framebuffer, safe on another core) */
void fx_post_flash(uint16_t *fb, uint16_t color, int alpha);
void fx_post_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t color, int alpha);

/* ---------------- camera / state (affects shapes, not lyric glyphs) ---------------- */
void fx_camera(float zoom, float world_cx, float world_cy, float screen_cx, float screen_cy);
void fx_camera_reset(void);
void fx_set_shake(float dx, float dy);       /* also applies to glyphs */
void fx_set_alpha(float a);                  /* global opacity for shapes, 0..1 */

/* ---------------- screen-space fills ---------------- */
void fx_vgradient(int y0, int y1, uint16_t top, uint16_t bottom, int alpha);
void fx_shade_band(float y0, float y1, float feather, int alpha);  /* darken a horizontal band */
void fx_plot_add(int x, int y, uint16_t c, int a);

/* ---------------- anti-aliased shapes (world coords, camera applied) ----------------
 * a = opacity 0..255, glow = 0..255 bloom strength in the shape's own color */
void fx_disc(float x, float y, float r, uint16_t c, int a, int glow);
void fx_ring(float x, float y, float r, float w, uint16_t c, int a, int glow);
void fx_arc(float x, float y, float r, float w, float a0, float a1, uint16_t c, int a, int glow);
void fx_line(float x0, float y0, float x1, float y1, float w, uint16_t c, int a, int glow);
void fx_poly(const float *xy, int n, uint16_t c, int a, int glow);    /* xy = x0,y0,x1,y1,... */
void fx_rect(float x, float y, float w, float h, uint16_t c, int a, int glow);
void fx_radial(float x, float y, float r, uint16_t c, int a);         /* soft additive light */

/* ---------------- text ---------------- */
float fx_glyph_adv(uint8_t c);
uint8_t fx_glyph_width(uint8_t c);
float fx_text_units(const char *str);
/* screen-space glyph: top-left of inked area, s = px per font unit, marks text coverage */
void fx_glyph(uint8_t ch, float x, float y, float s, uint16_t c, int a, uint16_t glow_c, int glow, bool snap);
/* world-space centered text (camera + global alpha applied) */
void fx_text(const char *str, float cx, float cy, float s, uint16_t c, int a, int glow);
