/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the scale-aware harmoniser.
 *
 * The plan is the set of intervals the voices sing, in semitones from the lead. The lead is always voice 0;
 * the others follow the chosen harmony, and the scale decides which notes that harmony is made of: the
 * same "third" is a minor third in C minor and a major third in C major.
 *
 * The plan is a pure function of the scale, the note and the parameters, so a preset plays the same way
 * every time (the chaos uses a seeded generator).
 */
#include "vox.h"
#include "vox_math.h"

/* the twelve harmony stacks, in semitones from the lead.
 * Each row is a four-note chord; the row the harmony index picks is then moved into the scale. */
static const int8_t HARM_STACK[12][3] = {
    {3, 7, 10},        /* 0: a minor seventh */
    {4, 7, 11},        /* 1: a major seventh */
    {3, 7, 12},        /* 2: a minor seventh, an octave up */
    {5, 7, 12},        /* 3: a major sixth */
    {2, 7, 9},         /* 4: a suspended fourth */
    {3, 7, 9},         /* 5: a major sixth */
    {4, 7, 10},        /* 6: a dominant seventh */
    {3, 7, 10},        /* 7: a minor seventh (the default) */
    {4, 7, 9},         /* 8: a major sixth */
    {5, 8, 12},        /* 9: a major sixth, an octave up */
    {2, 7, 11},        /* 10: a suspended fourth */
    {3, 8, 12}         /* 11: a minor sixth, an octave up */
};

/* the harmoniser's reset */
void vox_harm_init(vox_harm_t *h)
{
    h->count = 1;
    h->interval[0] = 0;
    h->interval[1] = 3;
    h->interval[2] = 7;
    h->interval[3] = -12;
    h->spread = 50;
    h->detune = 20;
    h->character = 30;
    h->humanize = 20;
    h->chaos = 0;
    h->harmony = 7;
    h->seed = 0x1B0D3A5u;
}

/* the harmoniser's plan for the current note */
void vox_harm_plan(vox_harm_t *h, const vox_scale_t *scale, int8_t note, int8_t octave, uint32_t now_ms)
{
    uint32_t i;
    int base = (int)note;
    int stack = (int)h->harmony % 12;

    (void)now_ms;
    (void)octave;

    h->interval[0] = 0;
    h->count = 1;
    if (base < 0)
        return;

    /* the voice count: at least the lead, at most the four the instrument has */
    if (h->count < 1)
        h->count = 1;

    for (i = 1; i < VOX_MAX_VOICES; i++) {
        int want = base + HARM_STACK[stack][i - 1u];
        /* the scale decides: the wanted note moves to the nearest note of the scale */
        {
            int steps[12];
            int n = vox_scale_steps(scale, steps);
            int best = want, bestd = 99, k;
            for (k = 0; k < n; k++) {
                int pc = (((scale->root + steps[k]) % 12) + 12) % 12;
                int cand = want;
                while (((cand % 12) + 12) % 12 != pc)
                    cand--;
                {
                    int d = want - cand;
                    if (d < 0)
                        d = -d;
                    if (d < bestd) {
                        bestd = d;
                        best = cand;
                    }
                }
                {
                    cand = want;
                    while (((cand % 12) + 12) % 12 != pc)
                        cand++;
                    {
                        int d = cand - want;
                        if (d < 0)
                            d = -d;
                        if (d < bestd) {
                            bestd = d;
                            best = cand;
                        }
                    }
                }
            }
            want = best;
        }
        h->interval[i] = (int8_t)(want - base);
    }
}

/* the voice's pitch offset, in semitones, from the plan */
float vox_harm_offset(const vox_harm_t *h, uint32_t voice, float detune_cents, uint32_t now_ms)
{
    float off;
    uint32_t seed = h->seed ^ (voice * 0x9E3779B9u);

    if (voice >= VOX_MAX_VOICES)
        return 0.0f;
    off = (float)h->interval[voice];

    /* the detune: the voices are pulled apart by a few cents each */
    if (h->detune > 0.0f) {
        float amt = (float)h->detune * (1.0f / 100.0f);
        off += detune_cents * 0.01f * amt;
    }

    /* the humanize: a slow drift per voice */
    if (h->humanize > 0.0f) {
        float amt = (float)h->humanize * (1.0f / 100.0f);
        float t = (float)(now_ms % 60000u) * (1.0f / 1000.0f);
        off += vox_sin_turns(t * (0.37f + 0.11f * (float)voice) * (1.0f / 60.0f)) * 18.0f * amt * 0.01f;
    }

    /* the chaos: a random but reproducible offset, and the character makes it harsher */
    if (h->chaos > 0.0f) {
        float amt = (float)h->chaos * (1.0f / 100.0f);
        float r = vox_randf(&seed);
        off += (r - 0.5f) * 24.0f * amt * 0.01f;
    }
    return off;
}

/* ------------------------------------------------------------------ the scales --- */

/* the scale's note names, for the display */
char *vox_scale_note_name(const vox_scale_t *scale, int pitch_class, char *out)
{
    static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int root = scale ? (int)scale->root : 0;
    int pc = ((pitch_class - root) % 12 + 12) % 12;
    const char *n = N[pc];
    int i = 0;

    while (n[i] && i < 3) {
        out[i] = n[i];
        i++;
    }
    out[i] = 0;
    return out;
}

/* the scale's pitch classes, in semitones from the root */
int vox_scale_steps(const vox_scale_t *scale, int *out)
{
    static const int MAJOR[7] = {0, 2, 4, 5, 7, 9, 11};
    static const int MINOR[7] = {0, 2, 3, 5, 7, 8, 10};
    static const int PENTA_MAJ[5] = {0, 2, 4, 7, 9};
    static const int PENTA_MIN[5] = {0, 3, 5, 7, 10};
    static const int DORIAN[7] = {0, 2, 3, 5, 7, 9, 10};
    static const int BLUES[6] = {0, 3, 5, 6, 7, 10};
    const int *set;
    int n, i;

    if (!scale)
        return 0;
    switch (scale->scale) {
    case VOX_SCALE_MAJOR:      set = MAJOR; n = 7; break;
    case VOX_SCALE_MINOR:      set = MINOR; n = 7; break;
    case VOX_SCALE_MAJOR_PENTA:set = PENTA_MAJ; n = 5; break;
    case VOX_SCALE_MINOR_PENTA:set = PENTA_MIN; n = 5; break;
    case VOX_SCALE_DORIAN:     set = DORIAN; n = 7; break;
    case VOX_SCALE_BLUES:      set = BLUES; n = 6; break;
    case VOX_SCALE_CUSTOM:
        n = 0;
        for (i = 0; i < 12; i++)
            if (scale->custom[i])
                out[n++] = i;
        return n;
    default:                   /* the chromatic scale: every note */
        n = 0;
        for (i = 0; i < 12; i++)
            out[n++] = i;
        return n;
    }
    for (i = 0; i < n; i++)
        out[i] = set[i];
    return n;
}

/* is a pitch class in the scale? */
int vox_scale_has(const vox_scale_t *scale, int pitch_class)
{
    int steps[12];
    int n, i;
    int root = scale ? (int)scale->root : 0;
    int pc = ((pitch_class - root) % 12 + 12) % 12;

    n = vox_scale_steps(scale, steps);
    for (i = 0; i < n; i++)
        if (steps[i] == pc)
            return 1;
    return 0;
}

/* the scale's defaults */
void vox_scale_default(vox_scale_t *scale, int mode, int root)
{
    uint32_t i;

    scale->root = (uint8_t)(((root % 12) + 12) % 12);
    scale->scale = (uint8_t)(mode >= 0 && mode < VOX_SCALE_COUNT ? mode : VOX_SCALE_CHROMATIC);
    for (i = 0; i < 12; i++)
        scale->custom[i] = 1;
}

/* the note's name, for the display: "A4", "C#3" */
const char *vox_note_name(int note, char *out)
{
    static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int pc = ((note % 12) + 12) % 12;
    int oct = note / 12 - 1;
    const char *n = N[pc];
    int i = 0;

    while (n[i] && i < 2) {
        out[i] = n[i];
        i++;
    }
    out[i++] = (char)('0' + (oct < 0 ? 0 : (oct > 9 ? 9 : oct)));
    out[i] = 0;
    return out;
}
