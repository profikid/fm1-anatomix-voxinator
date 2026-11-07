/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the vocal folds.
 *
 * The two folds are the picture's focus: they open and close at the detected pitch's speed (the
 * rate is slowed down so the eye can follow it), and the gap widens with the level. When the pitch
 * corrector is working, a second, dimmer pair of folds shows the corrected pitch beside the
 * detected one, so the correction can be seen.
 */

#include "anatomy.h"
#include "vox_math.h"

/* the landmarks this file asks the geometry for */
void an_fold_anchors(const anatomy_in_t *in, float *out);
void an_map(int32_t x, int32_t y, int32_t w, int32_t h, int32_t sx, int32_t sy, int32_t sw, int32_t sh,
            float *p, int n);

/* how fast the folds move, in turns a second: the pitch, slowed to something the eye can follow.
 * The clock (anatomy_update) asks this for the phase, so the rate lives here, with the folds. */
float an_fold_rate(float f0)
{
    float r = 1.2f + (f0 > 0.0f ? f0 / 55.0f : 0.0f);
    return r > 14.0f ? 14.0f : r;
}

/* the gap between the folds, in the design's pixels */
static float fold_gap(const anatomy_in_t *in, float phase)
{
    float g = 1.4f + 4.2f * in->level + 1.3f * vox_sin_turns(phase);
    if (g < 0.4f)
        g = 0.4f;
    if (g > 7.0f)
        g = 7.0f;
    return g;
}

/* the folds, in the rectangle given: the design window (sx, sy, sw, sh) is magnified into it */
static void folds_draw(const anatomy_t *a, const anatomy_in_t *in, int32_t x, int32_t y, int32_t w, int32_t h,
                       int32_t sx, int32_t sy, int32_t sw, int32_t sh)
{
    float fold[8], p[8];
    float gap, cy;
    int i;
    if (w < 8 || h < 8)
        return;
    an_fold_anchors(in, fold);

    /* the detected pitch's folds */
    gap = fold_gap(in, a->fold_phase[0]);
    cy = 0.5f * (fold[1] + fold[5]);
    for (i = 0; i < 4; i++) {
        p[i * 2] = fold[i * 2];
        p[i * 2 + 1] = fold[i * 2 + 1];
    }
    p[1] = cy - gap * 0.5f;               /* the upper fold's front end */
    p[5] = cy + gap * 0.5f;               /* the lower fold's front end */
    an_map(x, y, w, h, sx, sy, sw, sh, p, 4);

    /* the sound coming through: a warm glow in the gap, under the folds */
    if (in->level > 0.02f) {
        float gx = p[0], gy = (float)((int32_t)p[1] + (int32_t)p[5]) * 0.5f;
        float rr = (1.5f + 3.0f * in->level) * (float)w / (float)(sw > 0 ? sw : 240);
        if (rr > 6.0f)
            rr = 6.0f;                  /* the glow never swallows the folds */
        an_disc(gx, gy, rr, an_fade(AN_AMB, AN_DARK, 0.65f));
    }

    /* the folds themselves: two bright bars, the picture's focus */
    cv_line((int32_t)p[0], (int32_t)p[1], (int32_t)p[4], (int32_t)p[5], AN_AMB);
    cv_line((int32_t)p[2], (int32_t)p[3], (int32_t)p[6], (int32_t)p[7], AN_AMB);
    cv_line((int32_t)p[0], (int32_t)p[1] + 1, (int32_t)p[4], (int32_t)p[5] + 1, an_fade(AN_AMB, AN_RED, 0.4f));
    cv_line((int32_t)p[2], (int32_t)p[3] + 1, (int32_t)p[6], (int32_t)p[7] + 1, an_fade(AN_AMB, AN_RED, 0.4f));

    /* the corrected pitch: a second, dimmer pair of folds beside the first */
    if (in->target_f0 > 1.0f && in->f0 > 1.0f && in->target_f0 != in->f0) {
        float ph = (float)(in->now_ms % 60000u) * 0.001f * an_fold_rate(in->target_f0);
        float t;
        ph = ph - (float)(int32_t)ph;
        t = fold_gap(in, ph);
        for (i = 0; i < 4; i++) {
            p[i * 2] = fold[i * 2];
            p[i * 2 + 1] = fold[i * 2 + 1];
        }
        p[1] = cy - t * 0.5f - 1.0f;
        p[5] = cy + t * 0.5f + 1.0f;
        an_map(x, y, w, h, sx, sy, sw, sh, p, 4);
        cv_line((int32_t)p[0], (int32_t)p[1], (int32_t)p[4], (int32_t)p[5], an_fade(AN_CYAN, AN_DARK, 0.35f));
        cv_line((int32_t)p[2], (int32_t)p[3], (int32_t)p[6], (int32_t)p[7], an_fade(AN_CYAN, AN_DARK, 0.35f));
    }
}

/* the vocal folds */
void anatomy_draw_folds(anatomy_t *a, const anatomy_in_t *in, int32_t x, int32_t y, int32_t w, int32_t h)
{
    folds_draw(a, in, x, y, w, h, 0, 0, 240, 176);
}

/* the folds, magnified: the GLIDE page looks at them closely */
void an_folds_zoom(const anatomy_t *a, const anatomy_in_t *in, int32_t x, int32_t y, int32_t w, int32_t h,
                   int32_t sx, int32_t sy, int32_t sw, int32_t sh)
{
    folds_draw(a, in, x, y, w, h, sx, sy, sw, sh);
}
