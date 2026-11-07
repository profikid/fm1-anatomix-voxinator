/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the effects on the picture.
 *
 * The delay's ghost outlines repeat the tract behind the original, fading with the feedback. The
 * reverb's rings expand around the head and fade with the tail. The distortion breaks the outline
 * into pixels. The overload frames the picture in red and flashes.
 */

#include "anatomy.h"
#include "vox_math.h"

/* the landmarks this file asks the geometry for */
int an_airway(const anatomy_in_t *in, float *out, int max);
int an_silhouette(const anatomy_in_t *in, float *out, int max);
void an_head_centre(const anatomy_in_t *in, float *out);
void an_map(int32_t x, int32_t y, int32_t w, int32_t h, int32_t sx, int32_t sy, int32_t sw, int32_t sh,
            float *p, int n);
/* the helpers the picture is built from (ui_common.c) */
float an_sqrt(float a);
void an_ring(float cx, float cy, float r, uint16_t c);
uint16_t an_fade(uint16_t c, uint16_t bg, float t);

/* the delay's ghost outlines: the tract, repeated behind itself */
static void draw_ghosts(const anatomy_in_t *in, int32_t x, int32_t y, int32_t w, int32_t h)
{
    float air[28];
    int ghosts = 1 + (int)(in->delay_amt * 3.0f);
    int g, i;
    if (in->delay_amt < 0.02f)
        return;
    an_airway(in, air, 14);
    for (g = 1; g <= ghosts; g++) {
        float off = (float)g * (9.0f + 13.0f * in->delay_amt);
        uint16_t col = an_fade(AN_CYAN, AN_DARK, 0.80f - 0.14f * (float)g);
        for (i = 0; i < 14; i++)
            air[i * 2] += off;
        an_map(x, y, w, h, 0, 0, 240, 176, air, 14);
        for (i = 0; i < 13; i++)
            cv_line((int32_t)air[i * 2], (int32_t)air[i * 2 + 1], (int32_t)air[i * 2 + 2],
                    (int32_t)air[i * 2 + 3], col);
        for (i = 0; i < 14; i++)
            air[i * 2] -= off;            /* back, for the next ghost */
    }
}

/* the reverb's rings: they grow out of the head and fade with the tail */
static void draw_rings(const anatomy_t *a, const anatomy_in_t *in, int32_t x, int32_t y, int32_t w, int32_t h)
{
    float c[2];
    int i;
    if (a->n_rings == 0)
        return;
    an_head_centre(in, c);
    an_map(x, y, w, h, 0, 0, 240, 176, c, 1);
    for (i = 0; i < a->n_rings && i < 6; i++) {
        float p = a->rings[i];
        float r = (12.0f + 50.0f * p) * (float)w / 240.0f;
        /* the ring fades as it grows: the tail's strength decides how much of it is left */
        uint16_t col = an_fade(AN_CYAN, AN_DARK, (1.0f - p) * 0.95f * (0.45f + 0.55f * in->reverb_amt));
        if (r > 1.0f)
            an_ring(c[0], c[1], r, col);
    }
}

/* the distortion: the outline breaks into pixels.
 * The silhouette's edges are walked in two-pixel steps and every step is snapped to the pixel
 * grid, so the bone's edge comes apart the way a clipped signal comes apart. */
static void draw_pixel_edge(const anatomy_in_t *in, int32_t x, int32_t y, int32_t w, int32_t h)
{
    float sil[96];
    uint32_t seed = 0x9E37u ^ in->now_ms;
    int n, i;
    if (in->distort < 0.03f)
        return;
    n = an_silhouette(in, sil, 48);
    an_map(x, y, w, h, 0, 0, 240, 176, sil, n);
    for (i = 0; i < n; i++) {
        int j = (i + 1) % n;
        float x0 = sil[i * 2], y0 = sil[i * 2 + 1];
        float dx = sil[j * 2] - x0, dy = sil[j * 2 + 1] - y0;
        float len = an_sqrt(dx * dx + dy * dy);
        int steps = (int)(len * 0.5f);
        int s;
        for (s = 0; s < steps; s++) {
            float t = (float)s * 2.0f / (len > 0.01f ? len : 0.01f);
            float px = x0 + dx * t, py = y0 + dy * t;
            uint32_t r = vox_rand(&seed);
            float jx = ((float)(r & 255u) - 128.0f) * (1.0f / 128.0f) * in->distort * 3.0f;
            int32_t qx = ((int32_t)(px + jx)) & ~1;
            int32_t qy = ((int32_t)py) & ~1;
            cv_pset(qx, qy, an_fade(AN_RED, AN_DARK, 0.20f));
            cv_pset(qx + 1, qy, an_fade(AN_RED, AN_DARK, 0.35f));
        }
    }
}

/* the overload: the picture's frame turns red and flashes */
static void draw_overload(const anatomy_in_t *in, int32_t x, int32_t y, int32_t w, int32_t h)
{
    uint16_t col;
    float blink;
    if (!in->overload)
        return;
    blink = 0.5f + 0.5f * vox_sin_turns((float)(in->now_ms % 1000u) * 0.006f);
    col = an_fade(AN_RED, AN_DARK, 1.0f - blink * 0.5f);
    cv_rect(x + 1, y + 1, w - 2, 2, col);
    cv_rect(x + 1, y + h - 3, w - 2, 2, col);
    cv_rect(x + 1, y + 1, 2, h - 2, col);
    cv_rect(x + w - 3, y + 1, 2, h - 2, col);
}

/* the effects on the picture */
void anatomy_draw_effects(anatomy_t *a, const anatomy_in_t *in, int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (w < 8 || h < 8)
        return;
    draw_ghosts(in, x, y, w, h);
    draw_rings(a, in, x, y, w, h);
    draw_pixel_edge(in, x, y, w, h);
    draw_overload(in, x, y, w, h);
}
