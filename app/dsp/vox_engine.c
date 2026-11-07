/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the engine.
 *
 * The order of one block:
 *   1. the input's conditioning (the gain, the gate, the high-pass)
 *   2. the pitch detection
 *   3. the correction (GLIDE) or the harmony plan (MULTIVOICE)
 *   4. the voices: each one is the shifter and the formant filter together
 *   5. the mixer: the voices into the wet bus, the original into the dry bus
 *   6. the effects chain
 *   7. the limiter
 *
 * The two modes differ in what the voices are and in how the dry bus is used:
 *   GLIDE       the voice is the corrected one; the MIX is the corrected against the raw
 *   MULTIVOICE  the voices are the original and the harmonies; the original is the dry bus
 */
#include "vox.h"
#include "vox_math.h"

/* the buffers the engine needs: the lengths, so the app can put them in the platform's pool */
void vox_buffers_sizes(uint32_t *delay_len, uint32_t *reverb_len, uint32_t *ring_len, uint32_t *ola_len)
{
    if (delay_len)
        *delay_len = VOX_FX_DELAY_MAX;
    if (reverb_len)
        *reverb_len = VOX_FX_REVERB_MAX;
    if (ring_len)
        *ring_len = VOX_PSOLA_RING;
    if (ola_len)
        *ola_len = VOX_OLA_LEN;
}

/* the engine's defaults: the CLEAN STUDIO preset */
void vox_engine_defaults(vox_engine_t *e)
{
    uint32_t i;

    for (i = 0; i < sizeof e->set; i++)
        ((uint8_t *)&e->set)[i] = 0;
    e->set.magic = VOX_MAGIC;
    e->set.version = VOX_PRESET_VERSION;
    e->set.mode = 1;                       /* GLIDE */
    e->set.in_gain = 70;
    e->set.in_gate = 30;
    e->set.retune = 30;
    e->set.glide = 25;
    e->set.humanize = 25;
    e->set.flex = 65;
    e->set.mix = 100;
    e->set.voices = 1;
    e->set.spread = 50;
    e->set.detune = 20;
    e->set.character = 30;
    e->set.harmony = 7;
    e->set.chaos = 0;
    e->set.scale.root = 0;
    e->set.scale.scale = VOX_SCALE_CHROMATIC;
    e->set.formant = 0;
    e->set.formant_amt = 100;
    for (i = 0; i < VOX_MAX_VOICES; i++) {
        e->set.voice[i].on = (uint8_t)(i == 0u ? 1u : 0u);
        e->set.voice[i].level = 100;
        e->set.voice[i].pan = 50;
        e->set.voice[i].width = 50;
        e->set.voice[i].detune = 20;
    }
}

/* the engine's reset */
void vox_engine_init(vox_engine_t *e, const vox_buffers_t *bufs)
{
    uint32_t i;

    vox_input_init(&e->input);
    vox_detect_init(&e->detect);
    vox_correct_init(&e->correct);
    vox_harm_init(&e->harm);
    for (i = 0; i < VOX_MAX_VOICES; i++)
        vox_voice_init(&e->voice[i], bufs, i);
    vox_dyn_init(&e->dyn);
    vox_fx_init(&e->fx, bufs);
    vox_mix_init(&e->mix);
    vox_limit_init(&e->limit);
    vox_limit_init(&e->limit_r);

    e->pitch.f0 = 0.0f;
    e->pitch.clarity = 0.0f;
    e->pitch.rms = 0.0f;
    e->pitch.period = 0.0f;
    e->pitch.note = -1;
    e->pitch.octave = -1;
    e->pitch.voiced = 0;
    e->target_f0 = 0.0f;
    e->level = 0.0f;
    e->breathe = 0.0f;
    e->now_ms = 0;
    e->seed = 0x13579BDFu;
    e->dirty = 0;
}

/* the engine's block */
void vox_engine_process(vox_engine_t *e, const float *in, uint32_t n, uint32_t now_ms)
{
    vox_correct_param_t cp;
    vox_voice_param_t vp;
    uint32_t i, v;
    float mixf;

    if (n > VOX_BLOCK)
        n = VOX_BLOCK;
    e->now_ms = now_ms;

    /* 1. the input's conditioning */
    e->input.gain = (float)e->set.in_gain * 0.02f;   /* 0..100 % is 0..2 */
    vox_input_process(&e->input, in, e->block, n, e->set.in_gate);

    /* 2. the detection */
    vox_detect(&e->detect, e->block, n);
    e->pitch.f0 = e->detect.f0;
    e->pitch.clarity = e->detect.clarity;
    e->pitch.rms = e->detect.rms;
    e->pitch.period = e->detect.period;
    e->pitch.note = e->detect.note;
    e->pitch.octave = e->detect.octave;
    e->pitch.voiced = e->detect.voiced;

    /* 3. the corrector's parameters */
    cp.retune = e->set.retune;
    cp.glide = e->set.glide;
    cp.humanize = e->set.humanize;
    cp.flex = e->set.flex;
    cp.mix = e->set.mix;
    cp.octave = e->set.octave;
    cp.scale = e->set.scale;

    /* the target: the correction's own work */
    e->target_f0 = vox_correct_target(&e->correct, &cp, &e->pitch, now_ms);

    /* the harmony's parameters */
    e->harm.count = e->set.voices;
    e->harm.spread = e->set.spread;
    e->harm.detune = e->set.detune;
    e->harm.character = e->set.character;
    e->harm.humanize = e->set.humanize;
    e->harm.chaos = e->set.chaos;
    e->harm.harmony = e->set.harmony;
    vox_harm_plan(&e->harm, &e->set.scale, e->pitch.note, e->pitch.octave, now_ms);

    /* 4. the voices */
    vox_mix_init(&e->mix);
    for (v = 0; v < VOX_MAX_VOICES; v++) {
        float semis, formant;
        float level;

        vp = e->set.voice[v];
        if (!vp.on || v >= (uint32_t)e->set.voices)
            continue;
        if (e->set.mode == 0u) {
            /* MULTIVOICE: the lead is the original, the others are the harmonies */
            semis = (v == 0u) ? 0.0f : vox_harm_offset(&e->harm, v, (float)e->set.detune, now_ms);
        } else {
            /* GLIDE: the lead is the corrected voice, the others are the harmonies */
            semis = (v == 0u) ? e->correct.bend : vox_harm_offset(&e->harm, v, (float)e->set.detune, now_ms);
        }
        /* the formant: the preset's offset, in the voice's own semitones */
        formant = (float)vp.formant;
        if (v == 0u)
            formant = (float)e->set.formant;
        formant *= (float)e->set.formant_amt * 0.01f;

        vox_voice_render(&e->voice[v], e->block, n, e->pitch.period, semis, (float)vp.fine, formant);

        /* the voice's level: the character's control takes the lead's share */
        level = (float)vp.level * 0.01f;
        if (e->set.mode == 0u && v == 0u)
            level = 1.0f;   /* the original is the dry bus; it is not counted twice */
        vox_mix_voice(&e->mix, v, e->voice[v].out, n, level, (float)vp.pan, (float)vp.width);
    }

    /* 5. the buses */
    vox_mix_dry(&e->mix, e->block, n, 1.0f);
    vox_mix_out(&e->mix, e->l, e->r, n, 0.0f, 1.0f, now_ms);

    /* GLIDE: the corrected voice against the raw one */
    if (e->set.mode == 1u) {
        mixf = (float)e->set.mix * 0.01f;
        if (mixf < 0.0f)
            mixf = 0.0f;
        if (mixf > 1.0f)
            mixf = 1.0f;
        for (i = 0; i < n; i++) {
            e->l[i] = e->l[i] * mixf + e->mix.dry_l[i] * (1.0f - mixf);
            e->r[i] = e->r[i] * mixf + e->mix.dry_r[i] * (1.0f - mixf);
        }
    }

    /* 6. the effects chain */
    vox_fx_process(&e->fx, e->set.fx, e->l, e->r, n, now_ms);

    /* 7. the limiter */
    vox_limit_process(&e->limit, e->l, n, 0.98f);
    vox_limit_process(&e->limit_r, e->r, n, 0.98f);

    /* what the display reads */
    e->level = e->input.level;
    e->breathe += 0.0015f;
    if (e->breathe > 1.0f)
        e->breathe -= 1.0f;
}
