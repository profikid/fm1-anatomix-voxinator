/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the glide, the curve between two notes.
 *
 * The corrector decides WHICH note the voice should be on; the glide decides HOW it gets there. Keeping
 * them apart is what lets the two controls (RETUNE and GLIDE) behave independently:
 *
 *   RETUNE  how fast the voice arrives at the note
 *   GLIDE   how long the travel itself takes: 0 an instant change, 50 a smooth portamento,
 *           100 an extreme glide that never quite arrives
 *
 * The curve is an exponential approach: it starts fast and settles, which is how a voice actually moves
 * between two notes.
 */
#include "vox.h"
#include "vox_math.h"

#define LN2 0.6931472f

/* the glide's reset */
void vox_glide_init(vox_glide_t *g)
{
    g->from = 0.0f;
    g->to = 0.0f;
    g->t = 1.0f;
    g->start_ms = 0;
    g->moving = 0;
}

/* start a glide towards a new note */
void vox_glide_start(vox_glide_t *g, float from_hz, float to_hz, uint32_t now_ms)
{
    g->from = from_hz > 0.0f ? from_hz : to_hz;
    g->to = to_hz;
    g->t = 0.0f;
    g->start_ms = now_ms;
    g->moving = 1;
}

/* the glide's current pitch, in Hz */
float vox_glide_at(vox_glide_t *g, uint8_t retune, uint8_t glide_amt, uint32_t now_ms)
{
    uint32_t dt = now_ms - g->start_ms;
    float travel, eff, tau, shape, r;

    if (!g->moving)
        return g->to > 0.0f ? g->to : g->from;

    /* GLIDE 0: the note changes instantly */
    if (glide_amt == 0u) {
        g->t = 1.0f;
        return g->to;
    }

    /* the travel: 15 ms at GLIDE 0 (an instant change) to 2215 ms at GLIDE 100 */
    travel = 15.0f + (float)glide_amt * 22.0f;
    /* the retune shortens the travel: at 100 it is a tenth of it */
    r = (float)retune * 0.01f;
    eff = travel * (1.05f - 0.95f * r);
    if (eff < 1.0f)
        eff = 1.0f;

    /* the exponential: 95 % of the way at eff, so the time constant is a third of it */
    tau = eff * 0.3333f;
    shape = 1.0f - vox_exp2f(-(float)dt / (tau * LN2));
    if (shape < 0.0f)
        shape = 0.0f;
    if (shape > 1.0f)
        shape = 1.0f;

    /* at GLIDE 100 the voice never quite arrives: it keeps creeping towards the note */
    if (glide_amt >= 100u)
        shape *= 0.93f;

    g->t = shape;
    return g->from + (g->to - g->from) * shape;
}
