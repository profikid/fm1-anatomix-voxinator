/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the input's conditioning.
 *
 * The stage between the microphone's samples and the engine: the gain, the noise gate and the high-pass,
 * plus the meter the interface draws.
 *
 * The gate is the delicate part. A gate that opens on a single loud sample chatters, and a gate that never
 * closes lets the room's noise through. So the envelope follower has a fast attack and a slow release, and
 * the gate's state changes only when the envelope has been on the right side of the threshold for a
 * little while (a hold).
 */
#include "vox.h"
#include "vox_math.h"

#define GATE_HOLD_MS 60u                /* how long the gate waits before it opens or closes */
#define HP_HZ 75.0f                     /* the high-pass's corner: the rumble under the voice */
#define ATTACK_MS 5.0f                  /* the envelope follower's attack */
#define RELEASE_MS 120.0f               /* and its release */

/* the input's reset */
void vox_input_init(vox_input_t *in)
{
    in->gain = 1.0f;
    in->hp_z[0] = 0.0f;
    in->hp_z[1] = 0.0f;
    in->env = 0.0f;
    in->gate_open = 0.0f;
    in->level = 0.0f;
    in->clip = 0.0f;
}

/* the input's conditioning */
void vox_input_process(vox_input_t *in, const float *x, float *out, uint32_t n, uint8_t gate_pct)
{
    float atk, rel, thresh, hp_a;
    uint32_t i;
    float hold = 0.0f;
    float peak = 0.0f;

    /* the gate's threshold: 0 % is a gate that only opens on a very loud signal, 100 % never closes */
    thresh = 0.0008f * (1.0f + (float)(100u - (gate_pct > 100u ? 100u : gate_pct)) * 0.02f);
    atk = 1.0f / (float)(VOX_SR * ATTACK_MS / 1000u);
    rel = 1.0f / (float)(VOX_SR * RELEASE_MS / 1000u);

    /* the high-pass: a one-pole difference against a one-pole low pass (a DC blocker plus a rumble filter) */
    hp_a = 1.0f - vox_exp2f(-2.0f * 3.1415927f * HP_HZ / (float)VOX_SR);

    for (i = 0; i < n; i++) {
        float s = x[i];
        float a;

        /* the level: the peak of the block so far */
        {
            float m = s < 0.0f ? -s : s;
            if (m > peak)
                peak = m;
        }

        /* the envelope follower: the rectified signal through the one-pole */
        a = s < 0.0f ? -s : s;
        in->env += (a - in->env) * (a > in->env ? atk : rel);

        /* the gate: it opens when the envelope is over the threshold, and closes when it is under for a
         * little while (the hold) */
        if (in->env > thresh) {
            hold = (float)GATE_HOLD_MS;
            in->gate_open = 1.0f;
        } else if (hold > 0.0f) {
            hold -= 1.0f;
        } else {
            in->gate_open = 0.0f;
        }

        /* the high-pass */
        in->hp_z[0] += hp_a * (s - in->hp_z[0]);
        s = s - in->hp_z[0];

        out[i] = s * in->gain * in->gate_open;
    }

    in->level = peak;
    in->clip = peak > 1.0f ? 1.0f : 0.0f;
}
