/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the lungs and the diaphragm.
 *
 * The two lobes fill and empty with the input's level: the louder the voice, the wider they open.
 * With nothing coming in they keep a slow, small breath (the idle breathing). The overload makes
 * them fill too far, and the diaphragm's dome follows them down.
 */

#include "anatomy.h"
#include "vox_math.h"

/* the landmarks this file asks the geometry for */
void an_lung_box(const anatomy_in_t *in, int which, float *out);
void an_map(int32_t x, int32_t y, int32_t w, int32_t h, int32_t sx, int32_t sy, int32_t sw, int32_t sh,
            float *p, int n);
/* the helpers the picture is built from (ui_common.c) */
void an_poly(const float *p, int n, uint16_t c);
void an_polyf(const float *p, int n, uint16_t c);
void an_ell(float cx, float cy, float rx, float ry, uint16_t c);
void an_curve(const float *p, int n, uint16_t c);
uint16_t an_fade(uint16_t c, uint16_t bg, float t);

/* how full the lungs are, 0..1: the voice's level over the idle breath */
static float lung_fill(const anatomy_t *a, const anatomy_in_t *in)
{
    float idle = 0.5f + 0.5f * vox_sin_turns(a->breath);
    float f = idle * 0.30f + in->level * 0.70f;
    if (in->overload)
        f += 0.30f;                       /* the overload: the lungs fill too far */
    return f > 1.0f ? 1.0f : f;
}

/* the lungs and the diaphragm */
void anatomy_draw_lungs(anatomy_t *a, const anatomy_in_t *in, int32_t x, int32_t y, int32_t w, int32_t h)
{
    float box[20], lung[2][20], br[6], dia[10];
    float fill;
    uint16_t flesh, edge, lobe;
    int i, side;
    if (w < 8 || h < 8)
        return;
    fill = lung_fill(a, in);
    flesh = in->overload ? an_fade(AN_RED, AN_TISSUE, 0.35f) : an_fade(AN_PINK, AN_TISSUE, 0.45f);
    edge = in->overload ? AN_RED : AN_CORAL;
    lobe = an_fade(edge, AN_DARK, 0.62f);

    /* the two lobes: they grow out of their own centres as they fill */
    for (side = 0; side < 2; side++) {
        float cx = 0.0f, cy = 0.0f, x0 = 1.0e9f, x1 = -1.0e9f, y0 = 1.0e9f, y1 = -1.0e9f;
        float lob[8];              /* the lobules' centres, in the design's own coordinates */
        an_lung_box(in, side, box);
        for (i = 0; i < 10; i++) {
            lung[side][i * 2] = box[i * 2];
            lung[side][i * 2 + 1] = box[i * 2 + 1];
            cx += lung[side][i * 2];
            cy += lung[side][i * 2 + 1];
        }
        cx *= 0.1f;
        cy *= 0.1f;
        /* the back lobe spreads to the left, the front one to the right, and both drop */
        for (i = 0; i < 10; i++) {
            float dx = lung[side][i * 2] - cx;
            float dy = lung[side][i * 2 + 1] - cy;
            lung[side][i * 2] += dx * 0.16f * fill;
            lung[side][i * 2 + 1] += (5.0f + dy * 0.10f) * fill;
        }
        for (i = 0; i < 10; i++) {
            if (lung[side][i * 2] < x0)
                x0 = lung[side][i * 2];
            if (lung[side][i * 2] > x1)
                x1 = lung[side][i * 2];
            if (lung[side][i * 2 + 1] < y0)
                y0 = lung[side][i * 2 + 1];
            if (lung[side][i * 2 + 1] > y1)
                y1 = lung[side][i * 2 + 1];
        }
        /* the lobules: a 2x2 grid of soft arcs inside the lobe */
        for (i = 0; i < 4; i++) {
            float fx = 0.30f + 0.38f * (float)(i & 1);
            float fy = 0.32f + 0.36f * (float)(i >> 1);
            lob[i * 2] = x0 + (x1 - x0) * fx;
            lob[i * 2 + 1] = y0 + (y1 - y0) * fy;
        }
        an_map(x, y, w, h, 0, 0, 240, 176, lung[side], 10);
        an_polyf(lung[side], 10, flesh);
        an_poly(lung[side], 10, edge);
        an_map(x, y, w, h, 0, 0, 240, 176, lob, 4);
        for (i = 0; i < 4; i++)
            an_ell(lob[i * 2], lob[i * 2 + 1], (x1 - x0) * 0.15f * (float)w / 240.0f,
                   (y1 - y0) * 0.13f * (float)h / 176.0f, lobe);
        /* the fissure: the line between the lobes */
        if (side == 0) {
            float fx = 0.0f, fy = 0.0f, fis[4];
            for (i = 0; i < 10; i++) {
                if (lung[side][i * 2] > fx) {
                    fx = lung[side][i * 2];
                    fy = lung[side][i * 2 + 1];
                }
            }
            fis[0] = fx;
            fis[1] = fy;
            fis[2] = fx + 14.0f;
            fis[3] = fy + 8.0f;
            an_map(x, y, w, h, 0, 0, 240, 176, fis, 2);
            cv_line((int32_t)fis[0], (int32_t)fis[1], (int32_t)fis[2], (int32_t)fis[3],
                    an_fade(AN_BONE, AN_DARK, 0.35f));
        }
    }

    /* the bronchi: the trachea's split, into each lobe */
    {
        br[0] = 104.0f;
        br[1] = 132.0f;
        br[2] = 82.0f;
        br[3] = 138.0f;
        br[4] = 138.0f;
        br[5] = 136.0f;
        an_map(x, y, w, h, 0, 0, 240, 176, br, 3);
        cv_line((int32_t)br[0], (int32_t)br[1], (int32_t)br[2], (int32_t)br[3], an_fade(AN_BONE, AN_DARK, 0.3f));
        cv_line((int32_t)br[0], (int32_t)br[1], (int32_t)br[4], (int32_t)br[5], an_fade(AN_BONE, AN_DARK, 0.3f));
        cv_line((int32_t)br[2], (int32_t)br[3], (int32_t)br[2] - 5.0f, (int32_t)br[3] + 6.0f,
                an_fade(AN_BONE, AN_DARK, 0.45f));
        cv_line((int32_t)br[2], (int32_t)br[3], (int32_t)br[2] + 2.0f, (int32_t)br[3] + 9.0f,
                an_fade(AN_BONE, AN_DARK, 0.45f));
        cv_line((int32_t)br[4], (int32_t)br[5], (int32_t)br[4] + 6.0f, (int32_t)br[5] + 5.0f,
                an_fade(AN_BONE, AN_DARK, 0.45f));
    }

    /* the diaphragm: the dome under the lungs, pushed down by them */
    {
        static const float d0[5][2] = {{36, 170}, {70, 160}, {105, 152}, {142, 158}, {176, 168}};
        for (i = 0; i < 5; i++) {
            dia[i * 2] = d0[i][0];
            dia[i * 2 + 1] = d0[i][1] + 5.0f * fill;
        }
        an_map(x, y, w, h, 0, 0, 240, 176, dia, 5);
        an_curve(dia, 5, AN_CORAL);
        for (i = 0; i < 4; i++)
            cv_line((int32_t)dia[i * 2], (int32_t)dia[i * 2 + 1] + 2, (int32_t)dia[i * 2 + 4],
                    (int32_t)dia[i * 2 + 5] + 2, an_fade(AN_CORAL, AN_DARK, 0.45f));
    }
}
