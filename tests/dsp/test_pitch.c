/* SPDX-License-Identifier: MPL-2.0 */
/* ANATOMIX VOXINATOR: the pitch brain's tests.
 *
 * The harness is the family's: four lines, no framework.
 */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "vox.h"

#define RATE 44100u
#define BLOCK 256u

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

/* a sine at f Hz */
static void sine(float *x, uint32_t n, float f, float phase)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        x[i] = 0.5f * sinf((float)(2.0 * 3.14159265358979 * (double)f * (double)i / (double)RATE) + (double)phase);
}

/* the detector over a whole signal: the blocks are fed one after another, and the last result is returned */
static int run_detect(vox_detect_t *d, const float *x, uint32_t n, uint32_t block)
{
    uint32_t off;
    int voiced = 0;
    for (off = 0; off + block <= n; off += block)
        voiced = vox_detect(d, x + off, block);
    return voiced;
}

/* the detected pitch, in cents off the target */
static float cents_off(float got, float want)
{
    if (got <= 0.0f)
        return 99999.0f;
    return 1200.0f * log2((double)got / (double)want);
}

static void test_detect_sine(void)
{
    static const float freq[4] = {110.0f, 220.0f, 440.0f, 880.0f};
    uint32_t k;

    for (k = 0; k < 4u; k++) {
        static float x[BLOCK * 40u];
        static vox_detect_t d;
        int voiced;

        vox_detect_init(&d);
        sine(x, BLOCK * 40u, freq[k], 0.0f);
        voiced = run_detect(&d, x, BLOCK * 40u, BLOCK);
        CHECK(voiced, "%.0f Hz: not voiced", freq[k]);
        CHECK(fabs((double)cents_off(d.f0, freq[k])) < 60.0,
              "%.0f Hz: detected %.1f Hz (%.0f cents off)", freq[k], (double)d.f0, (double)cents_off(d.f0, freq[k]));
        CHECK(d.clarity > 0.5f, "%.0f Hz: the clarity is only %.2f", freq[k], (double)d.clarity);
        CHECK(d.note >= 0, "%.0f Hz: no note", freq[k]);
    }
}

static void test_detect_noise(void)
{
    static float x[BLOCK * 40u];
    static vox_detect_t d;
    uint32_t i;
    uint32_t seed = 12345u;
    int voiced;

    for (i = 0; i < BLOCK * 40u; i++) {
        seed = seed * 1103515245u + 12345u;
        x[i] = (float)((double)(seed >> 8) / 8388608.0 - 1.0) * 0.3f;
    }
    vox_detect_init(&d);
    voiced = run_detect(&d, x, BLOCK * 40u, BLOCK);
    CHECK(!voiced, "white noise was called a voice (%.1f Hz)", (double)d.f0);
    CHECK(d.clarity < 0.6f, "white noise's clarity is %.2f", (double)d.clarity);
}

static void test_detect_vibrato(void)
{
    static float x[BLOCK * 60u];
    static vox_detect_t d;
    uint32_t i;
    float min = 1e9f, max = -1e9f;
    int voiced;

    /* a 220 Hz note with a +/- 50 cent vibrato at 5 Hz */
    for (i = 0; i < BLOCK * 60u; i++) {
        double t = (double)i / (double)RATE;
        double vib = 0.5 * sin(2.0 * 3.14159265358979 * 5.0 * t);
        double f = 220.0 * pow(2.0, vib / 12.0);
        x[i] = 0.5f * (float)sin(2.0 * 3.14159265358979 * f * t);
    }
    vox_detect_init(&d);
    voiced = run_detect(&d, x, BLOCK * 60u, BLOCK);
    CHECK(voiced, "the vibrato was not voiced");
    /* the pitch must stay near the note: the vibrato is 50 cents, so 200 cents is generous */
    for (i = 0; i < BLOCK * 60u; i += BLOCK) {
        float f;
        vox_detect_t dd = d;
        (void)dd;
        f = d.f0;
        if (f < min) min = f;
        if (f > max) max = f;
        if (i + BLOCK < BLOCK * 60u)
            vox_detect(&d, x + i + BLOCK, BLOCK);
    }
    CHECK(cents_off(d.f0, 220.0f) < 200.0f, "the vibrato's pitch drifted to %.1f Hz", (double)d.f0);
    CHECK(min > 100.0f && max < 500.0f, "the vibrato's range is %.1f..%.1f Hz", (double)min, (double)max);
}

static void test_detect_note_change(void)
{
    static float x[BLOCK * 60u];
    static vox_detect_t d;
    int voiced;

    /* 220 Hz for half a second, then 440 Hz */
    sine(x, BLOCK * 30u, 220.0f, 0.0f);
    sine(x + BLOCK * 30u, BLOCK * 30u, 440.0f, 0.0f);
    vox_detect_init(&d);
    voiced = run_detect(&d, x, BLOCK * 60u, BLOCK);
    CHECK(voiced, "the note change was not voiced");
    CHECK(cents_off(d.f0, 440.0f) < 80.0, "after the change the pitch is %.1f Hz (want 440)", (double)d.f0);
}

static void test_correct_snap(void)
{
    static vox_correct_t c;
    static vox_pitch_t in;
    vox_correct_param_t p;
    float target;

    vox_correct_init(&c);
    memset(&p, 0, sizeof p);
    p.retune = 100;
    p.glide = 0;
    p.humanize = 0;
    p.flex = 0;
    p.mix = 100;
    p.octave = 0;
    vox_scale_default(&p.scale, VOX_SCALE_CHROMATIC, 0);

    /* a 224 Hz note: the nearest semitone above A3 (220) */
    in.f0 = 224.0f;
    in.voiced = 1;
    in.note = 57;                       /* A3 */
    in.octave = 3;
    target = vox_correct_target(&c, &p, &in, 1000);
    CHECK(cents_off(target, 220.0f) < 10.0, "the corrector landed at %.1f Hz (want 220)", (double)target);

    /* the unvoiced input is never snapped */
    in.voiced = 0;
    in.f0 = 0.0f;
    target = vox_correct_target(&c, &p, &in, 1100);
    CHECK(target == 0.0f, "the unvoiced input was snapped to %.1f Hz", (double)target);
    CHECK(c.active == 0, "the unvoiced input left the corrector active");
}

static void test_correct_scale(void)
{
    static vox_correct_t c;
    static vox_pitch_t in;
    vox_correct_param_t p;
    float target;

    vox_correct_init(&c);
    memset(&p, 0, sizeof p);
    p.retune = 100;
    p.glide = 0;
    p.humanize = 0;
    p.flex = 0;
    p.octave = 0;
    /* the minor scale: the A should snap down to the Ab */
    vox_scale_default(&p.scale, VOX_SCALE_MINOR, 0);

    in.f0 = 220.0f;                    /* A3 */
    in.voiced = 1;
    in.note = 57;
    in.octave = 3;
    target = vox_correct_target(&c, &p, &in, 1000);
    CHECK(cents_off(target, 207.65f) < 20.0, "the minor scale landed at %.1f Hz (want 207.7)", (double)target);
}

static void test_glide_time(void)
{
    vox_glide_t g;
    uint32_t t;
    float slow, fast;

    /* a slow glide: it has not arrived after 100 ms */
    vox_glide_init(&g);
    vox_glide_start(&g, 220.0f, 440.0f, 0);
    slow = vox_glide_at(&g, 50, 50, 100);
    CHECK(slow < 400.0f && slow > 220.0f, "the glide is at %.1f Hz after 100 ms", (double)slow);

    /* a fast one: it has arrived after 100 ms */
    vox_glide_init(&g);
    vox_glide_start(&g, 220.0f, 440.0f, 0);
    fast = vox_glide_at(&g, 100, 0, 100);
    CHECK(fast > 400.0f, "the fast glide is at %.1f Hz after 100 ms", (double)fast);

    /* and the slow one arrives eventually */
    vox_glide_init(&g);
    vox_glide_start(&g, 220.0f, 440.0f, 0);
    t = 0;
    while (t < 5000u) {
        float v = vox_glide_at(&g, 50, 50, t);
        t += 50u;
        if (v > 430.0f)
            break;
    }
    CHECK(t < 5000u, "the glide never arrived");
}

static void test_formant_pitch_independence(void)
{
    /* a formant shift must not move the pitch: the same signal through the formant engine must detect
     * the same fundamental */
    static float x[BLOCK * 40u];
    static vox_detect_t d1, d2;
    static vox_voice_t v;
    static float out[BLOCK];
    uint32_t i;
    int voiced;

    sine(x, BLOCK * 40u, 220.0f, 0.0f);
    vox_detect_init(&d1);
    voiced = run_detect(&d1, x, BLOCK * 40u, BLOCK);
    CHECK(voiced, "the reference was not voiced");

    vox_detect_init(&d2);
    vox_voice_init(&v, 0, 0);
    for (i = 0; i < BLOCK * 40u; i += BLOCK) {
        vox_formant_shift(&v, x + i, BLOCK, 12.0f, out);
        vox_detect(&d2, out, BLOCK);
    }
    CHECK(d2.voiced, "the formant-shifted signal was not voiced");
    CHECK(cents_off(d2.f0, d1.f0) < 30.0,
          "a +12 formant shift moved the pitch from %.1f to %.1f Hz", (double)d1.f0, (double)d2.f0);
}

static void test_harmony_scale(void)
{
    static vox_harm_t h;
    vox_scale_t scale;
    int steps[12];
    int n;

    vox_harm_init(&h);
    h.count = 3;
    h.harmony = 7;
    vox_scale_default(&scale, VOX_SCALE_CHROMATIC, 0);
    vox_harm_plan(&h, &scale, 60, 4, 0);
    CHECK(h.interval[0] == 0, "the lead is not at the root");
    CHECK(h.count >= 1, "no voices planned");

    /* in the minor scale, the planned intervals must be in the scale */
    vox_scale_default(&scale, VOX_SCALE_MINOR, 0);
    vox_harm_plan(&h, &scale, 60, 4, 0);
    n = vox_scale_steps(&scale, steps);
    CHECK(n == 7, "the minor scale has %d steps", n);
    {
        int i;
        for (i = 1; i < 4; i++) {
            int pc = (((int)h.interval[i] % 12) + 12) % 12;
            int k, ok = 0;
            for (k = 0; k < n; k++)
                if (steps[k] == pc)
                    ok = 1;
            CHECK(ok, "the interval %d is not in the minor scale", (int)h.interval[i]);
        }
    }
}

int main(void)
{
    test_detect_sine();
    test_detect_noise();
    test_detect_vibrato();
    test_detect_note_change();
    test_correct_snap();
    test_correct_scale();
    test_glide_time();
    test_formant_pitch_independence();
    test_harmony_scale();
    printf("%d of %d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
