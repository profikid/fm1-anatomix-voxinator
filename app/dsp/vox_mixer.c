/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the mixer.
 *
 * The voices are summed here. Each voice contributes to the dry mix (what the listener hears) and to the
 * wet mix (what the effect chain works on). The panning is an equal-power pair, and the width spreads a
 * voice's sides apart by a few samples so a harmony sits in the room rather than in one point.
 */
#include "vox.h"
#include "vox_math.h"

#define WIDTH_MAX_SAMPLES 12u           /* the widest a voice is spread: 2.7 ms at 44.1 kHz */

/* the mixer's reset */
void vox_mix_init(vox_mix_t *m)
{
    uint32_t i;
    for (i = 0; i < VOX_BLOCK; i++) {
        m->dry_l[i] = 0.0f;
        m->dry_r[i] = 0.0f;
        m->wet_l[i] = 0.0f;
        m->wet_r[i] = 0.0f;
    }
}

/* a voice's contribution: the wet bus */
void vox_mix_voice(vox_mix_t *m, uint32_t voice, const float *x, uint32_t n, float level, float pan,
                   float width)
{
    float p = pan * (1.0f / 100.0f);
    float gl, gr, spread;
    uint32_t i;

    if (p < 0.0f)
        p = 0.0f;
    if (p > 1.0f)
        p = 1.0f;

    /* the equal-power panning: at pan 0 the left is full and the right is silent, at pan 100 the other
     * way round, and in the middle both are at 0.707 */
    gl = level * vox_sin_turns((1.0f - p) * 0.25f);
    gr = level * vox_sin_turns(p * 0.25f);

    /* the width: a few samples between the sides */
    spread = (width * (1.0f / 100.0f)) * (float)WIDTH_MAX_SAMPLES;
    if (spread < 0.0f)
        spread = 0.0f;

    for (i = 0; i < n; i++) {
        uint32_t k = i;
        float lv, rv;
        if (i >= (uint32_t)spread)
            k = i - (uint32_t)spread;
        lv = x[k];
        rv = x[i];
        m->wet_l[i] += lv * gl;
        m->wet_r[i] += rv * gr;
    }
    (void)voice;
}

/* the dry bus: the original signal */
void vox_mix_dry(vox_mix_t *m, const float *x, uint32_t n, float level)
{
    uint32_t i;
    if (n > VOX_BLOCK)
        n = VOX_BLOCK;
    for (i = 0; i < n; i++) {
        m->dry_l[i] = x[i] * level;
        m->dry_r[i] = x[i] * level;
    }
}

/* the mixer's output: the dry and the wet, mixed into l and r */
void vox_mix_out(vox_mix_t *m, float *l, float *r, uint32_t n, float dry, float wet, uint32_t now_ms)
{
    uint32_t i;

    (void)now_ms;
    if (n > VOX_BLOCK)
        n = VOX_BLOCK;
    for (i = 0; i < n; i++) {
        l[i] = m->dry_l[i] * dry + m->wet_l[i] * wet;
        r[i] = m->dry_r[i] * dry + m->wet_r[i] * wet;
    }
}
