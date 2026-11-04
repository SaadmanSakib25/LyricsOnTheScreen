#pragma once

#include <stdint.h>
#include <stdbool.h>

#define SONG_TITLE "Something Just Like This"
#define SONG_ARTIST "Coldplay x Chainsmokers"
#define SONG_TOTAL_DURATION_MS 247000

/* Song tempo, used to pulse drops / instrumental breaks on the beat. */
#define SONG_BPM 103.0f

/* How a line is animated on screen (see lyric_fx.c). */
typedef enum {
    LS_VERSE = 0, /* words float up and fade in one by one, cool blue glow */
    LS_TYPE,      /* typewriter: letters appear one by one with a cursor  */
    LS_CHORUS,    /* big words pop in one by one, pink glow               */
    LS_DROP,      /* beat-synced: color-cycling words, pulses, rings      */
    LS_TITLE,     /* title card with the artist line underneath           */
    LS_BREAK,     /* instrumental: pulsing music note + small caption     */
} lyric_style_t;

/*
 * Text markup:
 *   '|'  forces a line break at that point (otherwise words auto-wrap)
 *   '*'  toggles the accent color, e.g. "And *Batman* with his fists"
 */
typedef struct {
    uint32_t start_ms;
    const char *text;
    uint8_t style;
} lyric_line_t;

static const lyric_line_t lyrics[] = {
    {0,      "Something|just like|*this*", LS_TITLE},
    {8300,   "I've been reading|books of old", LS_TYPE},
    {10700,  "The *legends*|and the *myths*", LS_VERSE},
    {13000,  "*Achilles*|and his gold", LS_VERSE},
    {15300,  "*Hercules*|and his gifts", LS_VERSE},
    {17600,  "*Spider-Man's*|control", LS_VERSE},
    {20000,  "And *Batman*|with his fists", LS_VERSE},
    {22500,  "And clearly|I don't see myself|upon that *list*", LS_VERSE},
    {26500,  "She said,|Where'd you|wanna go?", LS_TYPE},
    {29000,  "How much you|wanna *risk*?", LS_VERSE},
    {31200,  "I'm not looking|for somebody", LS_VERSE},
    {33600,  "With some|*superhuman* gifts", LS_VERSE},
    {36000,  "Some|*superhero*", LS_CHORUS},
    {38400,  "Some *fairytale*|bliss", LS_CHORUS},
    {40800,  "Just something|I can turn to", LS_VERSE},
    {43200,  "Somebody|I can *kiss*", LS_CHORUS},
    {45400,  "I want *something*|just like this", LS_CHORUS},
    {47800,  "Doo-doo-doo,|doo-doo-doo", LS_DROP},
    {50200,  "Doo-doo-doo,|doo, doo", LS_DROP},
    {52500,  "Doo-doo-doo,|doo-doo-doo", LS_DROP},
    {54600,  "Oh, I want|*something*|just like this", LS_DROP},
    {57200,  "Doo-doo-doo,|doo-doo-doo", LS_DROP},
    {59600,  "Doo-doo-doo,|doo, doo", LS_DROP},
    {61900,  "Doo-doo-doo,|doo-doo-doo", LS_DROP},
    {64200,  "Oh, I want|*something*|just like this", LS_DROP},
    {73500,  "I want *something*|just like this", LS_CHORUS},
    {77000,  "instrumental", LS_BREAK},
    {83000,  "I've been reading|books of old", LS_TYPE},
    {85300,  "The *legends*|and the *myths*", LS_VERSE},
    {87600,  "The *testaments*|they told", LS_VERSE},
    {89900,  "The *moon*|and its *eclipse*", LS_VERSE},
    {92200,  "And *Superman*|unrolls", LS_VERSE},
    {94500,  "A suit|before he lifts", LS_VERSE},
    {97800,  "But I'm not|the kind of person|that it *fits*", LS_VERSE},
    {101000, "She said,|Where'd you|wanna go?", LS_TYPE},
    {103600, "How much you|wanna *risk*?", LS_VERSE},
    {105700, "I'm not looking|for somebody", LS_VERSE},
    {108100, "With some|*superhuman* gifts", LS_VERSE},
    {110700, "Some|*superhero*", LS_CHORUS},
    {113000, "Some *fairytale*|bliss", LS_CHORUS},
    {115300, "Just something|I can turn to", LS_VERSE},
    {117700, "Somebody|I can *miss*", LS_CHORUS},
    {120000, "I want *something*|just like this", LS_CHORUS},
    {122500, "I want *something*|just like this", LS_CHORUS},
    {129300, "Oh, I want|*something*|just like this", LS_CHORUS},
    {131900, "Doo-doo-doo,|doo-doo-doo", LS_DROP},
    {134200, "Doo-doo-doo,|doo, doo", LS_DROP},
    {136500, "Doo-doo-doo,|doo-doo-doo", LS_DROP},
    {138700, "Oh, I want|*something*|just like this", LS_DROP},
    {141400, "Doo-doo-doo,|doo-doo-doo", LS_DROP},
    {143700, "Doo-doo-doo,|doo, doo", LS_DROP},
    {146000, "Doo-doo-doo,|doo-doo-doo", LS_DROP},
    {148000, "Oh, I want|*something*|just like this", LS_DROP},
    {150500, "guitar solo", LS_BREAK},
    {160500, "Where'd you|wanna go?", LS_TYPE},
    {163000, "How much you|wanna *risk*?", LS_VERSE},
    {165000, "I'm not looking|for somebody", LS_VERSE},
    {167400, "With some|*superhuman* gifts", LS_VERSE},
    {169900, "Some|*superhero*", LS_CHORUS},
    {172200, "Some *fairytale*|bliss", LS_CHORUS},
    {174600, "Just something|I can turn to", LS_VERSE},
    {177000, "Somebody|I can *kiss*", LS_CHORUS},
    {179200, "I want *something*|just like this", LS_CHORUS},
    {181800, "Oh, I want|*something*|just like this", LS_CHORUS},
    {184200, "Doo-doo-doo,|doo-doo-doo", LS_DROP},
    {186500, "Doo-doo-doo,|doo, doo", LS_DROP},
    {188800, "Doo-doo-doo,|doo-doo-doo", LS_DROP},
    {191000, "Oh, I want|*something*|just like this", LS_DROP},
    {193600, "Doo-doo-doo,|doo-doo-doo", LS_DROP},
    {196000, "Doo-doo-doo,|doo, doo", LS_DROP},
    {198300, "Doo-doo-doo,|doo-doo-doo", LS_DROP},
    {200500, "Oh, I want|*something*|just like this", LS_DROP},
    {205500, "Oh, I want|*something*|just like this", LS_DROP},
    {210000, "outro", LS_BREAK},
    {220000, "Where'd you|wanna go?", LS_TYPE},
    {222500, "How much you|wanna *risk*?", LS_VERSE},
    {224500, "I'm not looking|for somebody", LS_VERSE},
    {226800, "With some|*superhuman* gifts", LS_VERSE},
    {229200, "Some|*superhero*", LS_CHORUS},
    {231500, "Some *fairytale*|bliss", LS_CHORUS},
    {233900, "Just something|I can turn to", LS_VERSE},
    {236200, "Somebody|I can *kiss*", LS_CHORUS},
    {238500, "I want *something*|just like this", LS_CHORUS},
    {242000, "Oh, I want|*something*|just like this", LS_CHORUS},
    {247000, "Something|just like|*this*", LS_TITLE},
};

#define LYRICS_COUNT (sizeof(lyrics) / sizeof(lyrics[0]))
