#pragma once

#include <stdint.h>

/* How a line is animated on screen (see lyric_fx.c). */
typedef enum {
    LS_VERSE = 0, /* words float up and fade in one by one, cool blue glow */
    LS_TYPE,      /* typewriter: letters appear one by one with a cursor  */
    LS_CHORUS,    /* big words pop in one by one, pink glow               */
    LS_DROP,      /* beat-synced: color-cycling words, pulses, rings      */
    LS_TITLE,     /* title card with the artist line underneath           */
    LS_BREAK,     /* instrumental: pulsing music note + small caption     */
} lyric_style_t;

/* Animated illustration shown with a line (see scenes.c). Consecutive lines
 * with the same scene continue one shot instead of restarting it. */
typedef enum {
    SC_NONE = 0,
    SC_STARS,      /* night sky, shooting stars                 */
    SC_BOOK,       /* open book, flipping pages, magic dust     */
    SC_TEMPLE,     /* Greek temple drawing itself in            */
    SC_SHIELD,     /* golden shield + spear, shine sweep        */
    SC_LIGHTNING,  /* storm clouds, lightning strikes           */
    SC_WEB,        /* spider web spinning out, spider descends  */
    SC_CITY,       /* night skyline, moon, bats, comic POW!     */
    SC_LIST,       /* checklist... with a red X at the end      */
    SC_ROAD,       /* road to the sunset, map pin drops in      */
    SC_DICE,       /* dice tumbling across the floor            */
    SC_HERO,       /* caped silhouette on a rooftop, sun rays   */
    SC_CASTLE,     /* fairytale castle, wand trail, fireworks   */
    SC_LIGHTHOUSE, /* lighthouse sweeping its beam over the sea */
    SC_HEART,      /* beating heart, floating hearts            */
    SC_HEARTFORM,  /* particles swirl together into a heart     */
    SC_MOON,       /* blood-moon eclipse                        */
    SC_LIFTOFF,    /* hero launches into the sky                */
    SC_WARP,       /* hyperspace tunnel (drops)                 */
    SC_GRID,       /* synthwave sun + neon grid (drops)         */
    SC_WAVE,       /* oscilloscope waves (guitar solo)          */
} lyric_scene_t;

/*
 * Text markup:
 *   '|'  forces a line break at that point (otherwise words auto-wrap)
 *   '*'  toggles the accent color, e.g. "And *Batman* with his fists"
 */
typedef struct {
    uint32_t start_ms;
    const char *text;
    uint8_t style;
    uint8_t scene;
} lyric_line_t;
