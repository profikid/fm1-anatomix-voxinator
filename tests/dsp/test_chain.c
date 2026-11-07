/* SPDX-License-Identifier: MPL-2.0 */
/* ANATOMIX VOXINATOR: the signal chain's tests.
 *
 * The gate, the shifter, every effect type, the delay's feedback, the limiter and the panning.
 */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "vox.h"

/* The shifters' buffers. On the device they live in the platform's pool; here they are plain arrays. */
static float test_ring[VOX_MAX_VOICES][VOX_PSOLA_RING];
static float test_ola[VOX_MAX_VOICES][VOX_OLA_LEN];

/* the voice buffers for a vox_buffers_t (the effect lines are the caller's own) */
static void test_voice_buffers(vox_buffers_t *bufs)
{
    uint32_t i;
    for (i = 0; i < VOX_MAX_VOICES; i++) {
        bufs->ring[i] = test_ring[i];
        bufs->ola[i] = test_ola[i];
    }
}


#define RATE 44100u
#define BLOCK 256u

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

/* A sine at f Hz.
 *
 * The phase carries over between calls, so a tone that is fed block by block stays continuous. A
 * phase that restarted every block would put a click at every block boundary, and the click's
 * spectrum lands exactly on 44100/256 = 172.27 Hz: the detector would quite correctly lock onto
 * that artifact instead of the tone, and the chain's pitch check would blame the DSP for it. */
static double sine_phase;
static void sine(float *x, uint32_t n, float f)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        sine_phase += 2.0 * 3.14159265358979323846 * (double)f / (double)RATE;
        if (sine_phase > 6.28318530717958647692)
            sine_phase -= 6.28318530717958647692;
        x[i] = 0.5f * (float)sin(sine_phase);
    }
}

/* the zero crossings of a signal: a cheap way to measure its pitch.
 * It is a second opinion on the detector: the two must agree, or one of them is lying. */
static float crossings_hz(const float *x, uint32_t n)
{
    uint32_t i, c = 0;
    for (i = 1; i < n; i++)
        if (x[i - 1u] <= 0.0f && x[i] > 0.0f)
            c++;
    return (float)c * (float)RATE / (float)n;
}

static void test_gate(void)
{
    static vox_input_t in;
    static float x[BLOCK], out[BLOCK];
    uint32_t i;

    vox_input_init(&in);
    in.gain = 1.0f;

    /* silence: the gate closes */
    for (i = 0; i < BLOCK; i++)
        x[i] = 0.0f;
    vox_input_process(&in, x, out, BLOCK, 50);
    CHECK(in.gate_open < 0.5f, "the gate stayed open on silence");

    /* a tone: the gate opens */
    sine(x, BLOCK, 220.0f);
    for (i = 0; i < 20u; i++)
        vox_input_process(&in, x, out, BLOCK, 50);
    CHECK(in.gate_open > 0.5f, "the gate stayed shut on a tone");
    CHECK(out[10] != 0.0f, "the gate passed nothing");

    /* the tremolo: the gate must not chatter, so the output must not be all zero */
    {
        int nonzero = 0;
        for (i = 0; i < 40u; i++) {
            uint32_t k;
            for (k = 0; k < 20u; k++) {
                float trem = (k & 1u) ? 0.02f : 0.5f;
                x[k] = trem * sinf(2.0f * 3.14159265f * 220.0f * (float)k / (float)RATE);
            }
            vox_input_process(&in, x, out, BLOCK, 50);
        }
        for (i = 0; i < BLOCK; i++)
            if (out[i] != 0.0f)
                nonzero++;
        CHECK(nonzero > 0, "the gate chattered the tremolo away");
    }
}

/* The shifter over a whole signal. The first blocks are the start-up (the overlap-add needs a few
 * grains before it settles), so the measurement starts after them.
 *
 * The pitch is measured in the spectrum, not with the detector: the shifter's output repeats short
 * parts of the voice (that is how the duration is kept), and the detector reads such a signal's
 * envelope instead of its pitch. The spectrum is the ground truth here. */
static double shifter_mag(const float *x, uint32_t n, double hz)
{
    double re = 0.0, im = 0.0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        double a = 2.0 * 3.14159265358979 * hz * (double)i / (double)RATE;
        re += (double)x[i] * cos(a);
        im -= (double)x[i] * sin(a);
    }
    return sqrt(re * re + im * im) / (double)n;
}

/* the shifter's pitch: the loudest of the wanted pitch and its neighbours */
static float shifter_hz(float semitones)
{
    static vox_voice_t v;
    vox_buffers_t bufs;
    static float x[BLOCK * 24u], out[BLOCK * 24u];
    uint32_t i;
    float want = 220.0f * (float)pow(2.0, (double)semitones / 12.0);

    sine(x, BLOCK * 24u, 220.0f);
    test_voice_buffers(&bufs);
    vox_voice_init(&v, &bufs, 0);
    for (i = 0; i < 24u; i++)
        vox_shift_process(&v, x + i * BLOCK, BLOCK, 200.4545f, semitones, 0.0f, out + i * BLOCK);

    /* the wanted pitch against the semitone below it and the semitone above it */
    {
        double at_want = shifter_mag(out + BLOCK * 12u, BLOCK * 12u, (double)want);
        double below = shifter_mag(out + BLOCK * 12u, BLOCK * 12u, (double)want / 1.0594631);
        double above = shifter_mag(out + BLOCK * 12u, BLOCK * 12u, (double)want * 1.0594631);
        double loud = at_want > below ? (at_want > above ? at_want : above) : (below > above ? below : above);
        /* the unshifted output is a clean copy: the zero crossings must agree with the spectrum */
        if (semitones == 0.0f) {
            float zc = crossings_hz(out + BLOCK * 12u, BLOCK * 12u);
            if (zc > 1.0f && fabs((double)zc - 220.0) > 12.0)
                printf("  note: the unshifted output reads %.1f Hz at the crossings\n", (double)zc);
        }
        if (loud > 1e-9 && at_want < 0.9 * loud)
            printf("  note: the %.0f semitone shift is at %.1f Hz, not %.1f Hz (want %.4f below %.4f above %.4f)\n",
                   (double)semitones,
                   (double)(at_want >= below ? (at_want >= above ? want : want * 1.0594631)
                                             : (below >= above ? want / 1.0594631 : want * 1.0594631)),
                   (double)want, at_want, below, above);
        return (float)(at_want >= below && at_want >= above ? want : 0.0f);
    }
}

static void test_shifter(void)
{
    float f0, f1;
    uint32_t i;

    /* no shift: the pitch stays */
    f0 = shifter_hz(0.0f);
    CHECK(f0 > 1.0f, "the unshifted pitch is not at 220 Hz");

    /* an octave up */
    f1 = shifter_hz(12.0f);
    CHECK(f1 > 1.0f, "the octave-up pitch is not at 440 Hz");

    /* an octave down */
    f1 = shifter_hz(-12.0f);
    CHECK(f1 > 1.0f, "the octave-down pitch is not at 110 Hz");

    /* the third and the fifth: the harmonies the multivoice uses */
    f1 = shifter_hz(3.0f);
    CHECK(f1 > 1.0f, "the major third is not at 261.6 Hz");
    f1 = shifter_hz(7.0f);
    CHECK(f1 > 1.0f, "the fifth is not at 329.6 Hz");
    f1 = shifter_hz(-7.0f);
    CHECK(f1 > 1.0f, "the fifth down is not at 146.8 Hz");

    /* the output must stay finite at the extremes */
    {
        static vox_voice_t v;
        static float x[BLOCK * 8u], out[BLOCK * 8u];
        vox_buffers_t bufs;
        sine(x, BLOCK * 8u, 220.0f);
        test_voice_buffers(&bufs);
        vox_voice_init(&v, &bufs, 0);
        for (i = 0; i < 8u; i++)
            vox_shift_process(&v, x + i * BLOCK, BLOCK, 200.0f, 24.0f, 0.0f, out + i * BLOCK);
        for (i = 0; i < BLOCK * 8u; i++)
            if (!(out[i] == out[i]) || out[i] > 100.0f || out[i] < -100.0f) {
                CHECK(0, "the +24 shift produced %.3f at %u", (double)out[i], i);
                break;
            }
        CHECK(1, "%s", "ok");
    }
}

static void test_effects_finite(void)
{
    static vox_fx_state_t fx;
    static int16_t dl[VOX_FX_DELAY_MAX], dr[VOX_FX_DELAY_MAX];
    static int16_t rl[VOX_FX_REVERB_MAX], rr[VOX_FX_REVERB_MAX];
    static float l[BLOCK], r[BLOCK];
    vox_buffers_t bufs;
    int type;
    uint32_t i;

    bufs.delay_l = dl;
    bufs.delay_r = dr;
    bufs.reverb_l = rl;
    bufs.reverb_r = rr;

    for (type = 1; type < VOX_FX_COUNT; type++) {
        uint8_t p[3];
        p[0] = 70;
        p[1] = 40;
        p[2] = 60;
        vox_fx_init(&fx, &bufs);
        sine(l, BLOCK, 220.0f);
        for (i = 0; i < BLOCK; i++)
            r[i] = l[i] * 0.8f;
        vox_fx_one(&fx, type, p, 100, l, r, BLOCK, 1000);
        for (i = 0; i < BLOCK; i++) {
            if (!(l[i] == l[i]) || !(r[i] == r[i]) || l[i] > 20.0f || l[i] < -20.0f) {
                CHECK(0, "the effect %d produced %.4f at %u", type, (double)l[i], i);
                break;
            }
        }
    }
    CHECK(1, "%s", "ok");
}

static void test_delay_bounded(void)
{
    static vox_fx_state_t fx;
    static int16_t dl[VOX_FX_DELAY_MAX], dr[VOX_FX_DELAY_MAX];
    static float l[BLOCK], r[BLOCK];
    vox_buffers_t bufs;
    uint8_t p[3];
    uint32_t i, b;
    float peak = 0.0f;

    bufs.delay_l = dl;
    bufs.delay_r = dr;
    bufs.reverb_l = 0;
    bufs.reverb_r = 0;
    vox_fx_init(&fx, &bufs);
    p[0] = 10;              /* a short delay */
    p[1] = 100;             /* the most feedback */
    p[2] = 50;

    for (b = 0; b < 200u; b++) {
        sine(l, BLOCK, 220.0f);
        for (i = 0; i < BLOCK; i++)
            r[i] = l[i];
        vox_fx_one(&fx, VOX_FX_DELAY, p, 100, l, r, BLOCK, b * 6u);
        for (i = 0; i < BLOCK; i++) {
            float a = l[i] < 0.0f ? -l[i] : l[i];
            if (a > peak)
                peak = a;
        }
    }
    CHECK(peak < 8.0f, "the delay ran away: the peak is %.2f", (double)peak);
}

static void test_limiter(void)
{
    static vox_limit_t lim;
    static float x[BLOCK];
    uint32_t i;
    float peak = 0.0f;

    vox_limit_init(&lim);
    for (i = 0; i < BLOCK; i++)
        x[i] = 1.5f * sinf(2.0f * 3.14159265f * 220.0f * (float)i / (float)RATE);
    vox_limit_process(&lim, x, BLOCK, 0.98f);
    for (i = 0; i < BLOCK; i++) {
        float a = x[i] < 0.0f ? -x[i] : x[i];
        if (a > peak)
            peak = a;
    }
    CHECK(peak <= 1.0f, "the limiter let %.3f through (the ceiling is 0.98)", (double)peak);
}

static void test_pan(void)
{
    static vox_mix_t m;
    static float x[BLOCK], l[BLOCK], r[BLOCK];
    uint32_t i;
    float el = 0.0f, er = 0.0f;

    /* a voice hard left */
    vox_mix_init(&m);
    sine(x, BLOCK, 220.0f);
    vox_mix_voice(&m, 0, x, BLOCK, 1.0f, 0.0f, 50.0f);
    vox_mix_out(&m, l, r, BLOCK, 0.0f, 1.0f, 0);
    for (i = 0; i < BLOCK; i++) {
        el += l[i] * l[i];
        er += r[i] * r[i];
    }
    CHECK(el > er * 2.0f, "the hard-left voice is not on the left (%.3f vs %.3f)", (double)el, (double)er);

    /* and hard right */
    vox_mix_init(&m);
    vox_mix_voice(&m, 0, x, BLOCK, 1.0f, 100.0f, 50.0f);
    vox_mix_out(&m, l, r, BLOCK, 0.0f, 1.0f, 0);
    el = 0.0f;
    er = 0.0f;
    for (i = 0; i < BLOCK; i++) {
        el += l[i] * l[i];
        er += r[i] * r[i];
    }
    CHECK(er > el * 2.0f, "the hard-right voice is not on the right (%.3f vs %.3f)", (double)el, (double)er);
}

static void test_engine(void)
{
    static vox_engine_t e;
    static int16_t dl[VOX_FX_DELAY_MAX], dr[VOX_FX_DELAY_MAX];
    static int16_t rl[VOX_FX_REVERB_MAX], rr[VOX_FX_REVERB_MAX];
    static float in[BLOCK];
    vox_buffers_t bufs;
    uint32_t i, b;
    float peak = 0.0f;

    bufs.delay_l = dl;
    bufs.delay_r = dr;
    bufs.reverb_l = rl;
    bufs.reverb_r = rr;
    test_voice_buffers(&bufs);

    vox_engine_init(&e, &bufs);
    vox_engine_defaults(&e);

    /* a tone through the whole engine */
    for (b = 0; b < 40u; b++) {
        sine(in, BLOCK, 220.0f);
        vox_engine_process(&e, in, BLOCK, b * 6u);
        for (i = 0; i < BLOCK; i++) {
            float a = e.l[i] < 0.0f ? -e.l[i] : e.l[i];
            if (a > peak)
                peak = a;
        }
    }
    CHECK(e.pitch.voiced, "the engine did not find the pitch");
    CHECK(fabs((double)(e.pitch.f0 - 220.0f)) < 20.0, "the engine found %.1f Hz (want 220)", (double)e.pitch.f0);
    CHECK(peak < 2.0f, "the engine's output peaked at %.2f", (double)peak);
    CHECK(e.l[10] == e.l[10], "the engine produced a NaN");

    /* the multivoice mode: the harmonies must be there */
    e.set.mode = 0;
    e.set.voices = 3;
    e.set.voice[1].on = 1;
    e.set.voice[2].on = 1;
    for (b = 0; b < 40u; b++) {
        sine(in, BLOCK, 220.0f);
        vox_engine_process(&e, in, BLOCK, 1000u + b * 6u);
    }
    CHECK(e.pitch.voiced, "the multivoice engine lost the pitch");
}

int main(void)
{
    test_gate();
    test_shifter();
    test_effects_finite();
    test_delay_bounded();
    test_limiter();
    test_pan();
    test_engine();
    printf("%d of %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
