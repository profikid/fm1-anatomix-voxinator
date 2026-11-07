/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the output limiter.
 *
 * A fast peak limiter: the gain comes down as soon as the signal is over the ceiling and comes back up
 * slowly. There is no look-ahead, so the limiter can let a single peak through by a fraction of a dB; the
 * ceiling is set a little under full scale to make room for that.
 */
#include "vox.h"
#include "vox_math.h"

#define ATTACK_MS 1.0f                  /* the gain's fall: fast enough to catch a peak */
#define RELEASE_MS 120.0f               /* and its rise: slow enough not to pump */

/* the limiter's reset */
void vox_limit_init(vox_limit_t *l)
{
    l->env = 0.0f;
    l->gain = 1.0f;
    l->atk = 1.0f / (float)(VOX_SR * ATTACK_MS / 1000u);
    l->rel = 1.0f / (float)(VOX_SR * RELEASE_MS / 1000u);
    l->peak = 0.0f;
}

/* the output limiter, in place */
void vox_limit_process(vox_limit_t *l, float *x, uint32_t n, float ceiling)
{
    uint32_t i;
    float peak = 0.0f;

    if (ceiling <= 0.0f)
        ceiling = 0.98f;

    for (i = 0; i < n; i++) {
        float a = x[i] < 0.0f ? -x[i] : x[i];
        float need = 1.0f;

        if (a > peak)
            peak = a;

        /* the gain that brings THIS sample under the ceiling. The gain comes down at once (a peak must
         * not get through) and goes up slowly (so the limiter does not pump). */
        if (a > ceiling)
            need = ceiling / a;
        if (need < l->gain)
            l->gain = need;
        else
            l->gain += (need - l->gain) * l->rel;
        if (l->gain > 1.0f)
            l->gain = 1.0f;

        /* the envelope, for the display */
        l->env += (a - l->env) * (a > l->env ? l->atk : l->rel);

        x[i] *= l->gain;
    }
    l->peak = peak;
}
