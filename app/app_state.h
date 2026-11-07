/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the app's shared state.
 *
 * One struct holds the engine, the settings, the interface and the anatomy. The audio interrupt and the
 * UI frame both touch it, so the fields are split by who owns them:
 *   - the render owns the engine (it writes it, the UI only reads what it publishes)
 *   - the frame owns the interface and the settings
 *   - the published block (level, the detected note, the corrected pitch) is written by the render and
 *     read by the UI; it is a small set of scalars, so a torn read is not a practical concern, but the
 *     UI reads them once per frame into its own copy.
 */
#pragma once
#include <stdint.h>
#include "vox.h"
#include "ui.h"
#include "anatomy.h"

/* what the render publishes for the display */
typedef struct {
    float level;                       /* the input's level, 0..1 */
    float peak;                        /* the input's peak, 0..1 */
    float f0;                          /* the detected fundamental, Hz */
    float target_f0;                   /* the corrected fundamental, Hz */
    float clarity;                     /* the detector's confidence */
    int8_t note;                       /* the detected MIDI note, -1 when none */
    int8_t octave;
    int8_t target_note;
    int8_t voiced;                     /* 1 when the input is voiced */
} vox_published_t;

/* the global settings: the microphone, the output and the last-used preset. They stay out of the presets. */
typedef struct {
    uint32_t magic;                    /* SET_MAGIC */
    uint8_t mic_on;                    /* the USB microphone's host mode */
    uint8_t mic_gain;                  /* 0..100 */
    uint8_t out_level;                 /* 0..100 (the MASTER pot does the real work) */
    uint8_t last_preset;               /* 0..31, the preset that was loaded */
    uint8_t last_page;                 /* the page the instrument was left on */
    uint8_t rsv[2];
} vox_globals_t;

#define SET_MAGIC 0x31545041u           /* "ATP1" */

/* the state of the microphone (the platform's capture) */
typedef struct {
    uint8_t state;                     /* 0 none, 1 connected, 2 streaming, 3 error */
    uint32_t got;                      /* the samples the last render took */
    uint32_t rate;                     /* the device's sample rate */
    uint8_t channels;
    uint8_t bits;
    uint32_t overruns;                 /* the blocks the capture could not deliver */
} vox_mic_t;

struct app_state {
    /* the audio interrupt's own */
    vox_engine_t engine;
    vox_mic_t mic;
    vox_published_t pub;

    /* the frame's own */
    ui_t ui;
    anatomy_t anatomy;
    vox_globals_t globals;             /* the global settings (the microphone, the output) */

    /* the presets */
    vox_preset_t factory[VOX_FACTORY_PRESETS];
    vox_preset_t user[VOX_USER_PRESETS];
    char user_name[VOX_USER_PRESETS][16]; /* the user slots' names (the factory's names are built in) */
    uint8_t current;                   /* which preset is loaded: 0..15 factory, 16..31 user */
    uint8_t dirty;                     /* the settings changed and must be saved */
    uint32_t save_at_ms;               /* when the settings are saved (the flash write stops the sound) */

    /* the input's own */
    uint32_t buttons;                  /* the buttons held now */
    uint32_t buttons_prev;             /* the buttons held last time (the edges) */
    int32_t enc[NE];                   /* the encoders' detents since the last frame */
    uint32_t keys;                     /* the keys held now */
    uint32_t master;                   /* the MASTER pot, 0..4096 */
    uint32_t frame_ms;                 /* the clock */
    uint8_t overload;                  /* the render was over its budget */
};
