/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the anatomical visualisation.
 *
 * The anatomy is the instrument's interface, not decoration: every parameter that changes the sound changes
 * the picture. These functions draw into the platform's canvas (cv_rect, cv_line, cv_pset, cv_text — the
 * platform's drawing primitives, available because the app's screens are compiled into the same unit as
 * the platform's gfx.c).
 *
 * The colour language (the PRD's §4.3): a dark laboratory ground, warm anatomical pink and coral, bone
 * white for the outlines, amber and cyan for the DSP's readouts.
 */
#pragma once
#include <stdint.h>

/* what the anatomy reacts to (the app fills this in from the engine's state) */
typedef struct {
    float level;           /* the input's envelope, 0..1 */
    float peak;            /* the input's peak, 0..1 */
    float f0;              /* the detected fundamental, Hz (0 = unvoiced) */
    float target_f0;       /* the corrected fundamental, Hz */
    float clarity;         /* the detector's confidence, 0..1 */
    int voiced;            /* 1 when the input is voiced */
    float formant;         /* the formant shift, -12..+12 */
    float nasal;           /* the nasal resonance, 0..1 */
    float distort;         /* the distortion, 0..1 */
    float delay_amt;       /* the delay's feedback, 0..1 */
    float reverb_amt;      /* the reverb's tail, 0..1 */
    float compress;        /* the compression, 0..1 */
    float overload;        /* 1 while the input is clipping */
    int voices;            /* the number of active voices (1..4) */
    float voice_f0[4];     /* each voice's pitch, Hz */
    float voice_level[4];  /* each voice's level, 0..1 */
    uint32_t now_ms;       /* the clock, for the animation's phases */
} anatomy_in_t;

/* the animation's own state: the phases and the particles */
typedef struct {
    float breath;          /* the breathing phase, 0..1 */
    float fold_phase[4];   /* each vocal fold's phase, 0..1 */
    float particles[32];   /* the airflow particles' positions along the tract, 0..1 */
    uint8_t n_particles;
    float rings[6];        /* the reverb's expanding rings, 0..1 */
    uint8_t n_rings;
    uint32_t seed;
    uint32_t last_ms;
    uint32_t glitch_until;
} anatomy_t;

/* the anatomy's colours (RGB565) */
#define AN_BONE RGB(226, 218, 198)
#define AN_PINK RGB(226, 118, 132)
#define AN_RED RGB(178, 52, 62)
#define AN_CORAL RGB(240, 150, 128)
#define AN_TISSUE RGB(150, 58, 72)
#define AN_DARK RGB(22, 20, 26)
#define AN_LAB RGB(226, 118, 132)
#define AN_AMB RGB(255, 184, 70)
#define AN_CYAN RGB(120, 214, 246)
#define AN_DIM RGB(86, 78, 88)

/* the anatomy's reset */
void anatomy_init(anatomy_t *a);

/* the anatomy's update: the phases, the particles and the rings move.
 * dt_ms is how long since the last update; now_ms is the clock. */
void anatomy_update(anatomy_t *a, const anatomy_in_t *in, uint32_t now_ms);

/* the drawings. Each draws inside the rectangle (x, y, w, h). */
void anatomy_draw_head(anatomy_t *a, const anatomy_in_t *in, int32_t x, int32_t y, int32_t w, int32_t h);
void anatomy_draw_lungs(anatomy_t *a, const anatomy_in_t *in, int32_t x, int32_t y, int32_t w, int32_t h);
void anatomy_draw_folds(anatomy_t *a, const anatomy_in_t *in, int32_t x, int32_t y, int32_t w, int32_t h);
void anatomy_draw_airflow(anatomy_t *a, const anatomy_in_t *in, int32_t x, int32_t y, int32_t w, int32_t h);
void anatomy_draw_formants(anatomy_t *a, const anatomy_in_t *in, int32_t x, int32_t y, int32_t w, int32_t h);
void anatomy_draw_effects(anatomy_t *a, const anatomy_in_t *in, int32_t x, int32_t y, int32_t w, int32_t h);
