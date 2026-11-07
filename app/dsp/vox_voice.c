/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: a voice.
 *
 * A voice is the shifter and the formant filter together: the pitch moves (so the voice is a harmony of
 * the lead) and the tract moves (so the voice sounds like a different size of singer), and the two never
 * touch each other.
 *
 * The voice produces a dry mono signal; the mixer (vox_mixer.c) owns the level, the pan and the sends.
 */
#include "vox.h"
#include "vox_math.h"

/* the voice's reset: the shifter's ring and accumulator come from the caller's pool */
void vox_voice_init(vox_voice_t *v, const vox_buffers_t *bufs, uint32_t index)
{
    uint32_t i;

    v->ring = (bufs && index < VOX_MAX_VOICES) ? bufs->ring[index] : 0;
    v->ola = (bufs && index < VOX_MAX_VOICES) ? bufs->ola[index] : 0;

    /* the shifter's own reset: the clocks, the alignment and the tail */
    vox_shift_init(v);
    vox_formant_init(v);

    if (v->ring)
        for (i = 0; i < VOX_PSOLA_RING; i++)
            v->ring[i] = 0.0f;
    if (v->ola)
        for (i = 0; i < VOX_OLA_LEN; i++)
            v->ola[i] = 0.0f;
    for (i = 0; i < VOX_FORMANT_ORDER; i++) {
        v->lpc_hist[i] = 0.0f;
        v->lpc_state[i] = 0.0f;
    }
    for (i = 0; i < VOX_BLOCK; i++)
        v->resid[i] = 0.0f;
    v->pan_l = 1.0f;
    v->pan_r = 1.0f;
    v->level = 1.0f;
}

/* the voice's dry output for a block */
void vox_voice_render(vox_voice_t *v, const float *in, uint32_t n, float period, float semitones, float cents,
                      float formant_semitones)
{
    if (n > VOX_BLOCK)
        n = VOX_BLOCK;

    /* the pitch first */
    vox_shift_process(v, in, n, period, semitones, cents, v->out);

    /* then the tract: the formants move, the pitch does not */
    if (formant_semitones > 0.01f || formant_semitones < -0.01f)
        vox_formant_shift(v, v->out, n, formant_semitones, v->out);
}
