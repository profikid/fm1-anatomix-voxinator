/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the fundamental frequency detector.
 *
 * A time-domain detector: the signal is decimated by two (22.05 kHz) and a normalised difference
 * function (the YIN idea) is swept over the lag range that the voice can occupy. The normalisation is what
 * makes it survive a noisy room, and the running mean it keeps is the clarity: how periodic the input is.
 *
 * Why not a correlation: a correlation over the whole lag range is O(n^2) per lag, and the budget is one
 * 5.8 ms block every 5.8 ms. The difference function over a bounded window costs about a fifth of that.
 *
 * Why not an FFT: the platform has no FFT, and a 1024-point one would cost more RAM than the whole
 * detector.
 */
#include "vox.h"
#include "vox_math.h"

#define DEC_SR (VOX_SR / 2u)            /* the decimated rate: 22.05 kHz */
#define LAG_MIN 20u                     /* 22.05 kHz / 1100 Hz: the shortest lag we look for */
#define LAG_MAX 340u                    /* 22.05 kHz / 65 Hz: the longest */
#define WIN 384u                        /* the difference function's window, in decimated samples */
#define YIN_THRESHOLD 0.18f             /* below this the lag is a real period */
#define RMS_FLOOR 0.002f                /* below this the input is silence, not a quiet voice */

/* the detector's reset */
void vox_detect_init(vox_detect_t *d)
{
    uint32_t i;
    for (i = 0; i < VOX_BLOCK; i++)
        d->hist[i] = 0.0f;
    for (i = 0; i < VOX_DET_DEC; i++)
        d->dec[i] = 0.0f;
    d->hist_pos = 0;
    d->filled = 0;
    d->dec_pos = 0;
    d->dec_filled = 0;
    d->dec_carry = 0.0f;
    d->f0 = 0.0f;
    d->clarity = 0.0f;
    d->rms = 0.0f;
    d->period = 0.0f;
    d->note = -1;
    d->octave = -1;
    d->voiced = 0;
    d->seed = 0x1234567u;
}

/* the difference function at one lag, over the window ending at the newest sample.
 * The signal is the decimated ring, read backwards from dec_pos. */
static float diff_at(const vox_detect_t *d, uint32_t lag)
{
    float sum = 0.0f;
    uint32_t j;
    for (j = 0; j < WIN; j++) {
        /* the newest sample backwards: the ring's write position minus one, minus the age */
        uint32_t i0 = (d->dec_pos + VOX_DET_DEC - 1u - j) % VOX_DET_DEC;
        uint32_t i1 = (d->dec_pos + VOX_DET_DEC - 1u - j - lag) % VOX_DET_DEC;
        float a = d->dec[i0] - d->dec[i1];
        sum += a * a;
    }
    return sum;
}

/* the fundamental in a block of n samples.
 * Returns 1 when the block is voiced (and d->f0 is then the fundamental), 0 when it is not. */
int vox_detect(vox_detect_t *d, const float *x, uint32_t n)
{
    float rms = 0.0f, diff[LAG_MAX + 1], cmnd[LAG_MAX + 1], best = 0.0f;
    uint32_t i, lag, best_lag = 0;
    int have_best = 0;

    /* the level: the root mean square of the block */
    for (i = 0; i < n; i++) {
        rms += x[i] * x[i];
        d->hist[d->hist_pos] = x[i];
        d->hist_pos = (d->hist_pos + 1u) % VOX_BLOCK;
        if (d->filled < VOX_BLOCK)
            d->filled++;
    }
    rms = vox_sqrtf(rms / (float)(n ? n : 1u));
    d->rms = rms;

    /* the decimation: two samples in, one out, with a one-pole low pass first so the aliasing stays small */
    {
        float z = d->dec_carry;
        for (i = 0; i < n; i++) {
            z += (x[i] - z) * 0.35f;         /* the anti-alias filter */
            if ((i & 1u) == 1u) {
                d->dec[d->dec_pos] = z;
                d->dec_pos = (d->dec_pos + 1u) % VOX_DET_DEC;
                if (d->dec_filled < VOX_DET_DEC)
                    d->dec_filled++;
            }
        }
        d->dec_carry = z;
    }

    /* not enough history, or too quiet: unvoiced */
    if (d->dec_filled < (WIN + LAG_MAX) || rms < RMS_FLOOR) {
        d->voiced = 0;
        d->clarity = 0.0f;
        d->period = 0.0f;
        d->note = -1;
        d->octave = -1;
        d->f0 = 0.0f;
        return 0;
    }

    /* the difference function and its cumulative mean (the mean runs over the lags 1..lag) */
    diff[0] = 0.0f;
    cmnd[0] = 1.0f;
    {
        float s = 0.0f;
        for (lag = 1; lag <= LAG_MAX; lag++) {
            diff[lag] = diff_at(d, lag);
            s += diff[lag];
            cmnd[lag] = diff[lag] * (float)lag / (s > 1e-12f ? s : 1e-12f);
        }
    }

    /* the first lag that is a real period (the YIN rule), else the global minimum */
    for (lag = LAG_MIN; lag <= LAG_MAX; lag++) {
        if (cmnd[lag] < YIN_THRESHOLD) {
            while (lag + 1u <= LAG_MAX && cmnd[lag + 1u] < cmnd[lag])
                lag++;
            best_lag = lag;
            have_best = 1;
            break;
        }
    }
    if (!have_best) {
        float lowest = 1e9f;
        for (lag = LAG_MIN; lag <= LAG_MAX; lag++) {
            if (cmnd[lag] < lowest) {
                lowest = cmnd[lag];
                best_lag = lag;
            }
        }
        /* a poor minimum is not a period */
        if (lowest > 0.65f) {
            d->voiced = 0;
            d->clarity = 0.0f;
            d->period = 0.0f;
            d->note = -1;
            d->octave = -1;
            d->f0 = 0.0f;
            return 0;
        }
        have_best = 1;
    }

    /* the parabolic refinement around the best lag, for the fractional period */
    {
        float y0 = cmnd[best_lag > 1u ? best_lag - 1u : best_lag];
        float y1 = cmnd[best_lag];
        float y2 = cmnd[best_lag + 1u <= LAG_MAX ? best_lag + 1u : best_lag];
        float denom = 2.0f * (2.0f * y1 - y0 - y2);
        float shift = 0.0f;
        if (denom > 1e-9f || denom < -1e-9f)
            shift = (y2 - y0) / denom;
        if (shift > 0.5f)
            shift = 0.5f;
        if (shift < -0.5f)
            shift = -0.5f;
        best = (float)best_lag + shift;
    }

    /* the pitch: the decimated period, twice the length at the real rate */
    d->period = best * 2.0f;
    {
        float hz = (float)DEC_SR / best;
        if (hz < VOX_DET_MIN_HZ || hz > VOX_DET_MAX_HZ) {
            d->voiced = 0;
            d->clarity = 0.0f;
            d->period = 0.0f;
            d->note = -1;
            d->octave = -1;
            d->f0 = 0.0f;
            return 0;
        }
        /* the smoothing: the pitch does not jump between the blocks, it moves towards the new one */
        if (d->f0 <= 0.0f)
            d->f0 = hz;
        else {
            float k = 0.45f;
            /* a big jump (a new note) is followed at once, a small one is smoothed */
            if (hz > d->f0 * 1.6f || hz < d->f0 * 0.6f)
                k = 0.85f;
            d->f0 += (hz - d->f0) * k;
        }
    }

    d->clarity = 1.0f - cmnd[best_lag];
    if (d->clarity < 0.0f)
        d->clarity = 0.0f;
    if (d->clarity > 1.0f)
        d->clarity = 1.0f;

    /* the note */
    {
        float midi = vox_midi_of(d->f0);
        int note = (int)(midi + (midi >= 0.0f ? 0.5f : -0.5f));
        if (note < 0)
            note = 0;
        if (note > 127)
            note = 127;
        d->note = (int8_t)note;
        d->octave = (int8_t)(note / 12 - 1);
    }
    d->voiced = 1;
    return 1;
}
