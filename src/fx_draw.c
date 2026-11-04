/*
 * Software 2D drawing: anti-aliased shapes, smoothed bitmap text,
 * colored bloom, simple camera and vignette. See fx_draw.h.
 */
#include "fx_draw.h"

#include <math.h>
#include <string.h>

#include "font5x7.h"

#define W    FXW
#define H    FXH
#define NPIX (W * H)
/* Glow is accumulated, blurred and upscaled at half resolution (4x cheaper). */
#define GW (W / 2)
#define GH (H / 2)
#define GNPIX (GW * GH)
#define GLOW_RADIUS 1   /* half-res px, applied twice -> ~4 px halo at full res */

/* ------------------------------------------------------------------ */
/* Color                                                              */
/* ------------------------------------------------------------------ */
uint16_t fx_blend(uint16_t d, uint16_t s, int a)
{
    if (a <= 0) return d;
    if (a >= 255) return s;
    a += a >> 7;
    int dr = d >> 11, dg = (d >> 5) & 63, db = d & 31;
    int sr = s >> 11, sg = (s >> 5) & 63, sb = s & 31;
    dr += ((sr - dr) * a) >> 8;
    dg += ((sg - dg) * a) >> 8;
    db += ((sb - db) * a) >> 8;
    return (uint16_t)((dr << 11) | (dg << 5) | db);
}

uint16_t fx_add(uint16_t d, uint16_t s, int a)
{
    if (a <= 0) return d;
    if (a > 255) a = 255;
    a += a >> 7;
    int r = (d >> 11) + (((s >> 11) * a) >> 8);
    int g = ((d >> 5) & 63) + ((((s >> 5) & 63) * a) >> 8);
    int b = (d & 31) + (((s & 31) * a) >> 8);
    if (r > 31) r = 31;
    if (g > 63) g = 63;
    if (b > 31) b = 31;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

uint16_t fx_mix(uint16_t a, uint16_t b, float k)
{
    return fx_blend(a, b, (int)(fx_clamp01(k) * 255.0f));
}

uint16_t fx_hsv(float h, float s, float v)
{
    h = fmodf(h, 360.0f);
    if (h < 0) h += 360.0f;
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r, g, b;
    if (h < 60)       { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else              { r = c; g = 0; b = x; }
    return fx_rgb((int)((r + m) * 255), (int)((g + m) * 255), (int)((b + m) * 255));
}

/* ------------------------------------------------------------------ */
/* State                                                              */
/* ------------------------------------------------------------------ */
static uint16_t *s_fb;
typedef struct {
    uint8_t gr[GNPIX], gg[GNPIX], gb[GNPIX]; /* colored glow, half-res      */
    uint8_t tmp[GNPIX];                      /* blur scratch                */
    uint8_t tcov[NPIX];                      /* lyric text coverage         */
    int gy0, gy1;                            /* dirty glow rows (full res)  */
} fx_slot_t;

static fx_slot_t s_slots[FX_SLOTS];
/* drawing-stage view of the current slot */
static fx_slot_t *s_cur = &s_slots[0];
static uint8_t *s_gr = s_slots[0].gr, *s_gg = s_slots[0].gg, *s_gb = s_slots[0].gb;
static uint8_t *s_tcov = s_slots[0].tcov;

static float s_zoom = 1.0f, s_wcx = W / 2.0f, s_wcy = H / 2.0f, s_scx = W / 2.0f, s_scy = H / 2.0f;
static float s_shx, s_shy;
static float s_galpha = 1.0f;

static uint8_t s_vig_x[W], s_vig_y[H];

static inline float tx(float x) { return (x - s_wcx) * s_zoom + s_scx + s_shx; }
static inline float ty(float y) { return (y - s_wcy) * s_zoom + s_scy + s_shy; }

void fx_camera(float zoom, float wcx, float wcy, float scx, float scy)
{
    s_zoom = zoom;
    s_wcx = wcx;
    s_wcy = wcy;
    s_scx = scx;
    s_scy = scy;
}

void fx_camera_reset(void) { fx_camera(1.0f, W / 2.0f, H / 2.0f, W / 2.0f, H / 2.0f); }
void fx_set_shake(float dx, float dy) { s_shx = dx; s_shy = dy; }
void fx_set_alpha(float a) { s_galpha = fx_clamp01(a); }

/* per-shape glow color prep */
typedef struct {
    int r, g, b, amt;
} glowc_t;

static inline glowc_t glow_prep(uint16_t c, int glow)
{
    glowc_t g;
    g.r = ((c >> 11) & 31) * 255 / 31;
    g.g = ((c >> 5) & 63) * 255 / 63;
    g.b = (c & 31) * 255 / 31;
    g.amt = glow < 0 ? 0 : (glow > 255 ? 255 : glow);
    return g;
}

static inline void glow_rows(int y0, int y1)
{
    if (y0 < s_cur->gy0) s_cur->gy0 = y0;
    if (y1 > s_cur->gy1) s_cur->gy1 = y1;
}

/* write one covered pixel: a = final opacity 0..255 */
static inline void put(int px, int py, uint16_t c, int a, const glowc_t *g)
{
    int idx = py * W + px;
    s_fb[idx] = fx_blend(s_fb[idx], c, a);
    int gi = (py >> 1) * GW + (px >> 1);
    if (a > 8 && (s_gr[gi] | s_gg[gi] | s_gb[gi])) {
        /* opaque things in front block the glow of things behind them */
        int keep = 256 - ((a * 3) >> 2);
        s_gr[gi] = (uint8_t)((s_gr[gi] * keep) >> 8);
        s_gg[gi] = (uint8_t)((s_gg[gi] * keep) >> 8);
        s_gb[gi] = (uint8_t)((s_gb[gi] * keep) >> 8);
    }
    if (g->amt) {
        int k = a * g->amt; /* 0..65025 */
        int v;
        v = (g->r * k) >> 16;
        if (v > s_gr[gi]) s_gr[gi] = (uint8_t)v;
        v = (g->g * k) >> 16;
        if (v > s_gg[gi]) s_gg[gi] = (uint8_t)v;
        v = (g->b * k) >> 16;
        if (v > s_gb[gi]) s_gb[gi] = (uint8_t)v;
    }
}

/* ------------------------------------------------------------------ */
/* Font                                                               */
/* ------------------------------------------------------------------ */
static uint16_t s_glyph[256][16];
static uint8_t s_left[256], s_width[256];

static int src_px(int c, int x, int y)
{
    if (x < 0 || x > 4 || y < 0 || y > 7) return 0;
    return (font5x7[c][x] >> y) & 1;
}

static void font_prepare(void)
{
    for (int c = 0; c < 256; c++) {
        int first = -1, last = -1;
        for (int x = 0; x < 5; x++) {
            if (font5x7[c][x]) {
                if (first < 0) first = x;
                last = x;
            }
        }
        s_left[c] = (uint8_t)(first < 0 ? 0 : first);
        s_width[c] = (uint8_t)(first < 0 ? 0 : last - first + 1);

        /* EPX / Scale2x smoothing */
        memset(s_glyph[c], 0, sizeof(s_glyph[c]));
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 5; x++) {
                int P = src_px(c, x, y);
                int A = src_px(c, x, y - 1), B = src_px(c, x + 1, y);
                int C = src_px(c, x - 1, y), D = src_px(c, x, y + 1);
                int e0 = P, e1 = P, e2 = P, e3 = P;
                if (C == A && C != D && A != B) e0 = A;
                if (A == B && A != C && B != D) e1 = B;
                if (D == C && D != B && C != A) e2 = C;
                if (B == D && B != A && D != C) e3 = D;
                s_glyph[c][2 * y]     |= (uint16_t)((e0 << (2 * x)) | (e1 << (2 * x + 1)));
                s_glyph[c][2 * y + 1] |= (uint16_t)((e2 << (2 * x)) | (e3 << (2 * x + 1)));
            }
        }
    }
}

float fx_glyph_adv(uint8_t c)
{
    if (c == ' ') return 3.0f;
    if (!s_width[c]) return 3.0f;
    return (float)s_width[c] + 1.0f;
}

uint8_t fx_glyph_width(uint8_t c) { return s_width[c]; }

float fx_text_units(const char *str)
{
    float u = 0;
    for (; *str; str++) u += fx_glyph_adv((uint8_t)*str);
    return u > 0 ? u - 1.0f : 0;
}

static void glyph_raw(uint8_t c, float x, float y, float s, uint16_t color, int alpha,
                      uint16_t glow_c, int glow_amt, bool snap, bool mark_text)
{
    if (alpha <= 0 || s_width[c] == 0 || s < 0.25f) return;
    if (alpha > 255) alpha = 255;
    x += s_shx;
    y += s_shy;
    if (snap) {
        x = roundf(x);
        y = roundf(y);
    }

    const uint16_t *rows = s_glyph[c];
    const int col0 = s_left[c] * 2;
    const int ncols = s_width[c] * 2;
    const float cell = s * 0.5f;
    const float inv = 1.0f / cell;
    static const float SUB[3] = {1.0f / 6.0f, 0.5f, 5.0f / 6.0f};

    int x0 = (int)floorf(x), y0 = (int)floorf(y);
    int x1 = (int)ceilf(x + ncols * cell), y1 = (int)ceilf(y + 16 * cell);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    if (x0 >= x1 || y0 >= y1) return;

    glowc_t g = glow_prep(glow_c, glow_amt);
    if (g.amt) glow_rows(y0, y1);

    static uint16_t colmask[W][3];
    for (int px = x0; px < x1; px++) {
        for (int k = 0; k < 3; k++) {
            int ui = (int)floorf((px + SUB[k] - x) * inv);
            colmask[px - x0][k] = (ui >= 0 && ui < ncols) ? (uint16_t)(1u << (col0 + ui)) : 0;
        }
    }

    for (int py = y0; py < y1; py++) {
        uint16_t r[3];
        for (int k = 0; k < 3; k++) {
            int vi = (int)floorf((py + SUB[k] - y) * inv);
            r[k] = (vi >= 0 && vi < 16) ? rows[vi] : 0;
        }
        if (!(r[0] | r[1] | r[2])) continue;

        for (int px = x0; px < x1; px++) {
            const uint16_t *cm = colmask[px - x0];
            int cnt = 0;
            for (int kx = 0; kx < 3; kx++) {
                cnt += ((r[0] & cm[kx]) != 0) + ((r[1] & cm[kx]) != 0) + ((r[2] & cm[kx]) != 0);
            }
            if (!cnt) continue;
            int a = cnt * alpha / 9;
            int idx = py * W + px;
            put(px, py, color, a, &g);
            if (mark_text && a > s_tcov[idx]) s_tcov[idx] = (uint8_t)a;
        }
    }
}

void fx_glyph(uint8_t ch, float x, float y, float s, uint16_t c, int a, uint16_t glow_c, int glow, bool snap)
{
    glyph_raw(ch, x, y, s, c, a, glow_c, glow, snap, true);
}

void fx_text(const char *str, float cx, float cy, float s, uint16_t c, int a, int glow)
{
    a = (int)(a * s_galpha);
    if (a <= 0) return;
    float sx = tx(cx) - s_shx, sy = ty(cy) - s_shy; /* glyph_raw re-adds shake */
    float ss = s * s_zoom;
    float x = sx - fx_text_units(str) * ss / 2.0f;
    float y = sy - 4.0f * ss;
    for (; *str; str++) {
        glyph_raw((uint8_t)*str, x, y, ss, c, a, c, glow, false, false);
        x += fx_glyph_adv((uint8_t)*str) * ss;
    }
}

/* ------------------------------------------------------------------ */
/* Frame                                                              */
/* ------------------------------------------------------------------ */
void fx_draw_init(void)
{
    font_prepare();
    /* vignette falloff tables (darken toward the edges) */
    for (int x = 0; x < W; x++) {
        float d = (x + 0.5f - W / 2.0f) / (W / 2.0f);
        s_vig_x[x] = (uint8_t)(d * d * 70.0f);
    }
    for (int y = 0; y < H; y++) {
        float d = (y + 0.5f - H / 2.0f) / (H / 2.0f);
        s_vig_y[y] = (uint8_t)(d * d * 60.0f);
    }
}

void fx_begin(uint16_t *fb, int slot)
{
    s_fb = fb;
    s_cur = &s_slots[slot % FX_SLOTS];
    s_gr = s_cur->gr;
    s_gg = s_cur->gg;
    s_gb = s_cur->gb;
    s_tcov = s_cur->tcov;
    memset(s_cur->gr, 0, sizeof(s_cur->gr));
    memset(s_cur->gg, 0, sizeof(s_cur->gg));
    memset(s_cur->gb, 0, sizeof(s_cur->gb));
    memset(s_cur->tcov, 0, sizeof(s_cur->tcov));
    s_cur->gy0 = H;
    s_cur->gy1 = 0;
    s_galpha = 1.0f;
    s_shx = s_shy = 0;
    fx_camera_reset();
}

void fx_fill(uint16_t color)
{
    for (int i = 0; i < NPIX; i++) s_fb[i] = color;
}

static void blur_h(const uint8_t *src, uint8_t *dst, int ya, int yb)
{
    const int r = GLOW_RADIUS;
    for (int y = ya; y < yb; y++) {
        const uint8_t *s = src + y * GW;
        uint8_t *d = dst + y * GW;
        int sum = 0;
        for (int x = 0; x <= r; x++) sum += s[x];
        for (int x = 0; x < GW; x++) {
            d[x] = (uint8_t)((sum * 85) >> 8); /* /3 */
            if (x + r + 1 < GW) sum += s[x + r + 1];
            if (x - r >= 0) sum -= s[x - r];
        }
    }
}

/* vertical blur restricted to rows [ya, yb), zero outside */
static void blur_v(const uint8_t *src, uint8_t *dst, int ya, int yb)
{
    const int r = GLOW_RADIUS;
    for (int x = 0; x < GW; x++) {
        int sum = 0;
        for (int y = ya; y <= ya + r && y < yb; y++) sum += src[y * GW + x];
        for (int y = ya; y < yb; y++) {
            dst[y * GW + x] = (uint8_t)((sum * 85) >> 8);
            if (y + r + 1 < yb) sum += src[(y + r + 1) * GW + x];
            if (y - r >= ya) sum -= src[(y - r) * GW + x];
        }
    }
}

static void blur_channel(uint8_t *ch, uint8_t *tmp, int ya, int yb)
{
    blur_h(ch, tmp, ya, yb);
    blur_v(tmp, ch, ya, yb);
    blur_h(ch, tmp, ya, yb);
    blur_v(tmp, ch, ya, yb);
}

/* bilinear-upscale the half-res glow (rows [hya, hyb)) and add it to the frame */
static void glow_composite(const fx_slot_t *sl, uint16_t *fb, int hya, int hyb, int gain)
{
    static uint16_t rr[GW], rg[GW], rb[GW]; /* vertically interpolated half-res row, x4 */
    int y0 = 2 * hya - 1, y1 = 2 * hyb + 1;
    if (y0 < 0) y0 = 0;
    if (y1 > H) y1 = H;
    for (int y = y0; y < y1; y++) {
        /* full-res row y = 3/4 of half-res row ha + 1/4 of its neighbour hb */
        int ha = y >> 1;
        int hb = (y & 1) ? ha + 1 : ha - 1;
        if (hb < 0) hb = 0;
        if (hb >= GH) hb = GH - 1;
        const uint8_t *ar = sl->gr + ha * GW, *ag = sl->gg + ha * GW, *ab = sl->gb + ha * GW;
        const uint8_t *br = sl->gr + hb * GW, *bg = sl->gg + hb * GW, *bb = sl->gb + hb * GW;
        bool any = false;
        for (int x = 0; x < GW; x++) {
            rr[x] = (uint16_t)(ar[x] * 3 + br[x]);
            rg[x] = (uint16_t)(ag[x] * 3 + bg[x]);
            rb[x] = (uint16_t)(ab[x] * 3 + bb[x]);
            any |= (rr[x] | rg[x] | rb[x]) != 0;
        }
        if (!any) continue;

        uint16_t *row = fb + y * W;
        const uint8_t *tc = sl->tcov + y * W;
        for (int x = 0; x < W; x++) {
            int xa = x >> 1;
            int xb = (x & 1) ? xa + 1 : xa - 1;
            if (xb < 0) xb = 0;
            if (xb >= GW) xb = GW - 1;
            int r = rr[xa] * 3 + rr[xb]; /* x16 */
            int g = rg[xa] * 3 + rg[xb];
            int b = rb[xa] * 3 + rb[xb];
            if (!(r | g | b)) continue;
            int k = (gain * (255 - tc[x])) >> 8; /* keep glow off lyric text */
            r = (r * k) >> 12;
            g = (g * k) >> 12;
            b = (b * k) >> 12;
            uint16_t d = row[x];
            int dr = (d >> 11) + (r >> 3);
            int dg = ((d >> 5) & 63) + (g >> 2);
            int db = (d & 31) + (b >> 3);
            if (dr > 31) dr = 31;
            if (dg > 63) dg = 63;
            if (db > 31) db = 31;
            row[x] = (uint16_t)((dr << 11) | (dg << 5) | db);
        }
    }
}

void fx_finish(int slot, uint16_t *fb, int gain, bool vignette)
{
    fx_slot_t *sl = &s_slots[slot % FX_SLOTS];
    if (sl->gy1 > sl->gy0) {
        int ya = (sl->gy0 >> 1) - 2 * GLOW_RADIUS - 1;
        int yb = ((sl->gy1 + 1) >> 1) + 2 * GLOW_RADIUS + 1;
        if (ya < 0) ya = 0;
        if (yb > GH) yb = GH;
        blur_channel(sl->gr, sl->tmp, ya, yb);
        blur_channel(sl->gg, sl->tmp, ya, yb);
        blur_channel(sl->gb, sl->tmp, ya, yb);
        glow_composite(sl, fb, ya, yb, gain);
    }

    if (vignette) {
        for (int y = 0; y < H; y++) {
            uint16_t *row = fb + y * W;
            const uint8_t *tc = sl->tcov + y * W;
            const int fy = 256 - s_vig_y[y];
            for (int x = 0; x < W; x++) {
                uint16_t d = row[x];
                if (!d || tc[x] > 128) continue;
                int f = fy - s_vig_x[x];
                int r = ((d >> 11) * f) >> 8;
                int g = (((d >> 5) & 63) * f) >> 8;
                int b = ((d & 31) * f) >> 8;
                row[x] = (uint16_t)((r << 11) | (g << 5) | b);
            }
        }
    }
}

void fx_post_flash(uint16_t *fb, uint16_t color, int alpha)
{
    if (alpha <= 0) return;
    for (int i = 0; i < NPIX; i++) fb[i] = fx_blend(fb[i], color, alpha);
}

void fx_post_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t color, int alpha)
{
    for (int j = y; j < y + h; j++) {
        if ((unsigned)j >= H) continue;
        for (int i = x; i < x + w; i++) {
            if ((unsigned)i >= W) continue;
            fb[j * W + i] = fx_blend(fb[j * W + i], color, alpha);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Screen-space fills                                                 */
/* ------------------------------------------------------------------ */
void fx_vgradient(int y0, int y1, uint16_t top, uint16_t bottom, int alpha)
{
    if (y0 < 0) y0 = 0;
    if (y1 > H) y1 = H;
    alpha = (int)(alpha * s_galpha);
    if (y1 <= y0 || alpha <= 0) return;
    for (int y = y0; y < y1; y++) {
        uint16_t c = fx_mix(top, bottom, (float)(y - y0) / (float)(y1 - y0));
        uint16_t *row = s_fb + y * W;
        if (alpha >= 255) {
            for (int x = 0; x < W; x++) row[x] = c;
        } else {
            for (int x = 0; x < W; x++) row[x] = fx_blend(row[x], c, alpha);
        }
    }
}

void fx_shade_band(float y0, float y1, float feather, int alpha)
{
    int ya = (int)floorf(y0 - feather), yb = (int)ceilf(y1 + feather);
    if (ya < 0) ya = 0;
    if (yb > H) yb = H;
    for (int y = ya; y < yb; y++) {
        float fy = y + 0.5f;
        float k = 1.0f;
        if (fy < y0) k = 1.0f - (y0 - fy) / feather;
        else if (fy > y1) k = 1.0f - (fy - y1) / feather;
        int a = (int)(alpha * fx_clamp01(k));
        if (a <= 0) continue;
        uint16_t *row = s_fb + y * W;
        for (int x = 0; x < W; x++) row[x] = fx_blend(row[x], 0, a);
    }
}

void fx_plot_add(int x, int y, uint16_t c, int a)
{
    if ((unsigned)x >= W || (unsigned)y >= H || a <= 0) return;
    s_fb[y * W + x] = fx_add(s_fb[y * W + x], c, a);
}

/* ------------------------------------------------------------------ */
/* Shapes                                                             */
/* ------------------------------------------------------------------ */
static void disc_screen(float x, float y, float r, uint16_t c, int a, int glow)
{
    if (r < 0.35f) r = 0.35f;
    const float ro2 = (r + 0.5f) * (r + 0.5f);
    const float ri2 = r > 0.5f ? (r - 0.5f) * (r - 0.5f) : -1.0f;
    int y0 = (int)floorf(y - r - 1), y1 = (int)ceilf(y + r + 1);
    if (y0 < 0) y0 = 0;
    if (y1 > H) y1 = H;
    if (y0 >= y1) return;
    glowc_t g = glow_prep(c, glow);
    if (g.amt) glow_rows(y0, y1);
    for (int py = y0; py < y1; py++) {
        float dy = py + 0.5f - y;
        float h2 = (r + 1.0f) * (r + 1.0f) - dy * dy;
        if (h2 <= 0) continue;
        float hw = sqrtf(h2);
        int xa = (int)floorf(x - hw), xb = (int)ceilf(x + hw);
        if (xa < 0) xa = 0;
        if (xb > W) xb = W;
        for (int px = xa; px < xb; px++) {
            float dx = px + 0.5f - x;
            float d2 = dx * dx + dy * dy;
            if (d2 >= ro2) continue;
            if (d2 <= ri2) {
                put(px, py, c, a, &g);
                continue;
            }
            float cov = r + 0.5f - sqrtf(d2);
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            put(px, py, c, (int)(a * cov), &g);
        }
    }
}

void fx_disc(float x, float y, float r, uint16_t c, int a, int glow)
{
    a = (int)(a * s_galpha);
    if (a <= 0) return;
    disc_screen(tx(x), ty(y), r * s_zoom, c, a, glow);
}

static void ring_impl(float x, float y, float r, float w, bool use_ang, float a0, float a1,
                      uint16_t c, int a, int glow)
{
    a = (int)(a * s_galpha);
    if (a <= 0 || r <= 0) return;
    x = tx(x);
    y = ty(y);
    r *= s_zoom;
    float hw = w * s_zoom * 0.5f;
    if (hw < 0.35f) hw = 0.35f;
    float ro = r + hw + 1.0f, ri = r - hw - 1.0f;
    /* squared radii: outside the band, and fully inside the band */
    const float bo2 = (r + hw + 0.5f) * (r + hw + 0.5f);
    const float bi2 = (r - hw - 0.5f) > 0 ? (r - hw - 0.5f) * (r - hw - 0.5f) : -1.0f;
    const float fo2 = (r + hw - 0.5f) > 0 ? (r + hw - 0.5f) * (r + hw - 0.5f) : -1.0f;
    const float fi2 = (r - hw + 0.5f) > 0 ? (r - hw + 0.5f) * (r - hw + 0.5f) : 0.0f;
    int y0 = (int)floorf(y - ro), y1 = (int)ceilf(y + ro);
    if (y0 < 0) y0 = 0;
    if (y1 > H) y1 = H;
    if (y0 >= y1) return;
    glowc_t g = glow_prep(c, glow);
    if (g.amt) glow_rows(y0, y1);

    for (int py = y0; py < y1; py++) {
        float dy = py + 0.5f - y;
        float o2 = ro * ro - dy * dy;
        if (o2 <= 0) continue;
        float xo = sqrtf(o2);
        float xi = (ri > 0 && fabsf(dy) < ri) ? sqrtf(ri * ri - dy * dy) : 0.0f;
        bool hole = xi > 0;
        for (int side = 0; side < (hole ? 2 : 1); side++) {
            int xa, xb;
            if (!hole)         { xa = (int)floorf(x - xo); xb = (int)ceilf(x + xo); }
            else if (side == 0) { xa = (int)floorf(x - xo); xb = (int)ceilf(x - xi); }
            else               { xa = (int)floorf(x + xi); xb = (int)ceilf(x + xo); }
            if (xa < 0) xa = 0;
            if (xb > W) xb = W;
            for (int px = xa; px < xb; px++) {
                float dx = px + 0.5f - x;
                float d2 = dx * dx + dy * dy;
                if (d2 >= bo2 || d2 <= bi2) continue;
                float cov = 1.0f;
                if (d2 < fi2 || d2 > fo2) {
                    cov = hw + 0.5f - fabsf(sqrtf(d2) - r);
                    if (cov <= 0) continue;
                    if (cov > 1) cov = 1;
                }
                if (use_ang) {
                    float ang = atan2f(dy, dx);
                    while (ang < a0) ang += FX_TAU;
                    while (ang >= a0 + FX_TAU) ang -= FX_TAU;
                    if (ang > a1) continue;
                }
                put(px, py, c, (int)(a * cov), &g);
            }
        }
    }
}

void fx_ring(float x, float y, float r, float w, uint16_t c, int a, int glow)
{
    ring_impl(x, y, r, w, false, 0, 0, c, a, glow);
}

void fx_arc(float x, float y, float r, float w, float a0, float a1, uint16_t c, int a, int glow)
{
    if (a1 <= a0) return;
    ring_impl(x, y, r, w, true, a0, a1, c, a, glow);
}

static inline float seg_dist2(float px, float py, float x0, float y0, float dx, float dy, float inv_l2)
{
    float t = ((px - x0) * dx + (py - y0) * dy) * inv_l2;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    float ex = px - (x0 + t * dx), ey = py - (y0 + t * dy);
    return ex * ex + ey * ey;
}

/* coverage of a pixel at squared distance d2 from a line of half width hw */
static inline float line_cov(float d2, float hw, float out2, float in2)
{
    if (d2 >= out2) return 0.0f;
    if (d2 <= in2) return 1.0f;
    float cov = hw + 0.5f - sqrtf(d2);
    return cov > 1.0f ? 1.0f : cov;
}

void fx_line(float x0, float y0, float x1, float y1, float w, uint16_t c, int a, int glow)
{
    a = (int)(a * s_galpha);
    if (a <= 0) return;
    x0 = tx(x0); y0 = ty(y0);
    x1 = tx(x1); y1 = ty(y1);
    float hw = w * s_zoom * 0.5f;
    if (hw < 0.35f) hw = 0.35f;
    float dx = x1 - x0, dy = y1 - y0;
    float l2 = dx * dx + dy * dy;
    if (l2 < 1e-4f) {
        disc_screen(x0, y0, hw, c, a, glow);
        return;
    }
    float inv_l2 = 1.0f / l2;
    const float out2 = (hw + 0.5f) * (hw + 0.5f);
    const float in2 = hw > 0.5f ? (hw - 0.5f) * (hw - 0.5f) : -1.0f;
    float len = sqrtf(l2);
    glowc_t g = glow_prep(c, glow);

    if (fabsf(dy) >= fabsf(dx)) {
        int ya = (int)floorf(fminf(y0, y1) - hw - 1), yb = (int)ceilf(fmaxf(y0, y1) + hw + 1);
        if (ya < 0) ya = 0;
        if (yb > H) yb = H;
        if (ya >= yb) return;
        if (g.amt) glow_rows(ya, yb);
        float span = (hw + 1.5f) * len / fabsf(dy) + hw;
        for (int py = ya; py < yb; py++) {
            float fy = py + 0.5f;
            float xc = x0 + (fy - y0) * dx / dy;
            int xa = (int)floorf(xc - span), xb = (int)ceilf(xc + span);
            if (xa < 0) xa = 0;
            if (xb > W) xb = W;
            for (int px = xa; px < xb; px++) {
                float cov = line_cov(seg_dist2(px + 0.5f, fy, x0, y0, dx, dy, inv_l2), hw, out2, in2);
                if (cov <= 0) continue;
                put(px, py, c, (int)(a * cov), &g);
            }
        }
    } else {
        int xa = (int)floorf(fminf(x0, x1) - hw - 1), xb = (int)ceilf(fmaxf(x0, x1) + hw + 1);
        if (xa < 0) xa = 0;
        if (xb > W) xb = W;
        if (xa >= xb) return;
        float span = (hw + 1.5f) * len / fabsf(dx) + hw;
        int ylo = (int)floorf(fminf(y0, y1) - span), yhi = (int)ceilf(fmaxf(y0, y1) + span);
        if (g.amt) glow_rows(ylo < 0 ? 0 : ylo, yhi > H ? H : yhi);
        for (int px = xa; px < xb; px++) {
            float fx = px + 0.5f;
            float yc = y0 + (fx - x0) * dy / dx;
            int ya = (int)floorf(yc - span), yb = (int)ceilf(yc + span);
            if (ya < 0) ya = 0;
            if (yb > H) yb = H;
            for (int py = ya; py < yb; py++) {
                float cov = line_cov(seg_dist2(fx, py + 0.5f, x0, y0, dx, dy, inv_l2), hw, out2, in2);
                if (cov <= 0) continue;
                put(px, py, c, (int)(a * cov), &g);
            }
        }
    }
}

/* Polygon fill: 4 sub-scanlines per pixel row, exact horizontal coverage. */
#define POLY_MAX 48
void fx_poly(const float *xy, int n, uint16_t c, int a, int glow)
{
    a = (int)(a * s_galpha);
    if (a <= 0 || n < 3) return;
    if (n > POLY_MAX) n = POLY_MAX;
    float px[POLY_MAX], py[POLY_MAX];
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    for (int i = 0; i < n; i++) {
        px[i] = tx(xy[2 * i]);
        py[i] = ty(xy[2 * i + 1]);
        if (px[i] < minx) minx = px[i];
        if (px[i] > maxx) maxx = px[i];
        if (py[i] < miny) miny = py[i];
        if (py[i] > maxy) maxy = py[i];
    }
    int y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
    int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx) + 1;
    if (y0 < 0) y0 = 0;
    if (y1 > H) y1 = H;
    if (x0 < 0) x0 = 0;
    if (x1 > W) x1 = W;
    if (y0 >= y1 || x0 >= x1) return;
    glowc_t g = glow_prep(c, glow);
    if (g.amt) glow_rows(y0, y1);

    static float acc[W + 1];
    for (int i = 0; i <= W; i++) acc[i] = 0;
    float xs[POLY_MAX];
    for (int row = y0; row < y1; row++) {
        int lo = W, hi = -1; /* touched range of acc[] for this row */
        bool any = false;
        for (int sub = 0; sub < 4; sub++) {
            float sy = row + (sub + 0.5f) * 0.25f;
            int nx = 0;
            for (int i = 0, j = n - 1; i < n; j = i++) {
                float yi = py[i], yj = py[j];
                if ((yi <= sy && yj > sy) || (yj <= sy && yi > sy)) {
                    xs[nx++] = px[i] + (sy - yi) * (px[j] - px[i]) / (yj - yi);
                }
            }
            for (int i = 1; i < nx; i++) { /* insertion sort */
                float v = xs[i];
                int k = i - 1;
                while (k >= 0 && xs[k] > v) { xs[k + 1] = xs[k]; k--; }
                xs[k + 1] = v;
            }
            for (int k = 0; k + 1 < nx; k += 2) {
                float xa = xs[k], xb = xs[k + 1];
                if (xa < 0) xa = 0;
                if (xb > W) xb = W;
                if (xb <= xa) continue;
                int ia = (int)xa, ib = (int)xb;
                any = true;
                if (ia < lo) lo = ia;
                if (ib > hi) hi = ib;
                if (ia == ib) {
                    acc[ia] += (xb - xa) * 0.25f;
                } else {
                    acc[ia] += (ia + 1 - xa) * 0.25f;
                    for (int q = ia + 1; q < ib; q++) acc[q] += 0.25f;
                    if (ib < W) acc[ib] += (xb - ib) * 0.25f;
                }
            }
        }
        if (!any) continue;
        if (hi > W - 1) hi = W - 1;
        for (int q = lo; q <= hi; q++) {
            float cov = acc[q];
            acc[q] = 0;
            if (cov <= 0.004f) continue;
            if (cov > 1) cov = 1;
            put(q, row, c, (int)(a * cov), &g);
        }
        if (hi + 1 <= W) acc[hi + 1] = 0;
    }
}

void fx_rect(float x, float y, float w, float h, uint16_t c, int a, int glow)
{
    float p[8] = {x, y, x + w, y, x + w, y + h, x, y + h};
    fx_poly(p, 4, c, a, glow);
}

void fx_radial(float x, float y, float r, uint16_t c, int a)
{
    a = (int)(a * s_galpha);
    if (a <= 0 || r <= 0) return;
    x = tx(x);
    y = ty(y);
    r *= s_zoom;
    int y0 = (int)floorf(y - r), y1 = (int)ceilf(y + r);
    if (y0 < 0) y0 = 0;
    if (y1 > H) y1 = H;
    const float inv_r2 = 1.0f / (r * r);
    for (int py = y0; py < y1; py++) {
        float dy = py + 0.5f - y;
        float h2 = r * r - dy * dy;
        if (h2 <= 0) continue;
        float hw = sqrtf(h2);
        int xa = (int)floorf(x - hw), xb = (int)ceilf(x + hw);
        if (xa < 0) xa = 0;
        if (xb > W) xb = W;
        for (int px = xa; px < xb; px++) {
            float dx = px + 0.5f - x;
            float k = 1.0f - (dx * dx + dy * dy) * inv_r2;
            if (k <= 0) continue;
            k *= k;
            int v = (int)(a * k * k); /* ~ (1 - d/r)^2 falloff without a sqrt */
            if (v <= 0) continue;
            int idx = py * W + px;
            s_fb[idx] = fx_add(s_fb[idx], c, v);
        }
    }
}
