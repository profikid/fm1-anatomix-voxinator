/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the effects chain.
 *
 * Four slots in order, each over the previous one's output. Every effect has three parameters and a
 * wet/dry mix. The types are grouped as the product groups them: the dynamics, the character, the
 * modulation, the space and the experimental.
 *
 * The chain's two big buffers (the delay's and the reverb's lines) belong to the caller: the app puts
 * them in the platform's RAM pool and hands the pointers over. The engine never allocates.
 */
#include "vox.h"
#include "vox_math.h"

/* the chain's reset */
void vox_fx_init(vox_fx_state_t *fx, const vox_buffers_t *bufs)
{
    uint32_t i;

    if (bufs) {
        fx->delay_l = bufs->delay_l;
        fx->delay_r = bufs->delay_r;
        fx->reverb_l = bufs->reverb_l;
        fx->reverb_r = bufs->reverb_r;
    } else {
        fx->delay_l = 0;
        fx->delay_r = 0;
        fx->reverb_l = 0;
        fx->reverb_r = 0;
    }
    fx->delay_pos = 0;
    fx->reverb_pos = 0;
    fx->lfo = 0.0f;
    fx->seed = 0x2F6E3B1u;
    for (i = 0; i < VOX_BLOCK; i++) {
        fx->hold[i] = 0.0f;
        fx->held[i] = 0.0f;
    }
    fx->held_len = 0;
    fx->hold_pos = 0;
    fx->glitch_until = 0;
    fx->stutter_count = 0;
}

/* the effect's name, for the display */
const char *vox_fx_name(int type)
{
    switch (type) {
    case VOX_FX_NONE:          return "EMPTY";
    case VOX_FX_GATE:          return "GATE";
    case VOX_FX_COMP:          return "COMP";
    case VOX_FX_LIMITER:       return "LIMITER";
    case VOX_FX_DEESSER:       return "DE-ESS";
    case VOX_FX_EQ:            return "EQ";
    case VOX_FX_SATURATE:      return "SATURATE";
    case VOX_FX_OVERDRIVE:     return "OVERDRIVE";
    case VOX_FX_FUZZ:          return "FUZZ";
    case VOX_FX_BITCRUSH:      return "BITCRUSH";
    case VOX_FX_TELEPHONE:     return "TELEPHONE";
    case VOX_FX_RADIO:         return "RADIO";
    case VOX_FX_VOCAL_DIST:    return "DISTORTION";
    case VOX_FX_CHORUS:        return "CHORUS";
    case VOX_FX_FLANGER:       return "FLANGER";
    case VOX_FX_PHASER:        return "PHASER";
    case VOX_FX_TREMOLO:       return "TREMOLO";
    case VOX_FX_RING:          return "RING MOD";
    case VOX_FX_DOUBLER:       return "DOUBLER";
    case VOX_FX_DELAY:         return "DELAY";
    case VOX_FX_TAPE_DELAY:    return "TAPE DELAY";
    case VOX_FX_PINGPONG:      return "PING PONG";
    case VOX_FX_STEREO_DELAY:  return "STEREO DELAY";
    case VOX_FX_REVERB:        return "REVERB";
    case VOX_FX_SHIMMER:       return "SHIMMER";
    case VOX_FX_FORMANT_FILTER:return "FORMANT";
    case VOX_FX_ROBOTIC:       return "ROBOTIC";
    case VOX_FX_PITCH_GLITCH:  return "PITCH GLITCH";
    case VOX_FX_STUTTER:       return "STUTTER";
    case VOX_FX_FREEZE:        return "FREEZE";
    case VOX_FX_SPECTRAL:      return "SPECTRAL";
    case VOX_FX_VOCODER:       return "VOCODER";
    default:                   return "?";
    }
}

/* the effect's three parameter names */
void vox_fx_param_name(int type, char out[3][12])
{
    static const char *const N[3] = {"AMOUNT", "RATE", "TONE"};
    int i;

    for (i = 0; i < 3; i++) {
        uint32_t k = 0;
        while (N[i][k] && k < 11u) {
            out[i][k] = N[i][k];
            k++;
        }
        out[i][k] = 0;
    }
    switch (type) {
    case VOX_FX_BITCRUSH:
    case VOX_FX_SPECTRAL:
        out[0][0] = 'B'; out[0][1] = 'I'; out[0][2] = 'T'; out[0][3] = 0;
        break;
    case VOX_FX_DELAY:
    case VOX_FX_TAPE_DELAY:
    case VOX_FX_PINGPONG:
    case VOX_FX_STEREO_DELAY:
        out[0][0] = 'T'; out[0][1] = 'I'; out[0][2] = 'M'; out[0][3] = 'E'; out[0][4] = 0;
        out[1][0] = 'F'; out[1][1] = 'B'; out[1][2] = 0;
        out[2][0] = 'T'; out[2][1] = 'O'; out[2][2] = 'N'; out[2][3] = 'E'; out[2][4] = 0;
        break;
    case VOX_FX_REVERB:
    case VOX_FX_SHIMMER:
        out[0][0] = 'S'; out[0][1] = 'I'; out[0][2] = 'Z'; out[0][3] = 'E'; out[0][4] = 0;
        out[1][0] = 'D'; out[1][1] = 'E'; out[1][2] = 'C'; out[1][3] = 'A'; out[1][4] = 'Y'; out[1][5] = 0;
        out[2][0] = 'T'; out[2][1] = 'O'; out[2][2] = 'N'; out[2][3] = 'E'; out[2][4] = 0;
        break;
    case VOX_FX_FORMANT_FILTER:
    case VOX_FX_VOCODER:
        out[0][0] = 'F'; out[0][1] = 'O'; out[0][2] = 'R'; out[0][3] = 'M'; out[0][4] = 'A'; out[0][5] = 'N';
        out[0][6] = 'T'; out[0][7] = 0;
        out[1][0] = 'M'; out[1][1] = 'I'; out[1][2] = 'X'; out[1][3] = 0;
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ the helpers --- */

/* the dry and the wet are mixed back together */
static void mix_back(float *l, float *r, const float *dl, const float *dr, uint32_t n, uint8_t mix_pct)
{
    float w = (float)mix_pct * (1.0f / 100.0f);
    uint32_t i;
    if (w > 1.0f)
        w = 1.0f;
    for (i = 0; i < n; i++) {
        l[i] = dl[i] * (1.0f - w) + l[i] * w;
        r[i] = dr[i] * (1.0f - w) + r[i] * w;
    }
}

/* ------------------------------------------------------------------ one effect --- */

/* one effect over a stereo block */
void vox_fx_one(vox_fx_state_t *fx, int type, const uint8_t p[3], uint8_t mix, float *l, float *r, uint32_t n,
                uint32_t now_ms)
{
    float *dl = fx->dry_l;
    float *dr = fx->dry_r;
    float a = (float)p[0] * 0.01f;
    float b = (float)p[1] * 0.01f;
    float c = (float)p[2] * 0.01f;
    uint32_t i;

    if (type == VOX_FX_NONE || n == 0)
        return;
    if (n > VOX_BLOCK)
        n = VOX_BLOCK;

    for (i = 0; i < n; i++) {
        dl[i] = l[i];
        dr[i] = r[i];
    }

    switch (type) {
    case VOX_FX_GATE: {
        /* a hard gate on the level: the amount is the threshold's depth */
        float thresh = 0.02f + a * 0.3f;
        for (i = 0; i < n; i++) {
            float m = l[i] < 0.0f ? -l[i] : l[i];
            if (m < thresh) {
                l[i] = 0.0f;
                r[i] = 0.0f;
            }
        }
        break;
    }
    case VOX_FX_COMP: {
        /* a simple compressor: the amount is the threshold's depth, the rate its speed */
        float atk = 1.0f / (float)(VOX_SR * (1.0f + 30.0f * (1.0f - b)) / 1000.0f);
        float rel = 1.0f / (float)(VOX_SR * (40.0f + 200.0f * b) / 1000.0f);
        float g = 1.0f, env = 0.0f;
        float thresh = 0.5f - a * 0.45f;
        for (i = 0; i < n; i++) {
            float m = l[i] < 0.0f ? -l[i] : l[i];
            env += (m - env) * (m > env ? atk : rel);
            if (env > thresh && thresh > 0.001f) {
                float want = thresh / env;
                if (want < g)
                    g = want;
                else
                    g += (want - g) * rel;
            } else {
                g += (1.0f - g) * rel;
            }
            l[i] *= g;
            r[i] *= g;
        }
        break;
    }
    case VOX_FX_LIMITER: {
        float g = 1.0f, env = 0.0f;
        float ceil = 0.95f;
        for (i = 0; i < n; i++) {
            float m = l[i] < 0.0f ? -l[i] : l[i];
            env += (m - env) * 0.4f;
            if (env > ceil) {
                float want = ceil / env;
                if (want < g)
                    g = want;
            } else {
                g += (1.0f - g) * 0.01f;
            }
            l[i] *= g;
            r[i] *= g;
        }
        break;
    }
    case VOX_FX_DEESSER: {
        /* the high band is measured and taken off */
        static float hs;
        float hp = 1.0f - vox_exp2f(-2.0f * 3.1415927f * 6000.0f / (float)VOX_SR);
        float g = 1.0f;
        for (i = 0; i < n; i++) {
            float hi = l[i] - hs;
            hs += hp * (l[i] - hs);
            hi = l[i] - hs;
            {
                float m = hi < 0.0f ? -hi : hi;
                if (m > 0.12f) {
                    float want = 0.12f / m;
                    if (want < g)
                        g = want;
                } else {
                    g += (1.0f - g) * 0.01f;
                }
            }
            l[i] = l[i] - hi * (1.0f - g) * a;
            r[i] = r[i] - hi * (1.0f - g) * a * 0.5f;
        }
        break;
    }
    case VOX_FX_EQ: {
        /* a low shelf and a high shelf: the amount is the low's depth, the tone the high's */
        static float ll, hl;
        float lo = 1.0f - vox_exp2f(-2.0f * 3.1415927f * 200.0f / (float)VOX_SR);
        float hi = 1.0f - vox_exp2f(-2.0f * 3.1415927f * 4000.0f / (float)VOX_SR);
        for (i = 0; i < n; i++) {
            float low = l[i];
            ll += lo * (low - ll);
            low = ll;
            hl += hi * (l[i] - hl);
            {
                float high = l[i] - hl;
                l[i] = low + (1.0f + a * 2.0f) * (l[i] - low) + high * (1.0f + c * 2.0f);
                r[i] = r[i] * (1.0f + a * 0.5f);
            }
        }
        break;
    }
    case VOX_FX_SATURATE:
    case VOX_FX_OVERDRIVE:
    case VOX_FX_VOCAL_DIST: {
        float drive = 1.0f + a * (type == VOX_FX_VOCAL_DIST ? 12.0f : 6.0f);
        for (i = 0; i < n; i++) {
            l[i] = vox_sat(l[i], drive) * (1.0f + a * 0.4f);
            r[i] = vox_sat(r[i], drive) * (1.0f + a * 0.4f);
        }
        break;
    }
    case VOX_FX_FUZZ: {
        /* the fuzz: a hard clip with a squared curve */
        float drive = 1.0f + a * 30.0f;
        for (i = 0; i < n; i++) {
            float x = l[i] * drive;
            float y = x < 0.0f ? -1.0f : 1.0f;
            y *= 0.7f + 0.3f * (x < 0.0f ? -x : x);
            l[i] = y * (0.3f + a * 0.7f);
            r[i] = y * (0.3f + a * 0.7f);
        }
        break;
    }
    case VOX_FX_BITCRUSH: {
        /* the bit depth: the amount is how many bits are left */
        float levels = 2.0f + (1.0f - a) * 62.0f;
        for (i = 0; i < n; i++) {
            l[i] = (float)((int32_t)(l[i] * levels) / (int32_t)levels) / levels;
            r[i] = (float)((int32_t)(r[i] * levels) / (int32_t)levels) / levels;
        }
        break;
    }
    case VOX_FX_TELEPHONE: {
        /* the telephone band: 300 Hz to 3400 Hz */
        static float tl, th;
        float lo = 1.0f - vox_exp2f(-2.0f * 3.1415927f * 400.0f / (float)VOX_SR);
        float hi = 1.0f - vox_exp2f(-2.0f * 3.1415927f * 3200.0f / (float)VOX_SR);
        for (i = 0; i < n; i++) {
            float x = l[i];
            tl += lo * (x - tl);
            th += hi * (x - th);
            l[i] = (x - tl) - (x - th);
            r[i] = (r[i] - tl) - (r[i] - th);
        }
        break;
    }
    case VOX_FX_RADIO: {
        /* the radio: band-limited, with a little distortion and a hum */
        static float rl, rh;
        float lo = 1.0f - vox_exp2f(-2.0f * 3.1415927f * 500.0f / (float)VOX_SR);
        float hi = 1.0f - vox_exp2f(-2.0f * 3.1415927f * 4000.0f / (float)VOX_SR);
        for (i = 0; i < n; i++) {
            float x = l[i];
            rl += lo * (x - rl);
            rh += hi * (x - rh);
            x = (x - rl) - (x - rh);
            x = vox_sat(x, 2.0f + a * 6.0f);
            l[i] = x;
            r[i] = x * 0.7f;
        }
        break;
    }
    case VOX_FX_CHORUS:
    case VOX_FX_FLANGER: {
        /* the modulated delay: the amount is the depth, the rate the speed */
        float depth = 1.0f + a * 8.0f;
        float rate = 0.2f + b * 6.0f;
        float base = type == VOX_FX_FLANGER ? 1.0f : 12.0f;
        static float zl, zr;
        for (i = 0; i < n; i++) {
            float t = (float)(now_ms + i) * 0.001f;
            float m = vox_sin_turns(t * rate * (1.0f / 60.0f));
            float dl = base + m * depth;
            float dr = base - m * depth * 0.6f;
            float sl = l[i > (uint32_t)dl ? i - (uint32_t)dl : 0u];
            float sr = r[i > (uint32_t)dr ? i - (uint32_t)dr : 0u];
            zl += (sl - zl) * 0.3f;
            zr += (sr - zr) * 0.3f;
            l[i] = zl;
            r[i] = zr;
        }
        break;
    }
    case VOX_FX_PHASER: {
        /* the all-pass comb: four stages, the rate sweeping them */
        float rate = 0.1f + b * 4.0f;
        static float p1, p2, p3, p4;
        for (i = 0; i < n; i++) {
            float t = (float)(now_ms + i) * 0.001f;
            float m = vox_sin_turns(t * rate * (1.0f / 60.0f));
            float g = 0.2f + m * 0.75f;
            float x = l[i];
            p1 = x - g * p1;
            p2 = p1 - g * p2;
            p3 = p2 - g * p3;
            p4 = p3 - g * p4;
            l[i] = p4 * 0.5f + x * 0.5f;
            r[i] = l[i];
        }
        break;
    }
    case VOX_FX_TREMOLO: {
        float rate = 0.5f + b * 12.0f;
        float depth = a * 0.9f;
        for (i = 0; i < n; i++) {
            float t = (float)(now_ms + i) * 0.001f;
            float m = vox_sin_turns(t * rate * (1.0f / 60.0f));
            float g = 1.0f - depth * 0.5f + m * depth * 0.5f;
            l[i] *= g;
            r[i] *= g;
        }
        break;
    }
    case VOX_FX_RING: {
        /* the ring modulator: the signal multiplied by a sine */
        float rate = 20.0f + b * 900.0f;
        for (i = 0; i < n; i++) {
            float t = (float)(now_ms + i) * 0.001f;
            float m = vox_sin_turns(t * rate * (1.0f / 60.0f));
            l[i] = l[i] * m * (0.5f + a);
            r[i] = r[i] * m * (0.5f + a);
        }
        break;
    }
    case VOX_FX_DOUBLER: {
        /* the doubler: the signal a little later, with a small pitch wobble */
        float delay = 12.0f + a * 40.0f;
        for (i = n; i > (uint32_t)delay; i--) {
            l[i - 1u] += l[i - 1u - (uint32_t)delay] * 0.6f;
            r[i - 1u] += r[i - 1u - (uint32_t)delay] * 0.6f;
        }
        break;
    }
    case VOX_FX_DELAY:
    case VOX_FX_TAPE_DELAY:
    case VOX_FX_PINGPONG:
    case VOX_FX_STEREO_DELAY: {
        /* the delay line: the time, the feedback and the tone */
        uint32_t dtime;
        float fb, tone;
        if (!fx->delay_l)
            break;
        dtime = (uint32_t)(VOX_SR * (0.02f + a * 0.45f));
        if (dtime >= VOX_FX_DELAY_MAX)
            dtime = VOX_FX_DELAY_MAX - 1u;
        fb = b * 0.85f;
        tone = 1.0f - vox_exp2f(-2.0f * 3.1415927f * (500.0f + c * 9000.0f) / (float)VOX_SR);
        for (i = 0; i < n; i++) {
            uint32_t rp = (fx->delay_pos + VOX_FX_DELAY_MAX - dtime) % VOX_FX_DELAY_MAX;
            float dl = (float)fx->delay_l[rp] * (1.0f / 32768.0f);
            float dr = (float)fx->delay_r[rp] * (1.0f / 32768.0f);
            /* the tape: the feedback is darkened */
            static float ts;
            ts += tone * (dl - ts);
            dl = ts;   /* the tape: the feedback is darkened */
            if (type == VOX_FX_PINGPONG) {
                float tmp = dr;
                dr = dl;
                dl = tmp;
            }
            l[i] += dl;
            r[i] += dr;
            /* the write back: the input and the feedback, with a limit so it cannot run away */
            {
                float wl = l[i] * 0.7f + dl * fb;
                float wr = r[i] * 0.7f + dr * fb;
                if (wl > 1.5f) wl = 1.5f;
                if (wl < -1.5f) wl = -1.5f;
                if (wr > 1.5f) wr = 1.5f;
                if (wr < -1.5f) wr = -1.5f;
                fx->delay_l[rp] = (int16_t)(wl * 32767.0f);
                fx->delay_r[rp] = (int16_t)(wr * 32767.0f);
            }
            fx->delay_pos = (fx->delay_pos + 1u) % VOX_FX_DELAY_MAX;
        }
        break;
    }
    case VOX_FX_REVERB:
    case VOX_FX_SHIMMER: {
        /* a Schroeder reverb: two combs and two all-passes, on the caller's lines */
        uint32_t c1, c2;
        float fb, damp;
        if (!fx->reverb_l)
            break;
        c1 = 1116u + (uint32_t)(a * 300.0f);
        c2 = 1188u + (uint32_t)(a * 400.0f);
        if (c1 >= VOX_FX_REVERB_MAX) c1 = VOX_FX_REVERB_MAX - 1u;
        if (c2 >= VOX_FX_REVERB_MAX) c2 = VOX_FX_REVERB_MAX - 1u;
        fb = 0.6f + b * 0.35f;
        damp = 1.0f - vox_exp2f(-2.0f * 3.1415927f * (1000.0f + c * 9000.0f) / (float)VOX_SR);
        for (i = 0; i < n; i++) {
            uint32_t p1 = (fx->reverb_pos + VOX_FX_REVERB_MAX - c1) % VOX_FX_REVERB_MAX;
            uint32_t p2 = (fx->reverb_pos + VOX_FX_REVERB_MAX - c2) % VOX_FX_REVERB_MAX;
            float v1 = (float)fx->reverb_l[p1] * (1.0f / 32768.0f);
            float v2 = (float)fx->reverb_r[p2] * (1.0f / 32768.0f);
            float acc = (v1 + v2) * 0.5f;
            /* the damping: the tail gets darker as it decays */
            {
                static float dstate;
                dstate += damp * (acc - dstate);
                acc = dstate;
            }
            fx->reverb_l[fx->reverb_pos] = (int16_t)((l[i] * 0.3f + acc * fb) * 32767.0f);
            fx->reverb_r[fx->reverb_pos] = (int16_t)((r[i] * 0.3f + acc * fb) * 32767.0f);
            l[i] += acc * (0.4f + a * 0.6f);
            r[i] += acc * (0.4f + a * 0.6f);
            fx->reverb_pos = (fx->reverb_pos + 1u) % VOX_FX_REVERB_MAX;
        }
        break;
    }
    case VOX_FX_FORMANT_FILTER: {
        /* a pair of resonant peaks: the amount is how strong, the tone where they sit */
        static float f1, f2;
        float g = 0.3f + a * 0.7f;
        float f = 300.0f + c * 2000.0f;
        float k = 1.0f - vox_exp2f(-2.0f * 3.1415927f * f / (float)VOX_SR);
        for (i = 0; i < n; i++) {
            float x = l[i];
            f1 += k * (x - f1);
            f2 += k * (f1 - f2);
            l[i] = x * (1.0f - g * 0.6f) + (f1 - f2) * g * 2.0f;
            r[i] = l[i];
        }
        break;
    }
    case VOX_FX_ROBOTIC: {
        /* the quantised pitch: the signal is chopped into steps */
        float steps = 2.0f + (1.0f - a) * 12.0f;
        for (i = 0; i < n; i++) {
            l[i] = (float)((int32_t)(l[i] * steps) / (int32_t)steps) / steps;
            r[i] = l[i];
        }
        break;
    }
    case VOX_FX_PITCH_GLITCH: {
        /* the pitch jumps around in bursts */
        if (now_ms > fx->glitch_until) {
            fx->glitch_until = now_ms + 80u + (uint32_t)(vox_randf(&fx->seed) * 400.0f);
            fx->hold_pos = (uint8_t)((fx->hold_pos + 1u) & 7u);
        }
        {
            float jump = 1.0f + (float)fx->hold_pos * 0.25f;
            for (i = 0; i < n; i++) {
                l[i] *= jump;
                r[i] *= jump;
            }
        }
        break;
    }
    case VOX_FX_STUTTER: {
        /* the signal repeats in short grains */
        uint32_t g = 8u + (uint32_t)(a * 60.0f);
        for (i = 0; i < n; i++) {
            uint32_t k = i % g;
            l[i] = dl[k];
            r[i] = dr[k];
        }
        break;
    }
    case VOX_FX_FREEZE: {
        /* the block is held and repeated */
        if (fx->held_len == 0) {
            for (i = 0; i < n; i++) {
                fx->held[i] = l[i];
                fx->hold[i] = r[i];
            }
            fx->held_len = (uint8_t)(n > 255u ? 255u : n);
        }
        for (i = 0; i < n; i++) {
            uint32_t k = (fx->hold_pos + i) % fx->held_len;
            l[i] = fx->held[k];
            r[i] = fx->hold[k];
        }
        fx->hold_pos = (uint8_t)((fx->hold_pos + n) % fx->held_len);
        break;
    }
    case VOX_FX_SPECTRAL: {
        /* the spectrum is degraded: the signal is squashed and the high band is taken off */
        static float sh;
        float hi = 1.0f - vox_exp2f(-2.0f * 3.1415927f * 2000.0f / (float)VOX_SR);
        for (i = 0; i < n; i++) {
            float x = l[i];
            sh += hi * (x - sh);
            x = (x - sh) * (0.3f + a * 0.7f);
            l[i] = vox_soft(x * 3.0f);
            r[i] = l[i];
        }
        break;
    }
    case VOX_FX_VOCODER: {
        /* a band-pass bank: three resonators, the tone moving them */
        static float v1, v2, v3;
        float k1 = 1.0f - vox_exp2f(-2.0f * 3.1415927f * (400.0f + c * 500.0f) / (float)VOX_SR);
        float k2 = 1.0f - vox_exp2f(-2.0f * 3.1415927f * (1200.0f + c * 1500.0f) / (float)VOX_SR);
        float k3 = 1.0f - vox_exp2f(-2.0f * 3.1415927f * (2600.0f + c * 3000.0f) / (float)VOX_SR);
        for (i = 0; i < n; i++) {
            float x = l[i];
            v1 += k1 * (x - v1);
            v2 += k2 * (x - v2);
            v3 += k3 * (x - v3);
            l[i] = (v1 + v2 + v3) * (0.3f + a);
            r[i] = l[i];
        }
        break;
    }
    default:
        break;
    }

    mix_back(l, r, dl, dr, n, mix);
}

/* the chain: the four slots in order */
void vox_fx_process(vox_fx_state_t *fx, const vox_fxslot_t *slots, float *l, float *r, uint32_t n,
                    uint32_t now_ms)
{
    uint32_t s;

    if (n > VOX_BLOCK)
        n = VOX_BLOCK;
    for (s = 0; s < VOX_FX_SLOTS; s++) {
        if (!slots[s].on || slots[s].type == VOX_FX_NONE)
            continue;
        vox_fx_one(fx, (int)slots[s].type, slots[s].p, slots[s].mix, l, r, n, now_ms);
    }
}
