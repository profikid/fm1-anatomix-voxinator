/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the dynamics.
 *
 * The compressor and the de-esser. The compressor keeps the voice's level even, so the pitch detector sees
 * a steady signal; the de-esser takes the sibilance off so the sibilants do not spit.
 *
 * Both work on the signal in place and are cheap: one envelope follower and a few filters.
 */
#include "vox.h"
#include "vox_math.h"

#define DEESS_HZ 7000.0f                /* the sibilance's band */
#define DEESS_Q 0.0f                    /* the band-pass's resonance (0 = the widest) */

/* the dynamics' reset */
void vox_dyn_init(vox_dyn_t *d)
{
    d->thresh = 0.1f;
    d->env = 0.0f;
    d->gain = 1.0f;
    d->atk = 1.0f / (float)(VOX_SR * 5u / 1000u);
    d->rel = 1.0f / (float)(VOX_SR * 120u / 1000u);
    d->deess = 0.0f;
    d->deess_env = 0.0f;
    d->hp_z[0] = 0.0f;
    d->hp_z[1] = 0.0f;
}

/* the compressor and the de-esser, in place.
 * thresh_db is the threshold in dB, ratio the compression ratio (1 = none), makeup the output gain in
 * linear, deess_amount the de-esser's depth (0..1). */
void vox_dyn_process(vox_dyn_t *d, float *x, uint32_t n, float thresh_db, float ratio, float makeup,
                     float deess_amount)
{
    float thresh = vox_exp2f(thresh_db * (1.0f / 6.0206f));   /* dB to linear */
    float hp_a = 1.0f - vox_exp2f(-2.0f * 3.1415927f * DEESS_HZ / (float)VOX_SR);
    uint32_t i;

    if (thresh < 1e-4f)
        thresh = 1e-4f;
    if (ratio < 1.0f)
        ratio = 1.0f;

    for (i = 0; i < n; i++) {
        float s = x[i];
        float a = s < 0.0f ? -s : s;

        /* the envelope */
        d->env += (a - d->env) * (a > d->env ? d->atk : d->rel);

        /* the gain reduction: a soft knee above the threshold */
        if (d->env > thresh) {
            float over = d->env / thresh;             /* how far over the threshold, linear */
            float db = 20.0f * vox_log2f(over > 1e-6f ? over : 1e-6f);
            float want = -db * (1.0f - 1.0f / ratio);  /* the reduction, in dB */
            float g = vox_exp2f(want * (1.0f / 6.0206f));
            d->gain += (g - d->gain) * 0.25f;          /* the gain moves smoothly */
        } else {
            d->gain += (1.0f - d->gain) * 0.1f;        /* and it recovers */
        }

        /* the de-esser: the high band's envelope, and a reduction when it is hissing */
        {
            float hi = s - d->hp_z[0];
            d->hp_z[0] += hp_a * (s - d->hp_z[0]);
            {
                float h = hi < 0.0f ? -hi : hi;
                d->deess_env += (h - d->deess_env) * (h > d->deess_env ? 0.05f : 0.002f);
                if (d->deess_env > 0.08f && deess_amount > 0.0f) {
                    float amount = (d->deess_env - 0.08f) * (1.0f / 0.32f);
                    if (amount > 1.0f)
                        amount = 1.0f;
                    d->deess += (amount * deess_amount * 0.6f - d->deess) * 0.15f;
                } else {
                    d->deess += (0.0f - d->deess) * 0.05f;
                }
                /* the high band is taken off, the rest is not touched */
                s = s - hi * d->deess;
            }
        }

        x[i] = s * d->gain * makeup;
    }
}
