/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the time-domain pitch shifter.
 *
 * The shifter changes the pitch and keeps the duration. That is what a harmony needs: if the voice's
 * length moved, a three-note chord would fall apart.
 *
 * The method is an overlap-add of resampled grains, with the phase alignment (SOLA) that keeps the
 * voice a voice instead of a chorus of out-of-phase fragments.
 *
 *   - the grains sit one source period apart in the output, and each grain is two periods long with a
 *     Hann window, so the windows sum to one and the level does not change
 *   - a grain's content is the input read at the ratio's rate, so the input's period appears in the
 *     output as period/ratio: that is the pitch shift itself
 *   - the content's clock walks the input at the input's own rate, so the singer reaches the end of
 *     the input exactly when the output reaches the end of the grain: the duration is kept
 *   - before a grain is taken, the input is searched for the place whose head best continues the grain
 *     before it: that is the alignment, and it is what keeps the carrier's phase locked
 *
 * Two clocks, and they must not be confused:
 *
 *   - the output's clock (src) walks the output's samples: one grain per source period
 *   - the content's clock (ana) walks the input's samples: it is the chain of the grains' places in
 *     the input
 *
 * The content's step is the part that needs care. Two grains overlap, and their carrier has to add
 * instead of cancelling, so the content's step must be a whole number of source periods away from the
 * step that the output's spacing asks for. If the ratio is a whole number the two agree and the step
 * is simply one period. If the ratio is not (a third, a fifth), the step has to be a period times
 * (the ratio's fraction plus a whole number), and the whole number is chosen so that the steps
 * average out to one period: the duration is kept, and the carrier's phase stays locked.
 *
 * The grains reach over the block's edges, so the sum is kept in an accumulator that the block reads
 * and clears: a grain placed in one block is still adding to the next block's samples.
 */
#include "vox.h"
#include "vox_math.h"

#define OLA_MASK (VOX_OLA_LEN - 1u)     /* the accumulator's index mask */
#define CORR_WIN 256u                   /* the alignment's comparison window, in output samples */
#define CORR_STRIDE 2u                  /* the correlation's taps are decimated by this */
#define SEARCH_MAX 256u                 /* the alignment's widest search, in output samples */
#define MIN_RATIO 0.5f                  /* the lowest shift: an octave down */
#define MAX_RATIO 2.0f                  /* the highest: an octave up */
#define MIN_PERIOD 32.0f                /* the shortest period the shifter accepts (1380 Hz) */
#define MAX_PERIOD 400.0f               /* and the longest (110 Hz) */
#define PERIOD_SMOOTH 0.25f             /* how much of the new period is taken (the rest is kept) */

/* the shifter's reset */
void vox_shift_init(vox_voice_t *v)
{
    uint32_t i;
    if (v->ring)
        for (i = 0; i < VOX_PSOLA_RING; i++)
            v->ring[i] = 0.0f;
    if (v->ola)
        for (i = 0; i < VOX_OLA_LEN; i++)
            v->ola[i] = 0.0f;
    for (i = 0; i < CORR_WIN; i++)
        v->tail[i] = 0.0f;
    v->ring_pos = 0;
    v->last_period = 0.0f;
    v->written = 0;
    v->delay = 0.0f;
    v->src = 0.0f;                      /* the output's clock */
    v->ana = 0.0f;                      /* the content's clock */
    v->frac_acc = 0.0f;                 /* the content step's accumulator */
    v->have_tail = 0;
}

/* the ring's sample at an absolute place, interpolated */
static float ring_at(const vox_voice_t *v, float pos)
{
    uint32_t idx;
    float frac;

    if (pos < 0.0f)
        return 0.0f;                       /* before the stream: silence */
    idx = (uint32_t)pos;                  /* the integer part */
    frac = pos - (float)idx;              /* and the fraction */
    idx %= VOX_PSOLA_RING;                /* the ring's index */
    {
        uint32_t i1 = (idx + 1u) % VOX_PSOLA_RING;
        return v->ring[idx] + (v->ring[i1] - v->ring[idx]) * frac;
    }
}

/* the shifter: the voice's output for a block.
 * in is the input block (mono), period the input's pitch period in samples (0 when unknown),
 * semitones the pitch shift, cents the fine shift. The result is in out. */
void vox_shift_process(vox_voice_t *v, const float *in, uint32_t n, float period, float semitones,
                       float cents, float *out)
{
    float ratio, block_start;
    uint32_t i;

    /* the input goes into the ring, so the shifter can look back over the block's edge */
    for (i = 0; i < n; i++) {
        v->ring[v->ring_pos] = in[i];
        v->ring_pos = (v->ring_pos + 1u) % VOX_PSOLA_RING;
    }
    v->written += n;
    block_start = (float)(v->written - n);

    /* the ratio: 2^(semitones/12) */
    ratio = vox_exp2f((semitones + cents * (1.0f / 100.0f)) * (1.0f / 12.0f));
    if (ratio < MIN_RATIO)
        ratio = MIN_RATIO;
    if (ratio > MAX_RATIO)
        ratio = MAX_RATIO;

    /* no period, or an impossible one: a plain copy, and the alignment is dropped (the next grain
     * must not be matched against a tail that belongs to a different voice) */
    if (period < MIN_PERIOD || period > MAX_PERIOD) {
        for (i = 0; i < VOX_OLA_LEN; i++)
            v->ola[i] = 0.0f;                  /* the pending grains are dropped */
        for (i = 0; i < n; i++)
            out[i] = in[i];
        v->have_tail = 0;
        v->last_period = 0.0f;
        v->src = block_start + (float)n + 512.0f;
        v->ana = v->src;
        return;
    }

    /* the period is smoothed, so the grain's size does not jump from block to block */
    if (v->last_period > MIN_PERIOD && v->last_period < MAX_PERIOD)
        period = v->last_period * (1.0f - PERIOD_SMOOTH) + period * PERIOD_SMOOTH;
    v->last_period = period;

    /* the grain: one period apart, two periods long */
    {
        float span_f = period * 2.0f;
        uint32_t span = (uint32_t)span_f;
        float wnorm;
        uint32_t search;
        int32_t step = 1;
        float frac = ratio - (float)(int32_t)ratio;   /* the ratio's fraction */
        float lag_min = period * ratio + 8.0f;        /* the content may not be newer than this */

        if (span < 8u)
            span = 8u;
        if (span > VOX_OLA_LEN - 2u)
            span = VOX_OLA_LEN - 2u;
        wnorm = 6.2831853f / (float)(span - 1u);

        /* the alignment's search: the place where the carrier's phase lines up again */
        search = (uint32_t)(period * 0.2f);
        if (search > SEARCH_MAX)
            search = SEARCH_MAX;
        if ((uint32_t)step > search)
            step = 1;

        /* The analysis delay: the grain reaches ratio*period samples into the input, so the content
         * has to be that far behind the writing head */
        v->delay = lag_min;

        /* A grain is placed when its first sample has not been read yet: the block reads its samples
         * after the grains are placed, so a grain whose first sample is still in the future is safe.
         * The output's clock therefore runs one period ahead of the read. */
        if (v->src < block_start + period)
            v->src = block_start + period;

        while (v->src < block_start + (float)n + period) {
            float nominal;
            float best = 0.0f;
            float center;
            uint32_t k;

            if (!v->have_tail) {
                /* The first grain of a phrase: the content starts well behind the writing head, so
                 * the grain never reads a sample that has not arrived. The output fades in as the
                 * content catches up. The margin leaves room for the alignment's own nudges. */
                nominal = v->src - lag_min - period * 2.0f;
                v->frac_acc = 0.0f;
            } else {
                /* The content's step: a whole number of source periods plus the ratio's fraction. The
                 * accumulator decides how often the long step is taken: the long step (one period
                 * more) is taken (1 - the fraction) of the time, so that the steps average one period.
                 * The duration is kept and the carrier's phase stays locked. */
                v->frac_acc += 1.0f - frac;
                nominal = v->ana + period * frac;
                if (v->frac_acc >= 1.0f) {
                    v->frac_acc -= 1.0f;
                    nominal += period;
                }
            }
            /* The alignment: the input place whose head best continues the grain before it. The
             * comparison is a difference, not a correlation: a correlation would prefer the loudest
             * place instead of the place whose shape matches, and the voice would wander. The
             * difference is taken in the input's own samples, at the ratio's spacing, so both sides
             * carry the same carrier. */
            if (v->have_tail && search > 0u) {
                float best_diff = 1e30f;
                int32_t off;
                int32_t half = (int32_t)period;      /* the grain's half, in whole samples */
                for (off = -(int32_t)search; off <= (int32_t)search; off += step) {
                    float diff = 0.0f;
                    uint32_t j;
                    for (j = 0; j < CORR_WIN; j += CORR_STRIDE) {
                        float d = ring_at(v, nominal + (float)off + ((float)((int32_t)j - half)) * ratio) -
                                  v->tail[j];
                        diff += d * d;
                    }
                    if (diff < best_diff) {
                        best_diff = diff;
                        best = (float)off;
                    }
                }
                /* a tail that says nothing (silence, noise) leaves the grain where it was */
                if (best_diff >= 1e30f)
                    best = 0.0f;
            }

            center = nominal + best;

            /* the grain: the input read at the ratio's rate, windowed, and added into the accumulator */
            for (k = 0; k < span; k++) {
                float out_pos = v->src - period + (float)k;      /* the output's place */
                float src_pos = center + ((float)k - period) * ratio;
                float w = 0.5f - 0.5f * vox_cosf(wnorm * (float)k);
                uint32_t idx;
                if (out_pos < 0.0f)
                    continue;                                   /* before the stream: nothing to add to */
                idx = ((uint32_t)out_pos) & OLA_MASK;
                v->ola[idx] += ring_at(v, src_pos) * w;
            }

            /* the tail for the next grain's alignment: the grain's own content at the output's times
             * from the grain's centre onwards */
            for (k = 0; k < CORR_WIN; k++)
                v->tail[k] = ring_at(v, center + (float)k * ratio);
            v->have_tail = 1;

            /* the two clocks: the output's takes the grain's step, the content's takes the grain's place */
            v->ana = center;
            v->src += period;
        }
    }

    /* the block's samples come out of the accumulator, and are cleared as they are read: a grain placed
     * in an earlier block is still adding to them, and a grain placed in this block reaches into the
     * next one */
    for (i = 0; i < n; i++) {
        uint32_t idx = ((uint32_t)block_start + i) & OLA_MASK;
        out[i] = v->ola[idx];
        v->ola[idx] = 0.0f;
    }
}
