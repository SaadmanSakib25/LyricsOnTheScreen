#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Where the lyric text goes for a given scene. */
typedef enum {
    SL_NONE = 0, /* no scene: text centered, full size            */
    SL_FULL,     /* full-screen background: text centered          */
    SL_FLOOR,    /* scene in the upper part, text over the floor   */
    SL_ILLUS,    /* illustration on top, text in the lower third   */
} scene_layout_t;

typedef struct {
    float t;        /* ms since this scene's shot began (spans repeated lines) */
    float line_t;   /* ms since the current lyric line began                   */
    float pulse;    /* beat pulse, 1 on the beat decaying to 0                 */
    float bfrac;    /* beat phase 0..1                                         */
    float hue;      /* slowly drifting hue for color cycling                   */
    bool drop;      /* current line is a drop                                  */
} scene_ctx_t;

scene_layout_t scene_layout(uint8_t scene);
void scene_draw(uint8_t scene, const scene_ctx_t *ctx);
