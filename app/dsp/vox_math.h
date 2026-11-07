/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR's DSP: the maths.
 *
 * Platform-free and libm-free: the device's compiler has no libm and no double precision, so every helper
 * here is a float approximation with a stated error. The style follows fm1-base's own brk_math.h.
 */
#pragma once
#include <stdint.h>

#define VOX_INL static inline __attribute__((always_inline))

/* the clamp */
VOX_INL float vox_clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
VOX_INL int32_t vox_clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* the absolute value */
VOX_INL float vox_absf(float x) { return x < 0.0f ? -x : x; }

/* the absolute value, spelled the way the C library would (the device has no fabsf) */
VOX_INL float vox_fabsf(float x) { return x < 0.0f ? -x : x; }

/* the soft clip: x / (1 + |x|). Never wraps, always inside +-1. */
VOX_INL float vox_soft(float x)
{
    float a = vox_absf(x);
    return x / (1.0f + a);
}

/* the hard clip with a slope: the saturation curve the character effects use */
VOX_INL float vox_sat(float x, float drive)
{
    float y = x * drive;
    return y / (1.0f + vox_absf(y));
}

/* sin, t in turns (0..1 == 0..2 pi). The fifth-order polynomial: the error is under 0.0004 over the turn.
 * The second half of the turn uses the symmetry, so the sign is kept. */
VOX_INL float vox_sin_turns(float t)
{
    float x, x2, s;

    t = t - (float)(int32_t)t;            /* into 0..1 */
    if (t < 0.0f)
        t += 1.0f;
    if (t > 0.5f) {
        t = 1.0f - t;                      /* sin(2 pi t) = -sin(2 pi (1 - t)) */
        x = t * 6.2831853f;
        x2 = x * x;
        s = x * (1.0f - x2 * (0.1666667f - x2 * (0.0083333f - x2 * 0.0001984f)));
        return -s;
    }
    x = t * 6.2831853f;                   /* 0..pi */
    x2 = x * x;
    return x * (1.0f - x2 * (0.1666667f - x2 * (0.0083333f - x2 * 0.0001984f)));
}

/* 2^x. The error is under 1e-4. */
VOX_INL float vox_exp2f(float x)
{
    union { float f; uint32_t u; } r;
    float e, t;
    int32_t i;

    if (x < -126.0f)
        x = -126.0f;
    if (x > 126.0f)
        x = 126.0f;
    e = (float)(int32_t)(x < 0.0f ? x - 0.999999f : x);   /* the integer part, rounded down */
    t = x - e;                                            /* the fraction, in -1..1 */
    i = (int32_t)e;
    r.u = (uint32_t)((i + 127) << 23);                    /* 2^e */
    return r.f * (1.0f + t * (0.6931472f + t * (0.2402265f + t * (0.0555041f + t * 0.0096181f))));
}

/* the square root, by Newton's method on the mantissa. The error is under 1e-5. */
VOX_INL float vox_sqrtf(float x)
{
    union { float f; uint32_t u; } r;
    float e, m;

    if (x <= 0.0f)
        return 0.0f;
    r.f = x;
    e = (float)(int32_t)((r.u >> 23) & 0xFFu) - 127.0f;
    r.u = (r.u & 0x007FFFFFu) | 0x3F800000u;   /* 1 <= m < 2, x = m * 2^e */
    m = r.f;
    m = 0.5f * (m + 1.0f / m);
    m = 0.5f * (m + 1.0f / m);
    m = 0.5f * (m + 1.0f / m);
    return m * vox_exp2f(e * 0.5f);
}

/* log2(x), x > 0. The error is under 0.002. */
VOX_INL float vox_log2f(float x)
{
    union { float f; uint32_t u; } r;
    float e, t;
    if (x <= 0.0f)
        return -126.0f;
    r.f = x;
    e = (float)(int32_t)((r.u >> 23) & 0xFFu) - 127.0f;
    r.u = (r.u & 0x007FFFFFu) | 0x3F800000u;   /* 1 <= 1 + t < 2 */
    t = r.f - 1.0f;
    return e + t * (1.4234755f + t * (-0.5876945f + t * 0.1642190f));
}

/* tan(pi * f / sr), for a filter's prewarp (f below sr / 2) */
VOX_INL float vox_tan_pi(float f_over_sr)
{
    float t = vox_clampf(f_over_sr, 0.0f, 0.49f) * 0.5f;
    float c = vox_sin_turns(t + 0.25f);
    return vox_sin_turns(t) / (c < 1e-4f ? 1e-4f : c);
}

/* the arc sine, for y in -1..1 (the Taylor series: the error is under 1e-3 at the ends) */
VOX_INL float vox_asinf(float y)
{
    float y2 = y * y;
    return y * (1.0f + y2 * (0.1666667f + y2 * (0.075f + y2 * (0.0446429f + y2 * (0.0303819f +
           y2 * (0.0223722f + y2 * 0.0173528f))))));
}

/* the arc cosine, for x in -1..1 */
VOX_INL float vox_acosf(float x)
{
    if (x >= 1.0f)
        return 0.0f;
    if (x <= -1.0f)
        return 3.1415927f;
    return 1.5707963f - vox_asinf(x);
}

/* the cosine, in radians */
VOX_INL float vox_cosf(float x)
{
    return vox_sin_turns(x * 0.15915494f + 0.25f);
}

/* xorshift32: the dice. Deterministic for a seed. */
VOX_INL uint32_t vox_rand(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

/* 0 .. 1 */
VOX_INL float vox_randf(uint32_t *s) { return (float)(vox_rand(s) >> 8) * (1.0f / 16777216.0f); }

/* -1 .. 1 */
VOX_INL float vox_randpm(uint32_t *s) { return vox_randf(s) * 2.0f - 1.0f; }

/* the MIDI note of a frequency: 69 + 12 * log2(f / 440) */
VOX_INL float vox_midi_of(float hz) { return 69.0f + 12.0f * vox_log2f(hz / 440.0f); }

/* the frequency of a MIDI note */
VOX_INL float vox_hz_of(float midi) { return 440.0f * vox_exp2f((midi - 69.0f) / 12.0f); }

/* the semitones between two frequencies */
VOX_INL float vox_semitones(float from_hz, float to_hz)
{
    if (from_hz <= 0.0f || to_hz <= 0.0f)
        return 0.0f;
    return 12.0f * vox_log2f(to_hz / from_hz);
}
