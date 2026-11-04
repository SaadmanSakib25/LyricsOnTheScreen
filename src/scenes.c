/*
 * Animated illustrations for the lyrics: original neon line-art scenes
 * drawn with the fx_draw primitives. World coordinates match the screen
 * (128x160); illustration scenes use roughly y = 0..100 and leave the
 * lower third for the lyric text.
 */
#include "scenes.h"

#include <math.h>

#include "fx_draw.h"
#include "lyric_types.h"

#define W FXW
#define H FXH

static const uint16_t BLACK = 0x0000;
static const uint16_t WHITE = 0xFFFF;

/* ------------------------------------------------------------------ */
/* Shared helpers                                                     */
/* ------------------------------------------------------------------ */
static float ease_bounce(float k)
{
    const float n = 7.5625f, d = 2.75f;
    if (k < 1.0f / d) return n * k * k;
    if (k < 2.0f / d) { k -= 1.5f / d; return n * k * k + 0.75f; }
    if (k < 2.5f / d) { k -= 2.25f / d; return n * k * k + 0.9375f; }
    k -= 2.625f / d;
    return n * k * k + 0.984375f;
}

static void stars_bg(uint32_t seed, int n, float t, float ymax, float bright)
{
    for (int i = 0; i < n; i++) {
        uint32_t h = seed * 977u + (uint32_t)i * 3u;
        float d = fx_hashf(h + 2);
        float x = fmodf(fx_hashf(h) * W - t * 0.002f * d + W * 16.0f, (float)W);
        float y = fx_hashf(h + 1) * ymax;
        float tw = 0.55f + 0.45f * sinf(t * 0.003f * (1.0f + d * 2.0f) + i * 1.7f);
        int a = (int)((50.0f + 180.0f * d) * tw * bright);
        if (d > 0.88f) {
            fx_disc(x, y, 0.9f, fx_rgb(255, 250, 235), a, 230);
        } else {
            fx_disc(x, y, 0.5f, fx_rgb(190, 205, 255), a, 0);
        }
    }
}

static void sparkle(float x, float y, float s, uint16_t c, int a)
{
    if (s <= 0.2f || a <= 0) return;
    fx_line(x - s, y, x + s, y, 0.7f, c, a, 255);
    fx_line(x, y - s, x, y + s, 0.7f, c, a, 255);
    fx_disc(x, y, 0.8f, WHITE, a, 255);
}

static void heart_shape(float cx, float cy, float s, uint16_t c, int a, int glow)
{
    float p[56];
    for (int i = 0; i < 28; i++) {
        float u = i * FX_TAU / 28.0f;
        float su = sinf(u);
        p[2 * i] = cx + 16.0f * su * su * su * s;
        p[2 * i + 1] = cy - (13.0f * cosf(u) - 5.0f * cosf(2 * u) - 2.0f * cosf(3 * u) - cosf(4 * u)) * s;
    }
    fx_poly(p, 28, c, a, glow);
}

static void heart_point(float u, float s, float cx, float cy, float *x, float *y)
{
    float su = sinf(u);
    *x = cx + 16.0f * su * su * su * s;
    *y = cy - (13.0f * cosf(u) - 5.0f * cosf(2 * u) - 2.0f * cosf(3 * u) - cosf(4 * u)) * s;
}

/* ------------------------------------------------------------------ */
/* Scenes                                                             */
/* ------------------------------------------------------------------ */
static void sc_stars(const scene_ctx_t *c)
{
    float t = c->t;
    fx_vgradient(0, 130, fx_rgb(12, 8, 40), BLACK, 255);
    stars_bg(1, 70, t, 160, 1.0f);

    /* shooting star every 2.6 s */
    const float P = 2600.0f;
    int k = (int)(t / P);
    float u = fmodf(t, P) / 700.0f;
    if (u < 1.0f) {
        float sx = 8.0f + fx_hashf(k * 7 + 1) * 70.0f, sy = 6.0f + fx_hashf(k * 7 + 2) * 40.0f;
        float hx = sx + u * 80.0f, hy = sy + u * 34.0f;
        for (int j = 0; j < 5; j++) {
            float f0 = j / 5.0f, f1 = (j + 1) / 5.0f;
            int a = (int)(255.0f * (1.0f - f0) * (1.0f - u * 0.6f));
            fx_line(hx - 30.0f * f0, hy - 12.75f * f0, hx - 30.0f * f1, hy - 12.75f * f1,
                    1.4f - f0, fx_rgb(220, 230, 255), a, 255);
        }
    }
}

static void sc_book(const scene_ctx_t *c)
{
    float t = c->t;
    fx_radial(64, 56, 64, fx_rgb(255, 160, 50), 80);

    float cover[] = {6, 54, 64, 63, 122, 54, 122, 85, 64, 95, 6, 85};
    fx_poly(cover, 6, fx_rgb(110, 45, 25), 255, 60);

    const uint16_t paper = fx_rgb(240, 225, 190);
    float L[] = {10, 49, 62, 58, 62, 89, 10, 80};
    float R[] = {118, 49, 66, 58, 66, 89, 118, 80};
    fx_poly(L, 4, paper, 255, 50);
    fx_poly(R, 4, paper, 255, 50);
    fx_line(64, 58, 64, 90, 1.2f, fx_rgb(140, 110, 80), 255, 0);

    /* lines of text on both pages */
    const uint16_t ink = fx_rgb(150, 125, 95);
    for (int j = 0; j < 5; j++) {
        float off = 6.0f + j * 4.6f;
        fx_line(16, 50 + 0.96f + off, 57, 50 + 7.4f + off, 0.7f, ink, 200, 0);
        fx_line(112, 50 + 0.96f + off, 71, 50 + 7.4f + off, 0.7f, ink, 200, 0);
    }

    /* page flipping right -> left */
    float ph = fmodf(t, 1500.0f) / 1000.0f;
    if (ph < 1.0f) {
        float th = fx_ease_inout(ph) * FX_PI;
        float ex = 64.0f + 54.0f * cosf(th);
        float lift = 14.0f * sinf(th);
        float pg[] = {64, 58, ex, 49 - lift, ex, 80 - lift, 64, 89};
        uint16_t shade = fx_mix(fx_rgb(170, 150, 120), paper, fabsf(cosf(th)));
        fx_poly(pg, 4, shade, 255, 80);
    }

    /* magic dust rising off the pages */
    for (int i = 0; i < 24; i++) {
        const float per = 2200.0f;
        float p = fmodf(t + fx_hashf(i) * per, per) / per;
        float x = 22.0f + fx_hashf(i + 40) * 84.0f + sinf(p * 7.0f + i) * 5.0f;
        float y = 64.0f - p * 66.0f;
        int a = (int)(sinf(p * FX_PI) * 255.0f);
        uint16_t col = fx_hsv(35.0f + fx_hashf(i + 80) * 25.0f, 0.55f, 1.0f);
        if (i % 6 == 0) sparkle(x, y, 2.5f * sinf(p * FX_PI), col, a);
        else fx_disc(x, y, 0.6f + fx_hashf(i + 9) * 0.8f, col, a, 255);
    }
}

static void sc_temple(const scene_ctx_t *c)
{
    float t = c->t;
    fx_vgradient(0, 100, fx_rgb(6, 8, 32), fx_rgb(24, 16, 44), 255);
    stars_bg(3, 30, t, 40, 0.8f);
    fx_radial(64, 52, 72, fx_rgb(110, 130, 255), 70);

    const uint16_t m = fx_rgb(215, 228, 255);
    const int gl = 170;

    float k = fx_ease_out(fx_prog(t, 0, 500));
    fx_line(64 - 56 * k, 97, 64 + 56 * k, 97, 1.2f, m, 255, gl);
    fx_line(64 - 52 * k, 93, 64 + 52 * k, 93, 1.2f, m, 255, gl);
    fx_line(64 - 48 * k, 89, 64 + 48 * k, 89, 1.2f, m, 255, gl);

    for (int i = 0; i < 5; i++) {
        float x = 26.0f + i * 19.0f;
        float kc = fx_ease_out(fx_prog(t, 300.0f + i * 110.0f, 500.0f));
        if (kc <= 0) continue;
        float ytop = 89.0f - 37.0f * kc;
        fx_line(x - 3, 89, x - 3, ytop, 1.0f, m, 255, gl);
        fx_line(x + 3, 89, x + 3, ytop, 1.0f, m, 255, gl);
        fx_line(x, 88, x, ytop + 1, 0.5f, m, 110, 40);
        if (kc >= 1.0f) fx_rect(x - 5, 50, 10, 2.2f, m, 255, gl);
    }

    float ke = fx_ease_out(fx_prog(t, 1000, 450));
    fx_line(64 - 50 * ke, 48, 64 + 50 * ke, 48, 1.2f, m, 255, gl);
    fx_line(64 - 50 * ke, 44, 64 + 50 * ke, 44, 1.2f, m, 255, gl);

    float kp = fx_ease_out(fx_prog(t, 1350, 600));
    if (kp > 0) {
        fx_line(14, 44, fx_lerp(14, 64, kp), fx_lerp(44, 20, kp), 1.3f, m, 255, gl);
        fx_line(114, 44, fx_lerp(114, 64, kp), fx_lerp(44, 20, kp), 1.3f, m, 255, gl);
    }
    if (t > 1950) {
        float s = 3.0f + 2.0f * sinf(t * 0.004f);
        sparkle(64, 20, s, fx_rgb(255, 230, 160), 255);
    }
}

static void sc_shield(const scene_ctx_t *c)
{
    float t = c->t;
    fx_radial(64, 52, 72, fx_rgb(255, 160, 30), 80);

    /* spear behind */
    float ks = fx_ease_out(fx_prog(t, 150, 500));
    if (ks > 0) {
        float ex = 18 + 92 * ks, ey = 100 - 96 * ks;
        fx_line(18, 100, ex, ey, 1.8f, fx_rgb(200, 150, 80), 255, 120);
        if (ks >= 1.0f) {
            float tip[] = {104, 10, 114, 0, 112, 14};
            fx_poly(tip, 3, fx_rgb(240, 220, 170), 255, 200);
        }
    }

    float e = fx_ease_back(fx_prog(t, 0, 700));
    float R = 32.0f * e;
    if (R < 1.0f) return;
    fx_disc(64, 52, R, fx_rgb(150, 92, 18), 255, 80);
    fx_ring(64, 52, R - 1.3f, 2.6f, fx_rgb(255, 212, 90), 255, 230);
    fx_ring(64, 52, R * 0.68f, 1.2f, fx_rgb(255, 200, 80), 255, 160);
    fx_ring(64, 52, R * 0.36f, 1.0f, fx_rgb(255, 200, 80), 255, 160);

    float star[32];
    for (int i = 0; i < 16; i++) {
        float a = i * FX_PI / 8.0f - FX_PI / 2.0f + t * 0.0003f;
        float r = (i % 2 == 0) ? R * 0.3f : R * 0.12f;
        star[2 * i] = 64 + cosf(a) * r;
        star[2 * i + 1] = 52 + sinf(a) * r;
    }
    fx_poly(star, 16, fx_rgb(255, 232, 140), 255, 220);

    /* shine sweeping across the shield */
    if (t > 700) {
        float ph = fmodf(t - 700, 1800) / 600.0f;
        if (ph < 1.0f) {
            float off = -R * 1.2f + ph * R * 2.4f;
            float hc2 = R * R - off * off;
            if (hc2 > 0) {
                float hc = sqrtf(hc2);
                float cx = 64 + off * 0.7071f, cy = 52 + off * 0.7071f;
                fx_line(cx - hc * 0.7071f, cy + hc * 0.7071f, cx + hc * 0.7071f, cy - hc * 0.7071f,
                        5.0f, WHITE, 90, 255);
                fx_line(cx - hc * 0.7071f, cy + hc * 0.7071f, cx + hc * 0.7071f, cy - hc * 0.7071f,
                        1.6f, WHITE, 220, 255);
            }
        }
    }

    for (int i = 0; i < 6; i++) {
        float tt = t + i * 233.0f;
        int cyc = (int)(tt / 1400.0f);
        float ph = fmodf(tt, 1400.0f) / 1400.0f;
        float a = fx_hashf(i + cyc * 13) * FX_TAU;
        float rr = R + 5.0f + fx_hashf(i * 7 + cyc) * 12.0f;
        sparkle(64 + cosf(a) * rr, 52 + sinf(a) * rr, 4.0f * sinf(ph * FX_PI), fx_rgb(255, 230, 150), 255);
    }
}

static void bolt(int k, int a)
{
    float x = 30.0f + fx_hashf(k * 31) * 68.0f, y = 22.0f;
    for (int j = 0; j < 8; j++) {
        float nx = x + (fx_hashf(k * 31 + j + 1) - 0.5f) * 18.0f, ny = y + 10.0f;
        fx_line(x, y, nx, ny, 2.4f, fx_rgb(170, 190, 255), a, 255);
        fx_line(x, y, nx, ny, 1.0f, WHITE, a, 255);
        if (j == 3) { /* branch */
            float bx = nx, by = ny, dir = fx_hashf(k * 17) > 0.5f ? 1.0f : -1.0f;
            for (int b = 0; b < 3; b++) {
                float bnx = bx + dir * (5.0f + fx_hashf(k * 13 + b) * 5.0f), bny = by + 7.0f;
                fx_line(bx, by, bnx, bny, 1.0f, fx_rgb(190, 205, 255), a * 2 / 3, 255);
                bx = bnx;
                by = bny;
            }
        }
        x = nx;
        y = ny;
    }
}

static void sc_lightning(const scene_ctx_t *c)
{
    float t = c->t;
    fx_vgradient(0, 100, fx_rgb(14, 16, 36), fx_rgb(4, 4, 12), 255);

    const float P = 1150.0f, T0 = 150.0f;
    int k = t > T0 ? (int)((t - T0) / P) : -1;
    float lt = t > T0 ? t - T0 - k * P : 1e9f;
    float flash = expf(-lt / 110.0f);
    if (lt > 55.0f && lt < 95.0f) flash *= 0.35f; /* flicker */
    if (flash > 0.01f) fx_vgradient(0, 100, fx_rgb(160, 170, 255), fx_rgb(40, 40, 90), (int)(flash * 170.0f));

    for (int i = 0; i < 26; i++) {
        float y = fmodf(fx_hashf(i + 50) * 120.0f + t * 0.18f, 120.0f) - 10.0f;
        float x = fx_hashf(i) * 150.0f - y * 0.25f;
        fx_line(x, y, x - 2, y + 7, 0.6f, fx_rgb(110, 130, 200), 110, 0);
    }

    if (k >= 0 && lt < 260.0f) bolt(k, (int)(255.0f * (1.0f - lt / 260.0f)));

    for (int i = 0; i < 9; i++) {
        float cx = fx_hashf(i + 200) * 140.0f - 6.0f + sinf(t * 0.0005f + i) * 3.0f;
        float cy = 8.0f + fx_hashf(i + 300) * 16.0f;
        float r = 12.0f + fx_hashf(i + 400) * 10.0f;
        uint16_t col = fx_mix(fx_rgb(32, 34, 58), fx_rgb(150, 150, 210), flash);
        fx_disc(cx, cy, r, col, 255, 0);
    }
}

static void sc_web(const scene_ctx_t *c)
{
    float t = c->t;
    fx_vgradient(0, 100, fx_rgb(8, 12, 24), BLACK, 255);
    fx_radial(64, 46, 64, fx_rgb(80, 120, 210), 50);

    enum { NS = 10, NR = 8 };
    const float cx = 64, cy = 46;
    const uint16_t silk = fx_rgb(205, 215, 235);
    float ang[NS], len[NS];
    for (int i = 0; i < NS; i++) {
        ang[i] = i * FX_TAU / NS + 0.15f + (fx_hashf(i) - 0.5f) * 0.2f;
        len[i] = 46.0f + fx_hashf(i + 20) * 8.0f;
        float ks = fx_ease_out(fx_prog(t, i * 40.0f, 500.0f));
        fx_line(cx, cy, cx + cosf(ang[i]) * len[i] * ks, cy + sinf(ang[i]) * len[i] * ks, 0.7f, silk, 220, 90);
    }

    int nseg = (int)((t - 500.0f) / 22.0f);
    if (nseg > NS * NR) nseg = NS * NR;
    for (int s = 0; s < nseg; s++) {
        int ring = s / NS, i = s % NS, j = (i + 1) % NS;
        float r = 5.0f + ring * 5.2f;
        fx_line(cx + cosf(ang[i]) * r, cy + sinf(ang[i]) * r,
                cx + cosf(ang[j]) * r, cy + sinf(ang[j]) * r, 0.6f, silk, 200, 70);
    }

    /* dew drops catching the light */
    for (int d = 0; d < 7; d++) {
        int i = (int)(fx_hashf(d + 90) * NS);
        float r = 5.0f + (int)(fx_hashf(d + 95) * NR) * 5.2f;
        float tw = 0.5f + 0.5f * sinf(t * 0.005f + d * 2.1f);
        if (t > 500.0f + (r / 5.2f) * NS * 22.0f) {
            fx_disc(cx + cosf(ang[i]) * r, cy + sinf(ang[i]) * r, 1.0f, WHITE, (int)(255 * tw), 255);
        }
    }

    /* spider lowering itself on a thread */
    float sy = 6.0f + 36.0f * fx_ease_out(fx_prog(t, 300, 1400)) + sinf(t * 0.004f) * 2.0f;
    const float sx = 100.0f;
    fx_line(sx, -2, sx, sy, 0.5f, silk, 170, 40);
    const uint16_t leg = fx_rgb(90, 90, 115);
    for (int side = -1; side <= 1; side += 2) {
        for (int l = 0; l < 4; l++) {
            float wig = sinf(t * 0.012f + l * 1.3f) * 0.7f;
            float bx = sx + side * 1.5f, by = sy + 1.5f + l * 1.1f;
            float kx = sx + side * 6.0f, ky = sy - 2.0f + l * 2.4f + wig;
            float fx = sx + side * 8.5f, fy = sy + 3.5f + l * 2.6f;
            fx_line(bx, by, kx, ky, 0.8f, leg, 255, 60);
            fx_line(kx, ky, fx, fy, 0.7f, leg, 255, 60);
        }
    }
    fx_disc(sx, sy + 4.0f, 3.4f, fx_rgb(25, 22, 32), 255, 0);
    fx_ring(sx, sy + 4.0f, 3.4f, 0.8f, fx_rgb(200, 50, 70), 255, 200);
    fx_disc(sx, sy, 2.0f, fx_rgb(30, 26, 38), 255, 0);
    fx_disc(sx - 0.8f, sy - 0.5f, 0.5f, fx_rgb(255, 60, 60), 255, 255);
    fx_disc(sx + 0.8f, sy - 0.5f, 0.5f, fx_rgb(255, 60, 60), 255, 255);
}

static void sc_city(const scene_ctx_t *c)
{
    float t = c->t;
    const float TPOW = 1150.0f;
    if (t > TPOW && t < TPOW + 260.0f) {
        float k = 1.0f - (t - TPOW) / 260.0f;
        fx_set_shake(sinf(t * 1.7f) * 2.5f * k, cosf(t * 2.3f) * 2.0f * k);
    }

    fx_vgradient(0, 100, fx_rgb(8, 12, 38), fx_rgb(34, 22, 56), 255);
    stars_bg(5, 25, t, 50, 0.7f);
    fx_radial(94, 26, 42, fx_rgb(200, 200, 160), 80);
    fx_disc(94, 26, 13, fx_rgb(240, 236, 205), 255, 180);
    fx_disc(90, 22, 2.5f, fx_rgb(205, 200, 175), 255, 0);
    fx_disc(98, 30, 3.0f, fx_rgb(205, 200, 175), 255, 0);

    /* bats crossing the moon */
    for (int b = 0; b < 4; b++) {
        float bx = 140.0f - fmodf(t * 0.03f + b * 37.0f, 180.0f);
        float by = 20.0f + b * 5.0f + sinf(t * 0.004f + b) * 3.0f;
        float flap = sinf(t * 0.03f + b * 1.3f) * 3.0f;
        float s = 4.0f + (b % 2);
        for (int side = -1; side <= 1; side += 2) {
            fx_line(bx, by, bx + side * s, by - flap, 1.3f, BLACK, 255, 0);
            fx_line(bx + side * s, by - flap, bx + side * s * 1.9f, by + 1.0f - flap * 0.3f, 1.0f, BLACK, 255, 0);
        }
        fx_disc(bx, by, 1.3f, BLACK, 255, 0);
    }

    /* skyline with lit windows */
    for (int i = 0; i < 10; i++) {
        float x = i * 13.0f - 3.0f + fx_hashf(i + 500) * 3.0f;
        float w = 11.0f + fx_hashf(i + 510) * 6.0f;
        float h = 26.0f + fx_hashf(i + 520) * 36.0f;
        float top = 100.0f - h;
        fx_rect(x, top, w, h + 2, fx_rgb(10, 10, 24), 255, 0);
        fx_line(x, top, x + w, top, 0.8f, fx_rgb(70, 80, 150), 200, 60);
        int id = 0;
        for (float wy = top + 4; wy < 97; wy += 5) {
            for (float wx = x + 2; wx < x + w - 2; wx += 4) {
                uint32_t hsh = (uint32_t)(i * 1000 + id++);
                bool lit = fx_hashf(hsh) > 0.55f;
                if (fx_hashf(hsh + 7) > 0.96f) lit = sinf(t * 0.004f + id) > 0;
                if (lit) fx_rect(wx, wy, 1.6f, 2.0f, fx_rgb(255, 200, 110), 220, 90);
            }
        }
    }

    /* comic POW! on "fists" */
    if (t > TPOW) {
        float e = fx_ease_back(fx_prog(t, TPOW, 260));
        float burst[28];
        for (int i = 0; i < 14; i++) {
            float a = i * FX_TAU / 14.0f + 0.2f;
            float r = ((i % 2) ? 11.0f : 21.0f) * e;
            burst[2 * i] = 38 + cosf(a) * r;
            burst[2 * i + 1] = 40 + sinf(a) * r;
        }
        fx_poly(burst, 14, fx_rgb(255, 222, 40), 255, 170);
        for (int i = 0; i < 14; i++) {
            int j = (i + 1) % 14;
            fx_line(burst[2 * i], burst[2 * i + 1], burst[2 * j], burst[2 * j + 1], 1.4f, fx_rgb(230, 40, 40), 255, 120);
        }
        for (int oy = -1; oy <= 1; oy++) {
            for (int ox = -1; ox <= 1; ox++) {
                if (ox || oy) fx_text("POW!", 38 + ox, 41 + oy, 1.5f * e, fx_rgb(60, 0, 10), 255, 0);
            }
        }
        fx_text("POW!", 38, 41, 1.5f * e, fx_rgb(235, 25, 45), 255, 0);
    }
}

static void sc_list(const scene_ctx_t *c)
{
    float t = c->t;
    const float TX = 1750.0f;
    if (t > TX && t < TX + 220.0f) {
        float k = 1.0f - (t - TX) / 220.0f;
        fx_set_shake(sinf(t * 1.9f) * 2.0f * k, 0);
    }
    fx_radial(64, 52, 64, fx_rgb(120, 120, 170), 50);

    float e = fx_ease_out(fx_prog(t, 0, 450));
    float yo = (1.0f - e) * 24.0f;
    int a = (int)(255 * e);
    const uint16_t edge = fx_rgb(220, 222, 238);
    fx_rect(32, 10 + yo, 64, 88, fx_rgb(28, 28, 44), a, 0);
    fx_line(32, 10 + yo, 96, 10 + yo, 1.0f, edge, a, 120);
    fx_line(96, 10 + yo, 96, 98 + yo, 1.0f, edge, a, 120);
    fx_line(96, 98 + yo, 32, 98 + yo, 1.0f, edge, a, 120);
    fx_line(32, 98 + yo, 32, 10 + yo, 1.0f, edge, a, 120);
    fx_rect(54, 6 + yo, 20, 6, fx_rgb(170, 170, 195), a, 80);

    const uint16_t green = fx_rgb(80, 255, 140);
    for (int i = 0; i < 6; i++) {
        float y = 24.0f + i * 12.0f + yo;
        fx_line(38, y - 3, 44, y - 3, 0.8f, edge, a, 60);
        fx_line(44, y - 3, 44, y + 3, 0.8f, edge, a, 60);
        fx_line(44, y + 3, 38, y + 3, 0.8f, edge, a, 60);
        fx_line(38, y + 3, 38, y - 3, 0.8f, edge, a, 60);
        if (i < 5) {
            float kb = fx_prog(t, 250.0f + i * 230.0f, 200.0f);
            float len = 22.0f + fx_hashf(i + 3) * 20.0f;
            if (kb > 0) fx_line(50, y, 50 + len * kb, y, 1.4f, fx_rgb(150, 150, 175), 220, 40);
            float kc = fx_prog(t, 420.0f + i * 230.0f, 140.0f);
            if (kc > 0) {
                float k1 = fx_clamp01(kc * 2.0f), k2 = fx_clamp01(kc * 2.0f - 1.0f);
                fx_line(39, y, 39 + 2 * k1, y + 2 * k1, 1.4f, green, 255, 220);
                if (k2 > 0) fx_line(41, y + 2, 41 + 5 * k2, y + 2 - 7 * k2, 1.4f, green, 255, 220);
            }
        } else if (t > 1400.0f) {
            fx_text("me?", 60, y, 1.0f, fx_rgb(200, 200, 220), (int)(255 * fx_prog(t, 1400, 250)), 60);
        }
    }

    if (t > TX) {
        float k = fx_ease_out(fx_prog(t, TX, 160));
        float y = 24.0f + 5 * 12.0f + yo;
        const uint16_t red = fx_rgb(255, 50, 60);
        fx_line(34, y - 7, fx_lerp(34, 94, k), fx_lerp(y - 7, y + 7, k), 2.4f, red, 255, 255);
        fx_line(34, y + 7, fx_lerp(34, 94, k), fx_lerp(y + 7, y - 7, k), 2.4f, red, 255, 255);
    }
}

static void sc_road(const scene_ctx_t *c)
{
    float t = c->t;
    const float hz = 64;
    fx_vgradient(0, (int)hz, fx_rgb(40, 12, 72), fx_rgb(255, 110, 70), 255);
    fx_radial(64, hz, 56, fx_rgb(255, 140, 60), 130);
    fx_disc(64, hz - 2, 15, fx_rgb(255, 205, 115), 255, 200);

    float ml[] = {0, hz, 12, hz - 8, 24, hz - 3, 38, hz - 11, 52, hz};
    float mr[] = {76, hz, 90, hz - 9, 104, hz - 4, 116, hz - 12, 128, hz - 5, 128, hz};
    fx_poly(ml, 5, fx_rgb(50, 16, 60), 255, 0);
    fx_poly(mr, 6, fx_rgb(50, 16, 60), 255, 0);

    fx_rect(0, hz, W, 42, fx_rgb(16, 8, 26), 255, 0);
    fx_line(0, hz, W, hz, 0.8f, fx_rgb(255, 150, 120), 220, 200);

    float road[] = {61, hz, 67, hz, 124, 106, 4, 106};
    fx_poly(road, 4, fx_rgb(36, 30, 50), 255, 0);
    fx_line(61, hz, 4, 106, 1.0f, fx_rgb(255, 200, 200), 190, 90);
    fx_line(67, hz, 124, 106, 1.0f, fx_rgb(255, 200, 200), 190, 90);

    for (int i = 0; i < 7; i++) {
        float u = fmodf(i / 7.0f + t * 0.00045f, 1.0f);
        float u1 = u + 0.05f;
        float y0 = hz + 42.0f * u * u, y1 = hz + 42.0f * u1 * u1;
        fx_line(64, y0, 64, y1, 0.4f + 2.0f * u, fx_rgb(255, 220, 120), (int)(255 * fx_clamp01(u * 4)), 120);
    }

    /* map pin drops in and bounces */
    float k = ease_bounce(fx_prog(t, 200, 800));
    float py = fx_lerp(-20, 30, k);
    float tri[] = {58.6f, py + 2.6f, 69.4f, py + 2.6f, 64, py + 12};
    const uint16_t pin = fx_rgb(255, 60, 100);
    fx_poly(tri, 3, pin, 255, 200);
    fx_disc(64, py, 6, pin, 255, 200);
    fx_disc(64, py, 2.3f, fx_rgb(60, 14, 50), 255, 0);
}

static void die(float x, float y, float s, float ang, int face, int a)
{
    float ca = cosf(ang), sa = sinf(ang);
    float q = s - 1.8f;
    float lp[16] = {-s, -q, -q, -s, q, -s, s, -q, s, q, q, s, -q, s, -s, q};
    float p[16];
    for (int i = 0; i < 8; i++) {
        p[2 * i] = x + lp[2 * i] * ca - lp[2 * i + 1] * sa;
        p[2 * i + 1] = y + lp[2 * i] * sa + lp[2 * i + 1] * ca;
    }
    fx_poly(p, 8, fx_rgb(245, 245, 252), a, 70);

    static const signed char PIPS[7][6][2] = {
        {{0}},
        {{0, 0}},
        {{-1, -1}, {1, 1}},
        {{-1, -1}, {0, 0}, {1, 1}},
        {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}},
        {{-1, -1}, {1, -1}, {0, 0}, {-1, 1}, {1, 1}},
        {{-1, -1}, {-1, 0}, {-1, 1}, {1, -1}, {1, 0}, {1, 1}},
    };
    const float sp = s * 0.5f;
    for (int i = 0; i < face && i < 6; i++) {
        float lx = PIPS[face][i][0] * sp, ly = PIPS[face][i][1] * sp;
        fx_disc(x + lx * ca - ly * sa, y + lx * sa + ly * ca, s * 0.18f, fx_rgb(225, 25, 60), a, 200);
    }
}

static void sc_dice(const scene_ctx_t *c)
{
    float t = c->t;
    fx_radial(64, 72, 64, fx_rgb(220, 40, 90), 60);
    fx_line(6, 92, 122, 92, 0.8f, fx_rgb(140, 60, 100), 170, 90);

    for (int i = 0; i < 2; i++) {
        float start = i * 120.0f, dur = 1300.0f;
        float k = fx_prog(t, start, dur);
        float e = fx_ease_out(k);
        const float s = 10.0f;
        float x = fx_lerp(-16.0f - i * 12.0f, 46.0f + i * 36.0f, e);
        float bounce = fabsf(sinf(e * FX_PI * 3.0f)) * 28.0f * (1.0f - e);
        float y = 92.0f - s * 1.15f - bounce;
        float ang = (1.0f - e) * FX_PI * 5.0f * (i ? -1.0f : 1.0f) + (i ? 0.12f : -0.08f);
        int face = k < 1.0f ? 1 + (int)(fx_hash((uint32_t)(t / 80.0f) + i * 7) % 6) : 6;
        die(x, y, s, ang, face, 255);

        float lt = t - start - dur;
        if (lt > 0 && lt < 350.0f) {
            float r = 4.0f + lt * 0.06f;
            fx_line(x - r, 92, x + r, 92, 1.5f, fx_rgb(255, 90, 130), (int)(255 * (1 - lt / 350.0f)), 255);
        }
    }
}

static void sc_hero(const scene_ctx_t *c)
{
    float t = c->t;
    float rise = fx_ease_out(fx_prog(t, 0, 5000));
    fx_vgradient(0, 92, fx_rgb(30, 10, 42), fx_rgb(255, 120, 50), 255);

    float sy = 100.0f - 36.0f * rise;
    fx_radial(64, sy, 84, fx_rgb(255, 150, 60), 150);
    float rot = t * 0.00008f;
    for (int i = 0; i < 14; i++) {
        float a0 = rot + i * FX_TAU / 14.0f, a1 = a0 + FX_TAU / 28.0f;
        float tri[] = {64, sy, 64 + cosf(a0) * 130, sy + sinf(a0) * 130, 64 + cosf(a1) * 130, sy + sinf(a1) * 130};
        fx_poly(tri, 3, fx_rgb(255, 195, 120), (int)(35 + 45 * rise), 0);
    }
    fx_disc(64, sy, 14, fx_rgb(255, 232, 165), 255, 255);

    /* distant city */
    for (int i = 0; i < 12; i++) {
        float x = i * 11.0f - 2.0f, h = 6.0f + fx_hashf(i + 700) * 12.0f;
        fx_rect(x, 90 - h, 10, h, fx_rgb(70, 22, 40), 255, 0);
    }
    fx_rect(0, 88, W, H - 88 + 4, BLACK, 255, 0);
    fx_rect(30, 85, 68, 4, BLACK, 255, 0);

    /* caped silhouette, cape blowing in the wind */
    float w = t * 0.006f;
    float cape[] = {
        70, 58, 68, 72, 66, 84,
        52 + sinf(w) * 2.0f, 84,
        42 + sinf(w + 1.0f) * 4.0f, 76 + sinf(w + 0.3f) * 2.0f,
        35 + sinf(w + 2.0f) * 5.0f, 66 + sinf(w + 0.5f) * 3.0f,
        46, 60, 58, 58,
    };
    fx_poly(cape, 8, fx_rgb(150, 14, 28), 255, 0);

    fx_disc(64, 52, 4.2f, BLACK, 255, 0);
    float torso[] = {58.5f, 57, 69.5f, 57, 67.5f, 72, 60.5f, 72};
    fx_poly(torso, 4, BLACK, 255, 0);
    float legl[] = {60.5f, 71, 63.8f, 71, 62, 85.5f, 57.5f, 85.5f};
    float legr[] = {64.2f, 71, 67.5f, 71, 70.5f, 85.5f, 66, 85.5f};
    fx_poly(legl, 4, BLACK, 255, 0);
    fx_poly(legr, 4, BLACK, 255, 0);
    fx_line(59, 58.5f, 54, 65, 2.6f, BLACK, 255, 0);
    fx_line(54, 65, 60, 71, 2.4f, BLACK, 255, 0);
    fx_line(69, 58.5f, 74, 65, 2.6f, BLACK, 255, 0);
    fx_line(74, 65, 68, 71, 2.4f, BLACK, 255, 0);

    /* embers drifting up */
    for (int i = 0; i < 12; i++) {
        float per = 2400.0f;
        float p = fmodf(t + fx_hashf(i + 33) * per, per) / per;
        float x = fx_hashf(i + 44) * W + sinf(p * 5 + i) * 4;
        float y = 92 - p * 90;
        fx_disc(x, y, 0.7f, fx_rgb(255, 180, 90), (int)(sinf(p * FX_PI) * 220), 255);
    }
}

static void sc_castle(const scene_ctx_t *c)
{
    float t = c->t;
    fx_vgradient(0, 100, fx_rgb(30, 10, 64), fx_rgb(90, 34, 100), 255);
    stars_bg(7, 30, t, 60, 0.9f);
    fx_radial(64, 92, 72, fx_rgb(255, 110, 200), 70);

    /* fireworks */
    if (t > 1200.0f) {
        const float P = 900.0f;
        int k = (int)((t - 1200.0f) / P);
        float lt = t - 1200.0f - k * P;
        float bx = 18 + fx_hashf(k * 5) * 92, by = 12 + fx_hashf(k * 5 + 1) * 24;
        float r = 17.0f * fx_ease_out(fx_clamp01(lt / 700.0f));
        int a = (int)(255.0f * (1.0f - lt / P));
        uint16_t col = fx_hsv(fx_hashf(k * 5 + 2) * 360.0f, 0.55f, 1.0f);
        for (int j = 0; j < 14; j++) {
            float an = j * FX_TAU / 14.0f;
            float px = bx + cosf(an) * r, py = by + sinf(an) * r + lt * lt * 0.000006f;
            fx_line(bx + cosf(an) * r * 0.6f, by + sinf(an) * r * 0.6f, px, py, 0.6f, col, a / 2, 200);
            fx_disc(px, py, 0.9f, col, a, 255);
        }
    }

    const uint16_t sil = fx_rgb(22, 8, 40), roof = fx_rgb(70, 22, 92), edge = fx_rgb(235, 160, 255);
    fx_rect(28, 56, 12, 46, sil, 255, 0);
    fx_rect(88, 56, 12, 46, sil, 255, 0);
    fx_rect(42, 66, 44, 36, sil, 255, 0);
    fx_rect(56, 42, 16, 26, sil, 255, 0);
    for (int i = 0; i < 7; i++) fx_rect(42 + i * 6.6f, 62, 3.5f, 4.5f, sil, 255, 0);

    float rl[] = {26, 56, 34, 38, 42, 56};
    float rr[] = {86, 56, 94, 38, 102, 56};
    float rc[] = {54, 42, 64, 20, 74, 42};
    fx_poly(rl, 3, roof, 255, 120);
    fx_poly(rr, 3, roof, 255, 120);
    fx_poly(rc, 3, roof, 255, 120);
    const float *roofs[3] = {rl, rr, rc};
    for (int r = 0; r < 3; r++) {
        const float *p = roofs[r];
        fx_line(p[0], p[1], p[2], p[3], 0.9f, edge, 255, 170);
        fx_line(p[2], p[3], p[4], p[5], 0.9f, edge, 255, 170);
        float fw = sinf(t * 0.01f + r) * 1.5f;
        float flag[] = {p[2], p[3] - 6, p[2] + 6, p[3] - 4.5f + fw, p[2], p[3] - 3};
        fx_line(p[2], p[3], p[2], p[3] - 6, 0.6f, edge, 255, 60);
        fx_poly(flag, 3, fx_rgb(255, 90, 170), 255, 200);
    }

    fx_disc(64, 88, 6, fx_rgb(255, 170, 80), 255, 220);
    fx_rect(58, 88, 12, 14, fx_rgb(255, 170, 80), 255, 220);
    const float win[][2] = {{32, 64}, {92, 64}, {32, 76}, {92, 76}, {61, 50}, {48, 74}, {78, 74}};
    for (int i = 0; i < 7; i++) {
        int a = 170 + (int)(85 * sinf(t * 0.005f + i * 1.9f));
        fx_rect(win[i][0], win[i][1], 3, 4, fx_rgb(255, 210, 120), a, 200);
    }

    /* magic wand trail arcing over the castle */
    float u = fx_ease_inout(fx_prog(t, 100, 1500));
    if (u > 0 && t < 2600.0f) {
        for (int j = 0; j < 16; j++) {
            float uj = u - j * 0.022f;
            if (uj < 0) break;
            float iu = 1 - uj;
            float px = iu * iu * 4 + 2 * iu * uj * 64 + uj * uj * 124;
            float py = iu * iu * 82 + 2 * iu * uj * -8 + uj * uj * 66;
            int a = (int)(255.0f * (1.0f - j / 16.0f) * (1.0f - fx_prog(t, 1800, 800)));
            uint16_t col = fx_hsv(300.0f - j * 12.0f, 0.45f, 1.0f);
            if (j == 0) sparkle(px, py, 4.0f, WHITE, a);
            else fx_disc(px, py + sinf(j * 2.0f + t * 0.01f) * j * 0.15f, 1.5f - j * 0.07f, col, a, 255);
        }
    }
}

static void sc_lighthouse(const scene_ctx_t *c)
{
    float t = c->t;
    fx_vgradient(0, 100, fx_rgb(4, 8, 28), fx_rgb(12, 22, 50), 255);
    stars_bg(9, 25, t, 60, 0.7f);

    const float lx = 90, ly = 34;
    float th = t * 0.0022f + 1.0f;
    float d = cosf(th), face = sinf(th);
    float facing = face > 0 ? face * face * face * face : 0;

    float spread = 4.0f + 6.0f * (1.0f - fabsf(d));
    float beam[] = {lx, ly - 1, lx + d * 140, ly - spread - 6, lx + d * 140, ly + spread + 6, lx, ly + 1};
    if (face < 0) fx_poly(beam, 4, fx_rgb(255, 240, 180), 50, 0);

    fx_rect(0, 78, W, 30, fx_rgb(4, 14, 36), 255, 0);
    for (int row = 0; row < 4; row++) {
        float y = 80.0f + row * 6.0f;
        for (float x = -6; x < W + 6; x += 6) {
            float y0 = y + sinf(x * 0.12f + t * 0.003f + row) * 1.2f;
            float y1 = y + sinf((x + 6) * 0.12f + t * 0.003f + row) * 1.2f;
            fx_line(x, y0, x + 6, y1, 0.8f, fx_rgb(70, 130, 215), 150 - row * 25, 60);
        }
    }

    float rock[] = {68, 86, 76, 74, 104, 72, 114, 86};
    fx_poly(rock, 4, fx_rgb(20, 20, 32), 255, 0);
    float tower[] = {82, 75, 98, 75, 95, 40, 85, 40};
    fx_poly(tower, 4, fx_rgb(225, 225, 235), 255, 30);
    for (int b = 0; b < 3; b++) {
        float y0 = 44.0f + b * 11.0f, y1 = y0 + 6.0f;
        float h0 = 5.0f + (y0 - 40.0f) / 35.0f * 3.0f, h1 = 5.0f + (y1 - 40.0f) / 35.0f * 3.0f;
        float band[] = {90 - h0, y0, 90 + h0, y0, 90 + h1, y1, 90 - h1, y1};
        fx_poly(band, 4, fx_rgb(205, 40, 50), 255, 40);
    }
    fx_rect(84, 30, 12, 10, fx_rgb(40, 40, 52), 255, 0);
    float cap[] = {82, 30, 90, 23, 98, 30};
    fx_poly(cap, 3, fx_rgb(150, 30, 40), 255, 0);
    fx_disc(lx, ly, 3.0f, fx_rgb(255, 240, 170), 255, 255);

    if (face >= 0) fx_poly(beam, 4, fx_rgb(255, 240, 180), (int)(60 + 60 * face), 0);
    fx_radial(lx, ly, 16.0f + 60.0f * facing, fx_rgb(255, 240, 190), (int)(80 + 175 * facing));
    if (facing > 0.2f) {
        for (int i = 0; i < 3; i++) {
            float yy = 82.0f + i * 6.0f;
            fx_line(lx - 6 - i * 3, yy, lx + 6 + i * 3, yy, 1.0f, fx_rgb(255, 240, 190), (int)(200 * facing), 200);
        }
    }
}

static void sc_heart(const scene_ctx_t *c)
{
    float t = c->t;
    float ph = fmodf(t, 860.0f);
    float beat = expf(-ph / 90.0f) + (ph > 200.0f ? 0.6f * expf(-(ph - 200.0f) / 90.0f) : 0.0f);
    fx_radial(64, 50, 74, fx_rgb(255, 40, 110), (int)(70 + 50 * beat));

    for (int i = 0; i < 9; i++) {
        const float per = 2600.0f;
        float p = fmodf(t + fx_hashf(i) * per, per) / per;
        float x = 14.0f + fx_hashf(i + 30) * 100.0f + sinf(p * 6.0f + i) * 6.0f;
        float y = 104.0f - p * 100.0f;
        float sz = 0.3f + fx_hashf(i + 60) * 0.25f;
        heart_shape(x, y, sz, fx_hsv(330.0f + fx_hashf(i) * 30.0f, 0.55f, 1.0f), (int)(sinf(p * FX_PI) * 220), 200);
    }

    fx_ring(64, 48, 30.0f + ph * 0.05f, 1.0f, fx_rgb(255, 90, 150), (int)(140 * expf(-ph / 260.0f)), 220);
    float e = fx_ease_back(fx_prog(t, 0, 500));
    float s = 2.0f * e * (1.0f + 0.08f * beat);
    heart_shape(64, 50, s, fx_rgb(255, 45, 100), 255, 200 + (int)(55 * beat));
    fx_disc(53, 41, 2.6f * e, fx_rgb(255, 175, 205), 170, 0);
}

static void sc_heartform(const scene_ctx_t *c)
{
    float t = c->t;
    float k = fx_ease_inout(fx_prog(t, 0, 1700));
    float beat = c->drop ? c->pulse : expf(-fmodf(t, 860.0f) / 110.0f) * k;
    float s = 2.3f * (1.0f + 0.06f * beat);
    const float cx = 64, cy = 48;

    fx_radial(cx, cy, 72, fx_rgb(255, 70, 140), (int)(30 + 60 * k + 40 * beat));
    heart_shape(cx, cy, s, fx_rgb(120, 15, 60), (int)(90 * k), 0);

    enum { N = 44 };
    float px[N], py[N];
    float swirl = (1.0f - k) * 2.6f;
    float cs = cosf(swirl), sn = sinf(swirl);
    for (int i = 0; i < N; i++) {
        float u = i * FX_TAU / N, hx, hy;
        heart_point(u, s, cx, cy, &hx, &hy);
        float sx = fx_hashf(i * 3 + 1) * W, sy = fx_hashf(i * 3 + 2) * 100.0f;
        float x = fx_lerp(sx, hx, k) - cx, y = fx_lerp(sy, hy, k) - cy;
        px[i] = cx + x * cs - y * sn;
        py[i] = cy + x * sn + y * cs;
    }
    if (k > 0.9f) {
        int a = (int)(160 * (k - 0.9f) / 0.1f);
        for (int i = 0; i < N; i++) {
            int j = (i + 1) % N;
            fx_line(px[i], py[i], px[j], py[j], 0.9f, fx_rgb(255, 120, 180), a, 200);
        }
    }
    for (int i = 0; i < N; i++) {
        uint16_t col = fx_hsv(330.0f + 40.0f * sinf(i * 0.4f + t * 0.002f), 0.5f, 1.0f);
        fx_disc(px[i], py[i], 1.1f + 0.5f * beat, col, 255, 255);
    }
    if (k >= 1.0f) {
        for (int i = 0; i < 4; i++) {
            float tt = t + i * 400.0f;
            int cyc = (int)(tt / 1600.0f);
            float p = fmodf(tt, 1600.0f) / 1600.0f;
            float hx, hy;
            heart_point(fx_hashf(cyc * 11 + i) * FX_TAU, s * 1.15f, cx, cy, &hx, &hy);
            sparkle(hx, hy, 3.5f * sinf(p * FX_PI), WHITE, 255);
        }
    }
}

static void sc_moon(const scene_ctx_t *c)
{
    float t = c->t;
    stars_bg(11, 50, t, 100, 0.9f);
    float k = fx_ease_inout(fx_prog(t, 200, 2000));
    uint16_t moonc = fx_mix(fx_rgb(235, 232, 210), fx_rgb(205, 70, 40), k);

    if (k > 0.6f) {
        int a = (int)(200 * (k - 0.6f) / 0.4f);
        fx_radial(64, 50, 64, fx_rgb(255, 100, 40), a);
        for (int i = 0; i < 16; i++) {
            float an = i * FX_TAU / 16.0f + t * 0.0002f;
            float r1 = 34.0f + 8.0f * fx_hashf(i) + 3.0f * sinf(t * 0.003f + i);
            fx_line(64 + cosf(an) * 31, 50 + sinf(an) * 31, 64 + cosf(an) * r1, 50 + sinf(an) * r1,
                    1.0f, fx_rgb(255, 150, 70), a, 255);
        }
    } else {
        fx_radial(64, 50, 58, fx_rgb(200, 200, 175), 70);
    }

    fx_disc(64, 50, 30, moonc, 255, 120);
    uint16_t crater = fx_mix(moonc, BLACK, 0.18f);
    const float cr[][3] = {{54, 42, 5}, {72, 58, 6}, {71, 38, 3.5f}, {52, 62, 4}, {79, 47, 2.5f}, {61, 52, 2}};
    for (int i = 0; i < 6; i++) fx_disc(cr[i][0], cr[i][1], cr[i][2], crater, 255, 0);

    float sx = fx_lerp(-4, 64, k);
    fx_disc(sx, 50, 31.5f, fx_rgb(26, 4, 6), 200, 0);
    if (k > 0.6f) fx_ring(64, 50, 31.0f, 1.6f, fx_rgb(255, 170, 90), (int)(255 * (k - 0.6f) / 0.4f), 255);
}

static void sc_liftoff(const scene_ctx_t *c)
{
    float t = c->t;
    fx_vgradient(0, 100, fx_rgb(10, 20, 64), fx_rgb(44, 64, 128), 255);
    float k = fx_prog(t, 450, 1300);
    float e = fx_ease_in(k);

    for (int i = 0; i < 22; i++) {
        float x = fx_hashf(i) * W;
        float len = 10.0f + fx_hashf(i + 30) * 25.0f;
        float sp = 0.12f + 0.6f * e;
        float y = fmodf(fx_hashf(i + 60) * 160.0f + t * sp, 160.0f) - 40.0f;
        fx_line(x, y, x, y + len, 0.6f, fx_rgb(180, 210, 255), (int)(50 + 140 * e), 40);
    }

    fx_line(0, 98, W, 98, 1.0f, fx_rgb(90, 120, 190), 200, 80);
    if (t < 450.0f) {
        fx_radial(64, 96, 8.0f + t * 0.05f, fx_rgb(150, 200, 255), 220);
        fx_disc(64, 94, 2.5f, WHITE, 255, 255);
        return;
    }

    float hy = 96.0f - e * 150.0f;
    float tail = fminf(100.0f, hy + 20.0f + 90.0f * e);
    fx_line(64, hy, 64, tail, 3.5f + 2.0f * e, fx_rgb(130, 180, 255), 200, 255);
    fx_line(64, hy, 64, tail, 1.4f, WHITE, 255, 255);
    fx_disc(64, hy, 3.0f, WHITE, 255, 255);

    float lt = t - 450.0f;
    fx_ring(64, 98, 6.0f + lt * 0.08f, 1.6f, fx_rgb(180, 220, 255), (int)(220 * expf(-lt / 400.0f)), 220);
    for (int i = 0; i < 12; i++) {
        float vx = (fx_hashf(i + 90) - 0.5f) * 0.09f, vy = -fx_hashf(i + 95) * 0.1f;
        float x = 64 + vx * lt, y = 97 + vy * lt + 0.00012f * lt * lt;
        if (y < 99) fx_disc(x, y, 0.8f, fx_rgb(200, 220, 255), (int)(255 * expf(-lt / 600.0f)), 200);
    }
}

static void sc_warp(const scene_ctx_t *c)
{
    float t = c->t;
    const float cx = 64, cy = 76;
    fx_radial(cx, cy, 80, fx_hsv(c->hue, 0.8f, 0.7f), (int)(50 + 110 * c->pulse));

    /* tunnel rings rushing past */
    for (int j = 0; j < 5; j++) {
        float u = fmodf(j / 5.0f + t * 0.0011f, 1.0f);
        float r = 4.0f + 120.0f * u * u;
        fx_ring(cx, cy, r, 0.6f + 1.4f * u, fx_hsv(c->hue + j * 25.0f, 0.7f, 1.0f),
                (int)(40 + 120 * u + 60 * c->pulse), 160);
    }

    /* star streaks */
    for (int i = 0; i < 90; i++) {
        float a = fx_hashf(i * 5) * FX_TAU;
        float u = fmodf(fx_hashf(i * 5 + 1) + t * 0.0010f, 1.0f);
        float r = 3.0f + 150.0f * u * u * u;
        float r0 = r * (0.62f - 0.25f * c->pulse) - 1.0f;
        if (r0 > 120.0f) continue;
        uint16_t col = fx_hsv(c->hue + fx_hashf(i * 5 + 2) * 80.0f, 0.4f, 1.0f);
        int alpha = (int)(255.0f * fx_clamp01(u * 2.5f));
        fx_line(cx + cosf(a) * r0, cy + sinf(a) * r0, cx + cosf(a) * r, cy + sinf(a) * r,
                0.5f + 1.4f * u, col, alpha, 170);
    }
    fx_disc(cx, cy, 2.0f + 3.0f * c->pulse, WHITE, 200, 255);
}

static void sc_grid(const scene_ctx_t *c)
{
    float t = c->t;
    const float hz = 82;
    const uint16_t top = fx_rgb(12, 0, 42), bot = fx_rgb(150, 20, 120);
    fx_vgradient(0, (int)hz, top, bot, 255);
    stars_bg(13, 20, t, 50, 0.6f);

    float sr = 26.0f * (1.0f + 0.05f * c->pulse);
    const float scy = 60;
    fx_radial(64, scy, 64, fx_rgb(255, 80, 150), (int)(110 + 90 * c->pulse));
    fx_disc(64, scy, sr, fx_rgb(255, 160, 80), 255, 230);
    fx_disc(64, scy - sr * 0.35f, sr * 0.7f, fx_rgb(255, 215, 110), 120, 0);
    for (int j = 0; j < 6; j++) {
        float y = scy + 3.0f + j * 4.2f;
        float dy = y - scy;
        if (dy >= sr) break;
        float half = sqrtf(sr * sr - dy * dy) + 1.0f;
        fx_rect(64 - half, y, 2 * half, 0.8f + j * 0.5f, fx_mix(top, bot, y / hz), 255, 0);
    }

    float mtn[] = {0, hz, 10, hz - 10, 22, hz - 4, 34, hz - 14, 48, hz, 80, hz, 94, hz - 12,
                   106, hz - 5, 118, hz - 15, 128, hz - 6, 128, hz};
    fx_poly(mtn, 11, fx_rgb(44, 8, 64), 255, 0);
    fx_rect(0, hz, W, H - hz, fx_rgb(8, 0, 22), 255, 0);
    fx_line(0, hz, W, hz, 1.2f, fx_rgb(255, 90, 210), 255, 255);

    const uint16_t gc = fx_rgb(255, 60, 200);
    float speed = c->drop ? 0.0011f : 0.0007f;
    for (int j = 0; j < 9; j++) {
        float u = fmodf(j / 9.0f + t * speed, 1.0f);
        float y = hz + (H - hz + 10.0f) * u * u;
        fx_line(0, y, W, y, 0.5f + 1.0f * u, gc, (int)(60 + 195 * u), 150);
    }
    for (int i = -7; i <= 7; i++) {
        fx_line(64 + i * 5.0f, hz, 64 + i * 30.0f, H + 2, 0.8f, gc, (int)(130 + 90 * c->pulse), 120);
    }
}

static void sc_wave(const scene_ctx_t *c)
{
    float t = c->t;
    fx_vgradient(0, H, fx_rgb(4, 6, 22), BLACK, 255);
    for (int x = 8; x < W; x += 16) fx_line((float)x, 20, (float)x, 110, 0.5f, fx_rgb(26, 44, 64), 255, 0);
    for (int y = 20; y <= 110; y += 15) fx_line(0, (float)y, W, (float)y, 0.5f, fx_rgb(26, 44, 64), 255, 0);

    const uint16_t cols[3] = {fx_rgb(60, 230, 255), fx_rgb(255, 70, 190), fx_rgb(170, 110, 255)};
    float amp = 10.0f + 16.0f * c->pulse;
    for (int w = 0; w < 3; w++) {
        float px = 0, py = 64;
        for (int x = 0; x <= W; x += 3) {
            float env = sinf(FX_PI * x / W);
            float y = 64.0f + sinf(x * 0.09f * (1.0f + w * 0.35f) + t * 0.006f * (1.0f + w * 0.4f) + w * 2.0f)
                              * amp * (0.6f + 0.4f * sinf(t * 0.001f + w)) * env
                      + sinf(x * 0.31f + t * 0.013f) * 3.0f * c->pulse * env;
            if (x > 0) fx_line(px, py, (float)x, y, 1.2f, cols[w], 230, 220);
            px = (float)x;
            py = y;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Dispatch                                                           */
/* ------------------------------------------------------------------ */
scene_layout_t scene_layout(uint8_t scene)
{
    switch (scene) {
    case SC_NONE:  return SL_NONE;
    case SC_STARS:
    case SC_WARP:  return SL_FULL;
    case SC_GRID:
    case SC_WAVE:  return SL_FLOOR;
    default:       return SL_ILLUS;
    }
}

void scene_draw(uint8_t scene, const scene_ctx_t *c)
{
    switch (scene) {
    case SC_STARS:      sc_stars(c); break;
    case SC_BOOK:       sc_book(c); break;
    case SC_TEMPLE:     sc_temple(c); break;
    case SC_SHIELD:     sc_shield(c); break;
    case SC_LIGHTNING:  sc_lightning(c); break;
    case SC_WEB:        sc_web(c); break;
    case SC_CITY:       sc_city(c); break;
    case SC_LIST:       sc_list(c); break;
    case SC_ROAD:       sc_road(c); break;
    case SC_DICE:       sc_dice(c); break;
    case SC_HERO:       sc_hero(c); break;
    case SC_CASTLE:     sc_castle(c); break;
    case SC_LIGHTHOUSE: sc_lighthouse(c); break;
    case SC_HEART:      sc_heart(c); break;
    case SC_HEARTFORM:  sc_heartform(c); break;
    case SC_MOON:       sc_moon(c); break;
    case SC_LIFTOFF:    sc_liftoff(c); break;
    case SC_WARP:       sc_warp(c); break;
    case SC_GRID:       sc_grid(c); break;
    case SC_WAVE:       sc_wave(c); break;
    default: break;
    }
}
