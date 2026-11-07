/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the corrector.
 *
 * It answers one question every block: what pitch should the voice be sounding right now?
 *
 * The steps:
 *   1. the detected pitch is turned into a note (a MIDI number)
 *   2. the note is snapped to the nearest note of the scale
 *   3. the travel to that note is the glide's work (vox_glide.c)
 *   4. HUMANIZE adds a slow drift to the corrected pitch
 *   5. FLEX decides how much of the singer's own pitch survives the correction
 *
 * An unvoiced input never snaps: the corrector hands the pitch back as it was and clears the note.
 */
#include "vox.h"
#include "vox_math.h"

#define HUMANIZE_VIBRATO_HZ 5.2f        /* the drift's rate: a slow vibrato */
#define HUMANIZE_CENTS 22.0f            /* how far the drift goes at HUMANIZE 100 */

/* the corrector's reset */
void vox_correct_init(vox_correct_t *c)
{
    c->target = 0.0f;
    c->cur = 0.0f;
    c->bend = 0.0f;
    c->note = -1;
    c->octave = -1;
    c->active = 0;
    c->last_voiced_ms = 0;
    c->seed = 0x5EED1234u;
    vox_glide_init(&c->glide);
}

/* the nearest note of the scale to the given MIDI note.
 * The scale is a set of pitch classes; the note's class is checked, and the nearest octave's member is
 * taken when the class is not in the scale. */
static int snap_to_scale(const vox_scale_t *scale, int midi)
{
    int steps[12];
    int n, i, best = midi, bestd = 99;

    n = vox_scale_steps(scale, steps);
    if (n <= 0)
        return midi;

    /* the candidates: every member of the scale, in the neighbourhood of the note */
    for (i = 0; i < n; i++) {
        int base = scale->root + steps[i];
        int cand;
        for (cand = base - 12; cand <= base + 12; cand += 12) {
            int dist = cand - midi;
            if (dist < 0)
                dist = -dist;
            if (dist < bestd) {
                bestd = dist;
                best = cand;
            }
        }
    }
    /* the base itself: the pitch class must match one of the scale's */
    {
        int ok = 0;
        int pc = ((midi % 12) + 12) % 12;
        for (i = 0; i < n; i++)
            if ((((scale->root + steps[i]) % 12) + 12) % 12 == pc)
                ok = 1;
        if (ok)
            best = midi;
    }
    if (best < 0)
        best = 0;
    if (best > 127)
        best = 127;
    return best;
}

/* the corrector's target for the detected pitch */
float vox_correct_target(vox_correct_t *c, const vox_correct_param_t *p, const vox_pitch_t *in, uint32_t now_ms)
{
    float target, detected, out;
    int midi, snapped;

    /* the input's own pitch */
    detected = in->f0 > 0.0f ? in->f0 : 0.0f;

    /* nothing to correct: the voice is not there */
    if (!in->voiced || detected <= 0.0f) {
        c->active = 0;
        c->note = -1;
        c->octave = -1;
        c->target = 0.0f;
        c->cur = 0.0f;
        c->bend = 0.0f;
        return detected;
    }
    c->active = 1;
    c->last_voiced_ms = now_ms;

    /* the note the singer is on, and the note of the scale it belongs to */
    midi = in->note >= 0 ? (int)in->note : (int)(vox_midi_of(detected) + 0.5f);
    snapped = snap_to_scale(&p->scale, midi);

    /* the octave shift is the instrument's, not the scale's */
    target = vox_hz_of((float)snapped + (float)p->octave * 12.0f);
    c->note = (int8_t)snapped;
    c->octave = (int8_t)(snapped / 12 - 1);

    /* a new note: the glide takes over */
    if (c->glide.to <= 0.0f || vox_fabsf(c->glide.to - target) > 0.5f || !c->glide.moving) {
        float from = c->cur > 0.0f ? c->cur : detected;
        vox_glide_start(&c->glide, from, target, now_ms);
    }

    /* the glide's current pitch */
    out = vox_glide_at(&c->glide, p->retune, p->glide, now_ms);
    c->cur = out;
    c->target = target;

    /* HUMANIZE: a slow drift of a few cents, so a held note is not a machine */
    if (p->humanize > 0.0f) {
        float amt = (float)p->humanize * (1.0f / 100.0f);
        float t = (float)(now_ms % 60000u) * (1.0f / 1000.0f);
        float drift = vox_sin_turns(t * HUMANIZE_VIBRATO_HZ * (1.0f / 60.0f)) * HUMANIZE_CENTS * amt;
        float wander = (vox_randf(&c->seed) - 0.5f) * HUMANIZE_CENTS * amt * 0.2f;
        out = vox_hz_of(vox_midi_of(out) + (drift + wander) * (1.0f / 100.0f));
    }

    /* FLEX: how much of the singer's own pitch survives the correction.
     * At 0 the output is the corrected note exactly; at 100 it is the singer's own pitch. */
    if (p->flex > 0.0f) {
        float f = (float)p->flex * (1.0f / 100.0f);
        out = out + (detected - out) * f;
    }

    c->bend = vox_semitones(detected, out);
    return out;
}
