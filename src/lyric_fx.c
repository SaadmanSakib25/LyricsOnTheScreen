/*
 * Lyric-video style renderer ("black screen lyrics" edits).
 *
 *  - Pure black background, only the line being sung is on screen.
 *  - Words appear one by one as they are sung (rise / pop / typewriter).
 *  - Anti-aliased, smoothed (EPX 2x) version of the 5x7 font, scaled
 *    to whatever size fits, with proportional letter spacing.
 *  - Real soft glow: text coverage is blurred and added back in color.
 *  - Drops pulse on the beat: zooming words, hue cycling, shockwave rings.
 *  - Slow drifting dust particles in the background.
 */
#include "lyric_fx.h"

#include <math.h>
#include <string.h>

#include "font5x7.h"
#include "lyrics_data.h"

#define W    FX_WIDTH
#define H    FX_HEIGHT
#define NPIX (W * H)

/* ------------------------------------------------------------------ */
/* Tunables                                                           */
/* ------------------------------------------------------------------ */
#define SHOW_PROGRESS_LINE 0      /* 1 = hairline song progress at the very bottom */

#define BEAT_MS      (60000.0f / SONG_BPM)
#define TEXT_AREA_W  118.0f       /* px available for a lyric row             */
#define TEXT_AREA_H  124.0f       /* px available for the whole lyric block   */
#define LINE_UNITS   10.0f        /* row pitch in font units (glyph is 8 tall) */
#define SPACE_UNITS  3.0f         /* word gap in font units                   */
#define EXIT_MS      280.0f       /* line fade-out before the next line       */
#define MAX_HOLD_MS  4000.0f      /* lines fade out after this long on screen */
#define GLOW_RADIUS  2            /* box blur radius (applied twice)          */
#define NUM_PARTICLES 30

#define MAX_WORDS      24
#define MAX_WORD_CHARS 24
#define MAX_ROWS       6

/* ------------------------------------------------------------------ */
/* Color helpers (RGB565, native endian)                              */
/* ------------------------------------------------------------------ */
static inline uint16_t rgb(int r, int g, int b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xFF) >> 3));
}

/* Alpha blend src over dst, a = 0..255 */
static inline uint16_t blend565(uint16_t d, uint16_t s, int a)
{
    a += a >> 7;
    int dr = d >> 11, dg = (d >> 5) & 63, db = d & 31;
    int sr = s >> 11, sg = (s >> 5) & 63, sb = s & 31;
    dr += ((sr - dr) * a) >> 8;
    dg += ((sg - dg) * a) >> 8;
    db += ((sb - db) * a) >> 8;
    return (uint16_t)((dr << 11) | (dg << 5) | db);
}

/* Additive (light) blend, saturating, a = 0..255 */
static inline uint16_t add565(uint16_t d, uint16_t s, int a)
{
    a += a >> 7;
    int r = (d >> 11) + (((s >> 11) * a) >> 8);
    int g = ((d >> 5) & 63) + ((((s >> 5) & 63) * a) >> 8);
    int b = (d & 31) + (((s & 31) * a) >> 8);
    if (r > 31) r = 31;
    if (g > 63) g = 63;
    if (b > 31) b = 31;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static uint16_t hsv565(float h, float s, float v)
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
    return rgb((int)((r + m) * 255), (int)((g + m) * 255), (int)((b + m) * 255));
}

static inline float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
static inline float ease_out_cubic(float k) { float u = 1.0f - k; return 1.0f - u * u * u; }

/* ------------------------------------------------------------------ */
/* Buffers                                                            */
/* ------------------------------------------------------------------ */
static uint16_t *s_fb;
static uint8_t s_glow[NPIX];   /* glow source mask, blurred in place  */
static uint8_t s_tmp[NPIX];    /* blur scratch                         */
static uint8_t s_tcov[NPIX];   /* text coverage (keeps glow off text)  */
static bool s_any_glow;
static float s_shake_x, s_shake_y;
static bool s_snap;            /* snap glyphs to whole pixels (crisper when still) */

/* ------------------------------------------------------------------ */
/* Smoothed proportional font                                         */
/* ------------------------------------------------------------------ */
static uint16_t s_glyph[256][16];   /* 10x16 EPX-upscaled bitmap, bit x = column */
static uint8_t s_left[256];         /* first inked source column                 */
static uint8_t s_width[256];        /* inked source columns (0 = blank)          */

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

        /* EPX / Scale2x: rounds off diagonals so big text isn't blocky */
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

static inline float glyph_adv(uint8_t c)
{
    if (c == ' ') return SPACE_UNITS;
    if (!s_width[c]) return 3.0f;
    return (float)s_width[c] + 1.0f;
}

/*
 * Draw one glyph with anti-aliasing (3x3 supersampling).
 *   x, y : top-left of the inked area, px     s : px per font unit
 *   alpha: 0..255                             glow_amt: 0..255 glow strength
 */
static void draw_glyph(uint8_t c, float x, float y, float s, uint16_t color, int alpha, int glow_amt)
{
    if (alpha <= 0 || s_width[c] == 0 || s < 0.25f) return;
    if (alpha > 255) alpha = 255;
    x += s_shake_x;
    y += s_shake_y;
    if (s_snap) {
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
            s_fb[idx] = blend565(s_fb[idx], color, a);
            if (a > s_tcov[idx]) s_tcov[idx] = (uint8_t)a;
            int g = a * glow_amt / 255;
            if (g > s_glow[idx]) {
                s_glow[idx] = (uint8_t)g;
                s_any_glow = true;
            }
        }
    }
}

static float text_units(const char *str)
{
    float u = 0;
    for (; *str; str++) u += glyph_adv((uint8_t)*str);
    return u > 0 ? u - 1.0f : 0;
}

static void draw_text_centered(const char *str, float cy, float s, uint16_t col, int alpha, int glow)
{
    float x = roundf((W - text_units(str) * s) / 2.0f);
    float y = roundf(cy - 4.0f * s);
    for (; *str; str++) {
        draw_glyph((uint8_t)*str, x, y, s, col, alpha, glow);
        x += glyph_adv((uint8_t)*str) * s;
    }
}

/* ------------------------------------------------------------------ */
/* Simple primitives (background / overlays)                          */
/* ------------------------------------------------------------------ */
static inline void plot_add(int x, int y, uint16_t c, int a)
{
    if ((unsigned)x >= W || (unsigned)y >= H || a <= 0) return;
    s_fb[y * W + x] = add565(s_fb[y * W + x], c, a > 255 ? 255 : a);
}

static void rect_blend(int x, int y, int w, int h, uint16_t c, int a)
{
    for (int j = y; j < y + h; j++) {
        if ((unsigned)j >= H) continue;
        for (int i = x; i < x + w; i++) {
            if ((unsigned)i >= W) continue;
            s_fb[j * W + i] = blend565(s_fb[j * W + i], c, a);
        }
    }
}

static void ring(int cx, int cy, int r, uint16_t c, int a)
{
    if (r <= 0 || a <= 0) return;
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        plot_add(cx + x, cy + y, c, a); plot_add(cx - x, cy + y, c, a);
        plot_add(cx + x, cy - y, c, a); plot_add(cx - x, cy - y, c, a);
        plot_add(cx + y, cy + x, c, a); plot_add(cx - y, cy + x, c, a);
        plot_add(cx + y, cy - x, c, a); plot_add(cx - y, cy - x, c, a);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Glow                                                               */
/* ------------------------------------------------------------------ */
static void blur_h(const uint8_t *src, uint8_t *dst)
{
    const int r = GLOW_RADIUS, div = 2 * r + 1;
    for (int y = 0; y < H; y++) {
        const uint8_t *s = src + y * W;
        uint8_t *d = dst + y * W;
        int sum = 0;
        for (int x = 0; x <= r; x++) sum += s[x];
        for (int x = 0; x < W; x++) {
            d[x] = (uint8_t)(sum / div);
            if (x + r + 1 < W) sum += s[x + r + 1];
            if (x - r >= 0) sum -= s[x - r];
        }
    }
}

static void blur_v(const uint8_t *src, uint8_t *dst)
{
    const int r = GLOW_RADIUS, div = 2 * r + 1;
    for (int x = 0; x < W; x++) {
        int sum = 0;
        for (int y = 0; y <= r; y++) sum += src[y * W + x];
        for (int y = 0; y < H; y++) {
            dst[y * W + x] = (uint8_t)(sum / div);
            if (y + r + 1 < H) sum += src[(y + r + 1) * W + x];
            if (y - r >= 0) sum -= src[(y - r) * W + x];
        }
    }
}

static void composite_glow(uint16_t color, int gain256)
{
    if (!s_any_glow) return;
    blur_h(s_glow, s_tmp);
    blur_v(s_tmp, s_glow);
    blur_h(s_glow, s_tmp);
    blur_v(s_tmp, s_glow);
    for (int i = 0; i < NPIX; i++) {
        int g = s_glow[i];
        if (!g) continue;
        g = (g * gain256) >> 8;
        if (g > 255) g = 255;
        g = g * (255 - s_tcov[i]) / 255;
        if (g) s_fb[i] = add565(s_fb[i], color, g);
    }
}

/* ------------------------------------------------------------------ */
/* Background particles                                               */
/* ------------------------------------------------------------------ */
typedef struct {
    float x, y, v, ph;
    float b;
} particle_t;

static particle_t s_parts[NUM_PARTICLES];
static uint32_t s_rng = 0x2545F491u;

static float frand(void)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return (float)(s_rng >> 8) * (1.0f / 16777216.0f);
}

static void particles_init(void)
{
    for (int i = 0; i < NUM_PARTICLES; i++) {
        s_parts[i].x = frand() * W;
        s_parts[i].y = frand() * H;
        s_parts[i].v = 0.06f + frand() * 0.22f;
        s_parts[i].ph = frand() * 6.283f;
        s_parts[i].b = 25.0f + frand() * 80.0f;
    }
}

static void particles_draw(uint32_t tick, float speed, uint16_t tint, float strength)
{
    for (int i = 0; i < NUM_PARTICLES; i++) {
        particle_t *p = &s_parts[i];
        p->y -= p->v * speed;
        p->x += sinf(p->ph + tick * 0.02f) * 0.12f;
        if (p->y < -2.0f) {
            p->y = H + 2.0f;
            p->x = frand() * W;
        }
        float tw = 0.55f + 0.45f * sinf(p->ph + tick * 0.09f);
        int a = (int)(p->b * tw * strength);
        int x = (int)p->x, y = (int)p->y;
        plot_add(x, y, tint, a);
        if (p->b > 80.0f) { /* a few bigger, softer motes */
            plot_add(x + 1, y, tint, a / 3);
            plot_add(x - 1, y, tint, a / 3);
            plot_add(x, y + 1, tint, a / 3);
            plot_add(x, y - 1, tint, a / 3);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Line layout (cached per line)                                      */
/* ------------------------------------------------------------------ */
typedef struct {
    char ch[MAX_WORD_CHARS];
    uint32_t emph;      /* bit i = char i uses the accent color */
    uint8_t len;
    bool brk;           /* forced line break before this word */
    float units;        /* width in font units */
    int chars_before;   /* sung characters before this word (timing) */
    int row;
    float x_units;      /* x within its row, font units */
} word_t;

typedef struct {
    bool valid;
    size_t index;
    word_t w[MAX_WORDS];
    int nwords;
    int total_chars;
    int nrows;
    float row_units[MAX_ROWS];
    float scale;
} layout_t;

static layout_t s_lay;

static void layout_parse(layout_t *L, const char *text)
{
    L->nwords = 0;
    L->total_chars = 0;
    bool emph = false, brk = false;
    word_t *cur = NULL;

    for (const char *p = text;; p++) {
        char c = *p;
        if (c == '\0' || c == ' ' || c == '|') {
            cur = NULL;
            if (c == '|') brk = true;
            if (c == '\0') break;
            continue;
        }
        if (c == '*') {
            emph = !emph;
            continue;
        }
        if (!cur) {
            if (L->nwords >= MAX_WORDS) continue;
            cur = &L->w[L->nwords++];
            memset(cur, 0, sizeof(*cur));
            cur->brk = brk;
            cur->chars_before = L->total_chars;
            brk = false;
        }
        if (cur->len < MAX_WORD_CHARS - 1) {
            if (emph) cur->emph |= 1u << cur->len;
            cur->ch[cur->len++] = c;
            L->total_chars++;
        }
    }

    for (int i = 0; i < L->nwords; i++) {
        float u = 0;
        for (int k = 0; k < L->w[i].len; k++) u += glyph_adv((uint8_t)L->w[i].ch[k]);
        L->w[i].units = u > 0 ? u - 1.0f : 0;
    }
}

static bool layout_wrap(layout_t *L, float s, int max_rows)
{
    const float avail = TEXT_AREA_W / s;
    int row = 0;
    float cur = 0;
    for (int i = 0; i < L->nwords; i++) {
        word_t *w = &L->w[i];
        if (w->units > avail) return false;
        if (i > 0 && (w->brk || cur + SPACE_UNITS + w->units > avail)) {
            L->row_units[row] = cur;
            if (++row >= max_rows) return false;
            cur = 0;
        }
        if (cur > 0) cur += SPACE_UNITS;
        w->row = row;
        w->x_units = cur;
        cur += w->units;
    }
    L->row_units[row] = cur;
    L->nrows = row + 1;
    float h = (L->nrows * LINE_UNITS - (LINE_UNITS - 8.0f)) * s;
    return h <= TEXT_AREA_H;
}

static float style_max_scale(uint8_t style)
{
    switch (style) {
    case LS_TYPE:   return 2.0f;
    case LS_CHORUS: return 3.0f;
    case LS_DROP:   return 3.0f;
    case LS_TITLE:  return 3.0f;
    case LS_BREAK:  return 1.0f;
    default:        return 2.5f;
    }
}

static void layout_build(size_t index)
{
    layout_t *L = &s_lay;
    layout_parse(L, lyrics[index].text);
    float s;
    for (s = style_max_scale(lyrics[index].style); s >= 1.0f; s -= 0.5f) {
        if (layout_wrap(L, s, 4)) break;
    }
    if (s < 1.0f) {
        s = 1.0f;
        layout_wrap(L, s, MAX_ROWS);
    }
    L->scale = s;
    L->index = index;
    L->valid = true;
}

/* ------------------------------------------------------------------ */
/* Scene rendering                                                    */
/* ------------------------------------------------------------------ */
typedef struct {
    uint16_t text, accent, glow;
} palette_t;

static palette_t style_palette(uint8_t style, size_t index, float t)
{
    palette_t p;
    switch (style) {
    case LS_TYPE:
        p.text = rgb(255, 255, 255); p.accent = rgb(200, 170, 255); p.glow = rgb(130, 80, 255);
        break;
    case LS_CHORUS:
        p.text = rgb(255, 255, 255); p.accent = rgb(255, 130, 200); p.glow = rgb(255, 50, 140);
        break;
    case LS_TITLE:
        p.text = rgb(255, 255, 255); p.accent = rgb(255, 200, 110); p.glow = rgb(255, 120, 40);
        break;
    case LS_BREAK:
        p.text = rgb(150, 150, 175); p.accent = rgb(200, 200, 255); p.glow = rgb(110, 110, 255);
        break;
    case LS_DROP: {
        float hue = (float)index * 53.0f + t * 0.03f;
        p.text = hsv565(hue, 0.25f, 1.0f);
        p.accent = hsv565(hue + 40.0f, 0.7f, 1.0f);
        p.glow = hsv565(hue, 0.9f, 1.0f);
        break;
    }
    default: /* LS_VERSE */
        p.text = rgb(255, 255, 255); p.accent = rgb(140, 210, 255); p.glow = rgb(40, 110, 255);
        break;
    }
    return p;
}

/*
 * Draw the active line's words, each animated relative to the moment it is sung.
 * Returns nothing; writes into the framebuffer / glow masks.
 */
static void draw_lyric_words(uint8_t style, size_t index, float t, float reveal, float exit_k,
                             float pulse, float center_y, const palette_t *pal)
{
    const layout_t *L = &s_lay;
    const float s = L->scale;
    const float vis = 1.0f - exit_k * exit_k;
    const float total = (float)(L->total_chars > 0 ? L->total_chars : 1);
    const float char_ms = reveal / total;

    float block_h = (L->nrows * LINE_UNITS - (LINE_UNITS - 8.0f)) * s;
    float top = center_y - block_h / 2.0f;

    /* typewriter cursor tracking */
    float cur_x = -1, cur_y = 0;
    bool line_done = true;

    for (int i = 0; i < L->nwords; i++) {
        const word_t *w = &L->w[i];
        float rt = reveal * (float)w->chars_before / total;
        float dt = t - rt;
        if (dt < 0) {
            line_done = false;
            continue;
        }

        float row_y = top + w->row * LINE_UNITS * s;
        float row_x = (W - L->row_units[w->row] * s) / 2.0f;
        float cx = row_x + (w->x_units + w->units / 2.0f) * s;
        float cy = row_y + 4.0f * s;

        float mul = 1.0f, dy = 0.0f, a = 1.0f, k;
        float type_ms = 0.0f;
        bool settled = true;

        switch (style) {
        case LS_TYPE:
            type_ms = char_ms;
            settled = true;
            break;
        case LS_CHORUS:
            k = clamp01(dt / 220.0f);
            a = clamp01(dt / 110.0f);
            mul = 1.0f + 0.55f * (1.0f - ease_out_cubic(k));
            settled = k >= 1.0f;
            break;
        case LS_DROP:
            k = clamp01(dt / 200.0f);
            a = clamp01(dt / 90.0f);
            {
                /* zoom-in amount limited so long words don't fly far off-screen */
                float pop = 150.0f / (w->units * s + 1.0f) - 1.0f;
                if (pop > 0.9f) pop = 0.9f;
                if (pop < 0.2f) pop = 0.2f;
                mul = (1.0f + pop * (1.0f - ease_out_cubic(k))) * (1.0f + 0.07f * pulse);
            }
            settled = false;
            break;
        case LS_TITLE:
            k = clamp01(dt / 650.0f);
            a = ease_out_cubic(k);
            mul = 1.0f + 0.3f * (1.0f - ease_out_cubic(k));
            settled = k >= 1.0f;
            break;
        case LS_BREAK:
            k = clamp01(dt / 600.0f);
            a = k;
            dy = 4.0f * (1.0f - ease_out_cubic(k));
            settled = k >= 1.0f;
            break;
        default: /* LS_VERSE */
            k = clamp01(dt / 320.0f);
            a = ease_out_cubic(k);
            dy = 10.0f * (1.0f - ease_out_cubic(k));
            settled = k >= 1.0f;
            break;
        }

        /* exit: drops zoom out through the camera, others drift up */
        if (exit_k > 0) {
            settled = false;
            if (style == LS_DROP) {
                float z = 1.0f + 0.4f * exit_k;
                cx = W / 2.0f + (cx - W / 2.0f) * z;
                cy = center_y + (cy - center_y) * z;
                mul *= z;
            } else {
                dy -= 10.0f * exit_k;
            }
        }

        float boost = expf(-dt / 380.0f); /* the word just sung glows brighter */
        int alpha = (int)(255.0f * a * vis);
        int glow = 95 + (int)(160.0f * boost);
        int eglow = 200 + (int)(55.0f * boost);

        uint16_t col = pal->text, ecol = pal->accent;
        if (style == LS_DROP) {
            float hue = (float)index * 53.0f + (float)i * 38.0f + t * 0.03f;
            col = hsv565(hue, 0.28f, 1.0f);
            ecol = hsv565(hue + 40.0f, 0.75f, 1.0f);
        }

        float sf = s * mul;
        float x = cx - w->units * sf / 2.0f;
        float y = cy - 4.0f * sf + dy;
        s_snap = settled;

        for (int c = 0; c < w->len; c++) {
            int ca = alpha;
            if (type_ms > 0) {
                float cdt = dt - c * type_ms;
                if (cdt < 0) {
                    line_done = false;
                    break;
                }
                if (cdt < 70.0f) ca = (int)(alpha * cdt / 70.0f);
            }
            bool e = (w->emph >> c) & 1u;
            uint8_t ch = (uint8_t)w->ch[c];
            draw_glyph(ch, x, y, sf, e ? ecol : col, ca, e ? eglow : glow);
            x += glyph_adv(ch) * sf;
            cur_x = x;
            cur_y = y;
        }
        s_snap = false;
    }

    /* blinking typewriter cursor */
    if (style == LS_TYPE && cur_x >= 0) {
        bool on = !line_done || ((int)(t / 420.0f) % 2 == 0);
        if (on && vis > 0.05f) {
            int cw = (int)(s * 0.75f + 0.5f);
            if (cw < 1) cw = 1;
            rect_blend((int)roundf(cur_x), (int)roundf(cur_y), cw, (int)(8.0f * s),
                       pal->accent, (int)(220.0f * vis));
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */
void fx_init(void)
{
    font_prepare();
    particles_init();
    s_lay.valid = false;
}

void fx_render(uint16_t *fb, uint32_t song_ms, size_t li, uint32_t tick, bool is_playing)
{
    s_fb = fb;
    s_any_glow = false;
    s_shake_x = s_shake_y = 0;
    memset(s_glow, 0, sizeof(s_glow));
    memset(s_tcov, 0, sizeof(s_tcov));

    if (li >= LYRICS_COUNT) li = LYRICS_COUNT - 1;
    const lyric_line_t *ln = &lyrics[li];
    const uint8_t style = ln->style;
    if (!s_lay.valid || s_lay.index != li) layout_build(li);

    /* ---- timing ---- */
    float t = song_ms > ln->start_ms ? (float)(song_ms - ln->start_ms) : 0.0f;
    uint32_t next_ms = (li + 1 < LYRICS_COUNT) ? lyrics[li + 1].start_ms
                                               : SONG_TOTAL_DURATION_MS + FX_END_HOLD_MS;
    float dur = (float)(next_ms - ln->start_ms);
    float per_char = (style == LS_TYPE) ? 55.0f : (style == LS_TITLE) ? 140.0f : 70.0f;
    float reveal = fminf(dur * 0.72f, (float)s_lay.total_chars * per_char);
    if (reveal < 1.0f) reveal = 1.0f;
    float exit_start = dur - EXIT_MS;
    if (style != LS_BREAK && exit_start > reveal + MAX_HOLD_MS) exit_start = reveal + MAX_HOLD_MS;
    if (exit_start < reveal) exit_start = reveal;
    float exit_k = clamp01((t - exit_start) / EXIT_MS);
    float vis = 1.0f - exit_k * exit_k;

    /* beat clock, anchored to the start of the line */
    float bphase = fmodf(t, BEAT_MS);
    float pulse = expf(-bphase / 140.0f);
    float bfrac = bphase / BEAT_MS;

    palette_t pal = style_palette(style, li, t);

    /* ---- background ---- */
    uint16_t bg = 0;
    if (style == LS_DROP) bg = add565(0, pal.glow, (int)(20.0f * pulse));
    for (int i = 0; i < NPIX; i++) fb[i] = bg;

    if (style == LS_DROP) {
        float u = 1.0f - bfrac;
        ring(W / 2, H / 2 - 4, (int)(6.0f + bfrac * 90.0f), pal.glow, (int)(130.0f * u * u));
        ring(W / 2, H / 2 - 4, (int)(7.0f + bfrac * 90.0f), pal.glow, (int)(60.0f * u * u));
        particles_draw(tick, 4.0f + 6.0f * pulse, blend565(0xFFFF, pal.glow, 140), 1.6f);
    } else if (style == LS_BREAK) {
        float u = 1.0f - bfrac;
        ring(W / 2, 68, (int)(14.0f + bfrac * 60.0f), pal.glow, (int)(55.0f * u * u * vis));
        particles_draw(tick, 1.5f, blend565(0xFFFF, pal.glow, 110), 1.2f);
    } else {
        particles_draw(tick, 1.0f, blend565(0xFFFF, pal.glow, 90), 1.0f);
    }

    /* ---- foreground ---- */
    int glow_gain = 512;
    if (style == LS_DROP) {
        if (is_playing && pulse > 0.35f) {
            s_shake_x = sinf((float)tick * 2.7f) * 1.6f * pulse;
            s_shake_y = cosf((float)tick * 3.1f) * 1.2f * pulse;
        }
        glow_gain = 512 + (int)(380.0f * pulse);
        draw_lyric_words(style, li, t, reveal, exit_k, pulse, 76.0f, &pal);
    } else if (style == LS_BREAK) {
        /* pulsing music note with a small caption */
        float a = clamp01(t / 600.0f) * vis;
        float sf = 3.0f * (1.0f + 0.12f * pulse);
        uint8_t note = 0x0E;
        float nx = W / 2.0f - s_width[note] * sf / 2.0f;
        float ny = 68.0f - 4.0f * sf;
        draw_glyph(note, nx, ny, sf, pal.accent, (int)(255.0f * a), 255);
        draw_lyric_words(style, li, t, reveal, exit_k, pulse, 108.0f, &pal);
        glow_gain = 512 + (int)(200.0f * pulse);
    } else if (style == LS_TITLE) {
        draw_lyric_words(style, li, t, reveal, exit_k, pulse, 64.0f, &pal);
        /* accent rule + artist line fade in after the title */
        float k = clamp01((t - reveal - 150.0f) / 700.0f);
        float e = ease_out_cubic(k) * vis;
        int half = (int)(22.0f * e);
        rect_blend(W / 2 - half, 110, half * 2, 1, pal.accent, (int)(200.0f * e));
        draw_text_centered(SONG_ARTIST, 124.0f, 1.0f, rgb(175, 175, 195), (int)(255.0f * e), 50);
    } else {
        draw_lyric_words(style, li, t, reveal, exit_k, pulse, 78.0f, &pal);
    }

    s_shake_x = s_shake_y = 0;
    composite_glow(pal.glow, glow_gain);

    /* ---- overlays ---- */
    if (!is_playing) {
        int a = 150 + (int)(80.0f * sinf((float)tick * 0.15f));
        rect_blend(W / 2 - 5, 146, 3, 9, rgb(220, 220, 230), a);
        rect_blend(W / 2 + 2, 146, 3, 9, rgb(220, 220, 230), a);
    }
#if SHOW_PROGRESS_LINE
    {
        uint32_t pos = song_ms > SONG_TOTAL_DURATION_MS ? SONG_TOTAL_DURATION_MS : song_ms;
        int pw = (int)((uint64_t)pos * W / SONG_TOTAL_DURATION_MS);
        rect_blend(0, H - 1, pw, 1, rgb(90, 90, 110), 255);
    }
#endif
}
