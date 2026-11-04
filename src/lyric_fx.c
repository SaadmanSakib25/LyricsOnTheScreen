/*
 * Lyric-video renderer: puts together each frame from
 *   1. an animated illustration for the line (scenes.c), with a slow
 *      cinematic push-in camera and fades between shots,
 *   2. kinetic word-by-word lyric typography with glow,
 *   3. beat-synced drama on the drops (hard cuts, flashes, shake),
 *   4. bloom + vignette (fx_draw.c).
 */
#include "lyric_fx.h"

#include <math.h>
#include <string.h>

#include "fx_draw.h"
#include "lyrics_data.h"
#include "scenes.h"

#define W FXW
#define H FXH

/* ------------------------------------------------------------------ */
/* Tunables                                                           */
/* ------------------------------------------------------------------ */
#define SHOW_PROGRESS_LINE 0      /* 1 = hairline song progress at the very bottom */

#define BEAT_MS        (60000.0f / SONG_BPM)
#define TEXT_AREA_W    118.0f
#define LINE_UNITS     10.0f      /* row pitch in font units (glyph is 8 tall) */
#define SPACE_UNITS    3.0f
#define EXIT_MS        280.0f     /* text fade-out before the next line        */
#define MAX_HOLD_MS    4000.0f    /* text fades after this long on screen      */
#define SCENE_FADE_MS  450.0f     /* fade between different scenes             */
#define NUM_PARTICLES  30

#define MAX_WORDS      24
#define MAX_WORD_CHARS 24
#define MAX_ROWS       6

/* ------------------------------------------------------------------ */
/* Ambient particles (used when a line has no scene)                  */
/* ------------------------------------------------------------------ */
typedef struct {
    float x, y, v, ph, b;
} particle_t;

static particle_t s_parts[NUM_PARTICLES];

static void particles_init(void)
{
    for (int i = 0; i < NUM_PARTICLES; i++) {
        s_parts[i].x = fx_hashf(i * 5 + 1) * W;
        s_parts[i].y = fx_hashf(i * 5 + 2) * H;
        s_parts[i].v = 0.06f + fx_hashf(i * 5 + 3) * 0.22f;
        s_parts[i].ph = fx_hashf(i * 5 + 4) * FX_TAU;
        s_parts[i].b = 25.0f + fx_hashf(i * 5 + 5) * 80.0f;
    }
}

static void particles_draw(uint32_t tick, uint16_t tint)
{
    for (int i = 0; i < NUM_PARTICLES; i++) {
        particle_t *p = &s_parts[i];
        p->y -= p->v;
        p->x += sinf(p->ph + tick * 0.02f) * 0.12f;
        if (p->y < -2.0f) {
            p->y = H + 2.0f;
            p->x = fx_hashf(tick + i) * W;
        }
        float tw = 0.55f + 0.45f * sinf(p->ph + tick * 0.09f);
        fx_plot_add((int)p->x, (int)p->y, tint, (int)(p->b * tw));
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
    float x_units;
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
        for (int k = 0; k < L->w[i].len; k++) u += fx_glyph_adv((uint8_t)L->w[i].ch[k]);
        L->w[i].units = u > 0 ? u - 1.0f : 0;
    }
}

static float block_height(const layout_t *L, float s)
{
    return (L->nrows * LINE_UNITS - (LINE_UNITS - 8.0f)) * s;
}

static bool layout_wrap(layout_t *L, float s, int max_rows, float max_h)
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
    return block_height(L, s) <= max_h;
}

/* text placement for a line, depending on its style and scene */
typedef struct {
    float center_y, max_scale, max_h;
    bool backdrop;      /* darken a band behind the text so it reads over the scene */
} text_place_t;

static text_place_t text_place(uint8_t style, uint8_t scene)
{
    text_place_t p = {78.0f, 2.5f, 124.0f, false};
    switch (style) {
    case LS_TYPE:   p.max_scale = 2.0f; break;
    case LS_CHORUS: p.max_scale = 3.0f; break;
    case LS_DROP:   p.max_scale = 3.0f; p.center_y = 76.0f; break;
    case LS_TITLE:  p.max_scale = 3.0f; p.center_y = 64.0f; break;
    case LS_BREAK:  p.max_scale = 1.0f; p.center_y = 108.0f; break;
    default: break;
    }
    switch (scene_layout(scene)) {
    case SL_FULL:
        if (style == LS_BREAK) p.center_y = 146.0f;
        p.backdrop = (style == LS_DROP);
        break;
    case SL_FLOOR:
        p.center_y = (style == LS_BREAK) ? 146.0f : 120.0f;
        p.max_h = 70.0f;
        p.backdrop = true;
        break;
    case SL_ILLUS:
        p.center_y = 129.0f;
        if (p.max_scale > 2.0f) p.max_scale = 2.0f;
        p.max_h = 54.0f;
        p.backdrop = true;
        break;
    default:
        break;
    }
    return p;
}

static void layout_build(size_t index, const text_place_t *tp)
{
    layout_t *L = &s_lay;
    layout_parse(L, lyrics[index].text);
    float s;
    for (s = tp->max_scale; s >= 1.0f; s -= 0.5f) {
        if (layout_wrap(L, s, 4, tp->max_h)) break;
    }
    if (s < 1.0f) {
        s = 1.0f;
        layout_wrap(L, s, MAX_ROWS, 1000.0f);
    }
    L->scale = s;
    L->index = index;
    L->valid = true;
}

/* ------------------------------------------------------------------ */
/* Lyric text                                                         */
/* ------------------------------------------------------------------ */
typedef struct {
    uint16_t text, accent, glow;
} palette_t;

static palette_t style_palette(uint8_t style, float hue)
{
    palette_t p;
    switch (style) {
    case LS_TYPE:
        p.text = fx_rgb(255, 255, 255); p.accent = fx_rgb(200, 170, 255); p.glow = fx_rgb(130, 80, 255);
        break;
    case LS_CHORUS:
        p.text = fx_rgb(255, 255, 255); p.accent = fx_rgb(255, 130, 200); p.glow = fx_rgb(255, 50, 140);
        break;
    case LS_TITLE:
        p.text = fx_rgb(255, 255, 255); p.accent = fx_rgb(255, 200, 110); p.glow = fx_rgb(255, 120, 40);
        break;
    case LS_BREAK:
        p.text = fx_rgb(170, 170, 195); p.accent = fx_rgb(200, 200, 255); p.glow = fx_rgb(110, 110, 255);
        break;
    case LS_DROP:
        p.text = fx_hsv(hue, 0.25f, 1.0f);
        p.accent = fx_hsv(hue + 40.0f, 0.7f, 1.0f);
        p.glow = fx_hsv(hue, 0.9f, 1.0f);
        break;
    default: /* LS_VERSE */
        p.text = fx_rgb(255, 255, 255); p.accent = fx_rgb(140, 210, 255); p.glow = fx_rgb(40, 110, 255);
        break;
    }
    return p;
}

static void draw_lyric_words(uint8_t style, float t, float reveal, float exit_k,
                             float pulse, float center_y, float hue, const palette_t *pal)
{
    const layout_t *L = &s_lay;
    const float s = L->scale;
    const float vis = 1.0f - exit_k * exit_k;
    const float total = (float)(L->total_chars > 0 ? L->total_chars : 1);
    const float char_ms = reveal / total;
    const float top = center_y - block_height(L, s) / 2.0f;

    float cur_x = -1, cur_y = 0;
    bool line_done = true;

    for (int i = 0; i < L->nwords; i++) {
        const word_t *w = &L->w[i];
        float dt = t - reveal * (float)w->chars_before / total;
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
            break;
        case LS_CHORUS:
            k = fx_clamp01(dt / 220.0f);
            a = fx_clamp01(dt / 110.0f);
            mul = 1.0f + 0.55f * (1.0f - fx_ease_out(k));
            settled = k >= 1.0f;
            break;
        case LS_DROP: {
            k = fx_clamp01(dt / 200.0f);
            a = fx_clamp01(dt / 90.0f);
            float pop = 150.0f / (w->units * s + 1.0f) - 1.0f; /* keep long words on screen */
            if (pop > 0.9f) pop = 0.9f;
            if (pop < 0.2f) pop = 0.2f;
            mul = (1.0f + pop * (1.0f - fx_ease_out(k))) * (1.0f + 0.07f * pulse);
            settled = false;
            break;
        }
        case LS_TITLE:
            k = fx_clamp01(dt / 650.0f);
            a = fx_ease_out(k);
            mul = 1.0f + 0.3f * (1.0f - fx_ease_out(k));
            settled = k >= 1.0f;
            break;
        case LS_BREAK:
            k = fx_clamp01(dt / 600.0f);
            a = k;
            dy = 4.0f * (1.0f - fx_ease_out(k));
            settled = k >= 1.0f;
            break;
        default: /* LS_VERSE */
            k = fx_clamp01(dt / 320.0f);
            a = fx_ease_out(k);
            dy = 10.0f * (1.0f - fx_ease_out(k));
            settled = k >= 1.0f;
            break;
        }

        if (exit_k > 0) {
            settled = false;
            if (style == LS_DROP) { /* zoom out through the camera */
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
            float h = hue + (float)i * 38.0f;
            col = fx_hsv(h, 0.28f, 1.0f);
            ecol = fx_hsv(h + 40.0f, 0.75f, 1.0f);
        }

        float sf = s * mul;
        float x = cx - w->units * sf / 2.0f;
        float y = cy - 4.0f * sf + dy;

        for (int ci = 0; ci < w->len; ci++) {
            int ca = alpha;
            if (type_ms > 0) {
                float cdt = dt - ci * type_ms;
                if (cdt < 0) {
                    line_done = false;
                    break;
                }
                if (cdt < 70.0f) ca = (int)(alpha * cdt / 70.0f);
            }
            bool e = (w->emph >> ci) & 1u;
            uint8_t ch = (uint8_t)w->ch[ci];
            fx_glyph(ch, x, y, sf, e ? ecol : col, ca, pal->glow, e ? eglow : glow, settled);
            x += fx_glyph_adv(ch) * sf;
            cur_x = x;
            cur_y = y;
        }
    }

    if (style == LS_TYPE && cur_x >= 0) { /* blinking typewriter cursor */
        bool on = !line_done || ((int)(t / 420.0f) % 2 == 0);
        if (on && vis > 0.05f) {
            float cw = fmaxf(1.0f, roundf(s * 0.75f));
            fx_rect(roundf(cur_x), roundf(cur_y), cw, 8.0f * s, pal->accent, (int)(220.0f * vis), 120);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */
void fx_init(void)
{
    fx_draw_init();
    particles_init();
    s_lay.valid = false;
}

void fx_draw_frame(int slot, uint16_t *fb, uint32_t song_ms, size_t li, uint32_t tick,
                   bool is_playing, fx_post_t *post)
{
    fx_begin(fb, slot);

    if (li >= LYRICS_COUNT) li = LYRICS_COUNT - 1;
    const lyric_line_t *ln = &lyrics[li];
    const uint8_t style = ln->style, scene = ln->scene;
    const bool drop = (style == LS_DROP);
    text_place_t tp = text_place(style, scene);
    if (!s_lay.valid || s_lay.index != li) layout_build(li, &tp);

    /* ---- line timing ---- */
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
    float exit_k = fx_clamp01((t - exit_start) / EXIT_MS);
    float vis = 1.0f - exit_k * exit_k;

    /* ---- beat clock, anchored to the start of the line ---- */
    float bphase = fmodf(t, BEAT_MS);
    float pulse = expf(-bphase / 140.0f);
    float hue = (float)li * 53.0f + t * 0.03f;
    palette_t pal = style_palette(style, hue);

    /* ---- scene shot timing: consecutive lines with one scene are one shot ---- */
    size_t run = li;
    while (run > 0 && lyrics[run - 1].scene == scene) run--;
    float t_scene = song_ms > lyrics[run].start_ms ? (float)(song_ms - lyrics[run].start_ms) : 0.0f;
    bool next_same = (li + 1 < LYRICS_COUNT) && lyrics[li + 1].scene == scene;
    bool next_drop = (li + 1 < LYRICS_COUNT) && lyrics[li + 1].style == LS_DROP;
    float s_in = drop ? 1.0f : fx_clamp01(t_scene / SCENE_FADE_MS); /* drops hard-cut */
    float s_out = 1.0f;
    if (!next_same && !(drop && next_drop)) {
        s_out = 1.0f - fx_clamp01((t - (dur - SCENE_FADE_MS)) / SCENE_FADE_MS);
    }
    float scene_alpha = s_in * s_out;

    /* ---- camera: slow push-in, punch on the beat during drops ---- */
    scene_layout_t sl = scene_layout(scene);
    float push = fx_clamp01(t_scene / 9000.0f);
    float zoom = 1.0f + (sl == SL_ILLUS ? 0.07f : 0.04f) * push;
    if (drop) zoom *= 1.0f + 0.035f * pulse;
    float shx = 0, shy = 0;
    if (drop && is_playing && pulse > 0.35f) {
        shx = sinf((float)tick * 2.7f) * 1.6f * pulse;
        shy = cosf((float)tick * 3.1f) * 1.2f * pulse;
    }

    /* ---- background / scene ---- */
    fx_fill(0);
    if (scene != SC_NONE) {
        scene_ctx_t sc = {
            .t = t_scene, .line_t = t, .pulse = pulse, .bfrac = bphase / BEAT_MS,
            .hue = hue, .drop = drop,
        };
        if (sl == SL_ILLUS) fx_camera(zoom, 64, 52, 64, 52);
        else fx_camera(zoom, 64, 76, 64, 76);
        fx_set_shake(shx, shy);
        fx_set_alpha(scene_alpha);
        scene_draw(scene, &sc);
        fx_set_alpha(1.0f);
        fx_camera_reset();
    } else {
        particles_draw(tick, fx_blend(0xFFFF, pal.glow, 90));
    }

    /* ---- lyric text ---- */
    fx_set_shake(shx, shy);
    if (tp.backdrop && s_lay.nwords > 0) {
        float bh = block_height(&s_lay, s_lay.scale);
        fx_shade_band(tp.center_y - bh / 2 - 3, tp.center_y + bh / 2 + 3, 10, (int)(150 * vis));
    }
    draw_lyric_words(style, t, reveal, exit_k, pulse, tp.center_y, hue, &pal);
    if (style == LS_TITLE) {
        float k = fx_clamp01((t - reveal - 150.0f) / 700.0f);
        float e = fx_ease_out(k) * vis;
        float half = 22.0f * e;
        fx_set_shake(0, 0);
        fx_rect(W / 2 - half, 110, half * 2, 1, pal.accent, (int)(200.0f * e), 150);
        fx_text(SONG_ARTIST, W / 2.0f, 124.0f, 1.0f, fx_rgb(175, 175, 195), (int)(255.0f * e), 50);
    }
    fx_set_shake(0, 0);

    /* ---- hand over to the finish stage ---- */
    post->glow_gain = 512 + (drop ? (int)(380.0f * pulse) : 0);
    post->vignette = scene != SC_NONE;
    post->flash = 0;
    if (drop && t < 300.0f) { /* white flash on every new drop line, bigger on the first */
        bool first = li == 0 || lyrics[li - 1].style != LS_DROP;
        post->flash = (int)((first ? 235.0f : 130.0f) * expf(-t / (first ? 140.0f : 90.0f)));
    }
    post->paused = !is_playing;
    post->tick = tick;
    post->song_ms = song_ms;
}

void fx_finish_frame(int slot, uint16_t *fb, const fx_post_t *post)
{
    fx_finish(slot, fb, post->glow_gain, post->vignette);
    fx_post_flash(fb, 0xFFFF, post->flash);
    if (post->paused) {
        int a = 150 + (int)(80.0f * sinf((float)post->tick * 0.15f));
        fx_post_rect(fb, W / 2 - 5, 146, 3, 9, fx_rgb(220, 220, 230), a);
        fx_post_rect(fb, W / 2 + 2, 146, 3, 9, fx_rgb(220, 220, 230), a);
    }
#if SHOW_PROGRESS_LINE
    {
        uint32_t pos = post->song_ms > SONG_TOTAL_DURATION_MS ? SONG_TOTAL_DURATION_MS : post->song_ms;
        int pw = (int)((uint64_t)pos * W / SONG_TOTAL_DURATION_MS);
        fx_post_rect(fb, 0, H - 1, pw, 1, fx_rgb(90, 90, 110), 255);
    }
#endif
}

void fx_render(uint16_t *fb, uint32_t song_ms, size_t li, uint32_t tick, bool is_playing)
{
    fx_post_t post;
    fx_draw_frame(0, fb, song_ms, li, tick, is_playing, &post);
    fx_finish_frame(0, fb, &post);
}
