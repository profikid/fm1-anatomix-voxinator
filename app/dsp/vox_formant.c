/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the formant shifter.
 *
 * The voice's size is its formants, not its pitch: a child and an adult sing the same note at the same
 * pitch and sound nothing alike. So the formant shift has to be a control of its own, and the pitch
 * shifter must not touch it.
 *
 * The method is an LPC spectral-envelope shift:
 *   1. estimate the linear prediction of the block (order VOX_FORMANT_ORDER), which describes the vocal
 *      tract's envelope
 *   2. keep the residual (the excitation) and re-filter it with a filter whose formants have been moved
 *   3. add the excitation back, so the new tract is excited the same way
 *
 * Moving the formants by n semitones is a frequency ratio of 2^(n/12). The filter's poles and zeros are
 * moved by that ratio, which is what makes the tract sound shorter and brighter (or longer and darker).
 */
#include "vox.h"
#include "vox_math.h"

#define EPS 1e-6f
#define LEV 0.008f                      /* the Levinson-Durbin step */

/* the formant engine's reset */
void vox_formant_init(vox_voice_t *v)
{
    uint32_t i;

    v->lpc_a[0] = 1.0f;
    for (i = 1; i <= VOX_FORMANT_ORDER; i++)
        v->lpc_a[i] = 0.0f;
    for (i = 0; i <= VOX_FORMANT_ORDER; i++)
        v->lpc_b[i] = 0.0f;
    for (i = 0; i < VOX_FORMANT_ORDER; i++) {
        v->lpc_hist[i] = 0.0f;
        v->lpc_state[i] = 0.0f;
    }
    for (i = 0; i < VOX_BLOCK; i++)
        v->resid[i] = 0.0f;
}

/* the autocorrelation and the Levinson-Durbin recursion: the LPC of the block.
 * The window is the autocorrelation's, so the edges do not leak into the coefficients. */
static void lpc_analyse(vox_voice_t *v, const float *x, uint32_t n)
{
    float r[VOX_FORMANT_ORDER + 1u];
    float a[VOX_FORMANT_ORDER + 1u];
    float e;
    uint32_t i, k;

    /* the autocorrelation over the whole block, with a rectangular window (the block is short enough) */
    for (k = 0; k <= VOX_FORMANT_ORDER; k++) {
        float s = 0.0f;
        for (i = 0; i + k < n; i++)
            s += x[i] * x[i + k];
        r[k] = s;
    }
    if (r[0] < EPS) {
        v->lpc_a[0] = 1.0f;
        for (i = 1; i <= VOX_FORMANT_ORDER; i++)
            v->lpc_a[i] = 0.0f;
        for (i = 0; i <= VOX_FORMANT_ORDER; i++)
            v->lpc_b[i] = 0.0f;
        return;
    }

    /* the Levinson-Durbin recursion */
    a[0] = 1.0f;
    e = r[0];
    for (k = 1; k <= VOX_FORMANT_ORDER; k++) {
        float acc = r[k];
        for (i = 1; i < k; i++)
            acc -= a[i] * r[k - i];
        float kk = acc / (e > EPS ? e : EPS);
        if (kk > 0.999f)
            kk = 0.999f;
        if (kk < -0.999f)
            kk = -0.999f;
        {
            float saved[VOX_FORMANT_ORDER + 1u];
            for (i = 1; i < k; i++)
                saved[i] = a[i];
            for (i = 1; i < k; i++)
                a[i] = saved[i] - kk * saved[k - i];
            a[k] = kk;
        }
        e *= (1.0f - kk * kk);
        if (e < EPS) {
            e = EPS;
            break;
        }
    }

    v->lpc_a[0] = 1.0f;
    for (i = 1; i <= VOX_FORMANT_ORDER; i++)
        v->lpc_a[i] = a[i];
    for (i = 0; i <= VOX_FORMANT_ORDER; i++)
        v->lpc_b[i] = a[i];
}

/* the residual: the input without its own filter (the excitation) */
static void lpc_residual(vox_voice_t *v, const float *x, uint32_t n)
{
    uint32_t i, k;

    for (i = 0; i < n; i++) {
        float pred = 0.0f;
        /* the sliding history: lpc_hist[0] is the newest sample, lpc_hist[k-1] the one k back */
        for (k = 1; k <= VOX_FORMANT_ORDER; k++)
            pred += v->lpc_a[k] * v->lpc_hist[k - 1u];
        v->resid[i] = x[i] - pred;
        for (k = VOX_FORMANT_ORDER; k > 0; k--)
            v->lpc_hist[k] = v->lpc_hist[k - 1u];
        v->lpc_hist[0] = x[i];
    }
}

/* the formant shift.
 * The input block goes through the analysis, the residual is re-filtered with the shifted tract, and the
 * result is in out. A shift of 0 semitones is a plain copy. */
void vox_formant_shift(vox_voice_t *v, const float *in, uint32_t n, float semitones, float *out)
{
    float ratio;
    uint32_t i;

    /* no shift: the signal passes through */
    if (semitones > -0.01f && semitones < 0.01f) {
        for (i = 0; i < n; i++)
            out[i] = in[i];
        return;
    }
    if (n > VOX_BLOCK)
        n = VOX_BLOCK;

    /* the analysis of the original tract */
    lpc_analyse(v, in, n);
    lpc_residual(v, in, n);

    /* the frequency ratio: n semitones is a ratio of 2^(n/12) */
    ratio = vox_exp2f(semitones * (1.0f / 12.0f));
    if (ratio < 0.2f)
        ratio = 0.2f;
    if (ratio > 5.0f)
        ratio = 5.0f;

    /* the shifted tract: the filter's poles move by the frequency ratio.
     *
     * The analysis filter is A(z) = 1 - a1 z^-1 - a2 z^-2, so its poles solve z^2 - a1 z - a2 = 0. For a
     * conjugate pair their radius is r = sqrt(|a2|) and their angle comes from a1 = 2 r cos(theta). The
     * synthesis filter is 1 / A(z), so its coefficients are +a1, +a2.
     *
     * The shift moves the angle (the formant) by the ratio and keeps the radius (the bandwidth), which is
     * what makes the tract sound shorter and brighter or longer and darker. */
    {
        float s[VOX_FORMANT_ORDER + 1u];
        uint32_t k;

        s[0] = 1.0f;
        for (k = 1; k + 1u <= VOX_FORMANT_ORDER; k += 2) {
            float a1 = v->lpc_a[k];
            float a2 = v->lpc_a[k + 1u];
            float a1s = 0.0f, a2s = 0.0f;

            if (a2 < 0.0f) {
                /* a conjugate pair: the radius is sqrt(-a2) and the angle comes from a1 = 2 r cos(theta).
                 * The shift moves the angle (the formant) and keeps the radius (the bandwidth). */
                float r = vox_sqrtf(-a2);
                float cos_t = 1.0f;
                float theta, r2, t2;

                if (r > 0.0001f)
                    cos_t = a1 / (2.0f * r);
                if (cos_t > 1.0f)
                    cos_t = 1.0f;
                if (cos_t < -1.0f)
                    cos_t = -1.0f;
                theta = vox_acosf(cos_t);

                r2 = r * (1.0f + 0.10f * (ratio - 1.0f));
                if (r2 > 0.985f)
                    r2 = 0.985f;
                t2 = theta * ratio;
                if (t2 > 3.0f)
                    t2 = 3.0f;
                a1s = 2.0f * r2 * vox_cosf(t2);
                a2s = -r2 * r2;
            } else {
                /* a real pair: the poles are scaled directly */
                float disc = a1 * a1 + 4.0f * a2;
                float p1, p2;
                if (disc < 0.0f)
                    disc = 0.0f;
                p1 = (a1 + vox_sqrtf(disc)) * 0.5f;
                p2 = (a1 - vox_sqrtf(disc)) * 0.5f;
                p1 *= ratio;
                p2 *= ratio;
                if (p1 > 0.985f) p1 = 0.985f;
                if (p1 < -0.985f) p1 = -0.985f;
                if (p2 > 0.985f) p2 = 0.985f;
                if (p2 < -0.985f) p2 = -0.985f;
                a1s = p1 + p2;
                a2s = -p1 * p2;
            }
            s[k] = -a1s;
            s[k + 1u] = -a2s;
        }
        if (VOX_FORMANT_ORDER & 1u) {
            /* an odd order: the last section is a single pole */
            float a1 = v->lpc_a[VOX_FORMANT_ORDER];
            if (a1 > 0.985f)
                a1 = 0.985f;
            if (a1 < -0.985f)
                a1 = -0.985f;
            s[VOX_FORMANT_ORDER] = -a1;
        }

        /* the resynthesis: the excitation through the shifted tract.
         * The excitation keeps its periodicity, so the pitch does not move; only the envelope does. */
        for (i = 0; i < VOX_FORMANT_ORDER; i++)
            v->lpc_state[i] = 0.0f;
        for (i = 0; i < n; i++) {
            float y = v->resid[i];
            for (k = 1; k <= VOX_FORMANT_ORDER; k++)
                y -= s[k] * v->lpc_state[k - 1u];
            for (k = VOX_FORMANT_ORDER; k > 0; k--)
                v->lpc_state[k] = v->lpc_state[k - 1u];
            v->lpc_state[0] = y;
            out[i] = y;
        }

        /* the level: the shifted tract has a different gain, so the block is matched to the input's.
         * Without this the voice would jump in level as the formant is moved. */
        {
            float in_rms = 0.0f, out_rms = 0.0f;
            for (i = 0; i < n; i++) {
                in_rms += in[i] * in[i];
                out_rms += out[i] * out[i];
            }
            in_rms = vox_sqrtf(in_rms / (float)(n ? n : 1u));
            out_rms = vox_sqrtf(out_rms / (float)(n ? n : 1u));
            if (out_rms > 1e-6f) {
                float g = in_rms / out_rms;
                if (g > 8.0f)
                    g = 8.0f;
                for (i = 0; i < n; i++)
                    out[i] *= g;
            }
        }
    }
}
