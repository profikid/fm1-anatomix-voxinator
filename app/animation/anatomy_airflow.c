/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the airflow.
 *
 * The particles are the breath made visible: they start in the lungs, climb the trachea, pass the
 * vocal folds, rise through the pharynx and leave through the mouth (or, when the nasal resonance
 * is up, a part of them turn up into the nasal cavity). Their speed follows the level, and the
 * distortion makes them jagged.
 */

#include "anatomy.h"
#include "vox_math.h"
/* the landmarks this file asks the geometry for */
int an_airway(const anatomy_in_t *in, float *out, int max);
int an_nasal_path(const anatomy_in_t *in, float *out, int max);
void an_map(int32_t x, int32_t y, int32_t w, int32_t h, int32_t sx, int32_t sy, int32_t sw, int32_t sh,
            float *p, int n);

/* the point at the fraction t of a path's length */
static void path_at(const float *p, int n, float t, float *out)
{
    float total = 0.0f, len[20], want;
    int i;
    if (n < 2) {
        out[0] = p[0];
        out[1] = p[1];
        return;
    }
    for (i = 0; i < n - 1; i++) {
        float dx = p[(i + 1) * 2] - p[i * 2], dy = p[(i + 1) * 2 + 1] - p[i * 2 + 1];
        len[i] = an_sqrt(dx * dx + dy * dy);
        total += len[i];
    }
    if (total < 0.001f) {
        out[0] = p[0];
        out[1] = p[1];
        return;
    }
    want = t * total;
    for (i = 0; i < n - 1; i++) {
        if (want <= len[i] || i == n - 2) {
            float f = len[i] > 0.001f ? want / len[i] : 0.0f;
            out[0] = p[i * 2] + (p[(i + 1) * 2] - p[i * 2]) * f;
            out[1] = p[i * 2 + 1] + (p[(i + 1) * 2 + 1] - p[i * 2 + 1]) * f;
            return;
        }
        want -= len[i];
    }
}

/* the airflow's particles */
void anatomy_draw_airflow(anatomy_t *a, const anatomy_in_t *in, int32_t x, int32_t y, int32_t w, int32_t h)
{
    float air[28], nas[16], pt[2];
    uint32_t seed;
    int i;
    if (w < 8 || h < 8)
        return;
    an_airway(in, air, 14);
    an_nasal_path(in, nas, 8);
    seed = a->seed ^ in->now_ms;

    for (i = 0; i < a->n_particles && i < 32; i++) {
        float t = a->particles[i];
        float jitter = in->distort * 4.0f;
        int32_t px, py;
        uint16_t col;
        /* every fourth particle takes the nasal branch when the nasal resonance is up */
        if (in->nasal > 0.15f && (i & 3) == 0) {
            float nt = t;
            path_at(nas, 8, nt < 0.35f ? nt / 0.35f : 1.0f, pt);
        } else
            path_at(air, 14, t, pt);
        if (jitter > 0.0f) {
            uint32_t r = vox_rand(&seed);
            pt[0] += ((float)(r & 255u) - 128.0f) * jitter / 128.0f;
            pt[1] += ((float)((r >> 8) & 255u) - 128.0f) * jitter / 128.0f;
        }
        an_map(x, y, w, h, 0, 0, 240, 176, pt, 1);
        px = (int32_t)pt[0];
        py = (int32_t)pt[1];
        /* the particles pulse with the voice */
        {
            float bright = 0.35f + 0.65f * in->level;
            col = an_fade(AN_CYAN, AN_DARK, 1.0f - bright);
        }
        an_disc((float)px, (float)py, 1.6f + 1.2f * in->level, col);
        if (in->distort > 0.25f)
            cv_pset(px + 1, py - 1, an_fade(col, AN_RED, 0.5f));
    }
}
