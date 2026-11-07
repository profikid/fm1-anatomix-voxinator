/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the interface.
 *
 * Eight screens (the PRD's §8.1), one page model, and the FM-1's physical controls mapped to them.
 * The screens are compiled into the same unit as the platform's gfx.c, so they use its drawing primitives
 * (cv_begin, cv_rect, cv_line, cv_text, cv_pset, cv_blit) and its fonts (FONT_XS, FONT_S, FONT_B, FONT_M).
 *
 * The screens draw from the app's shared state (app_state.h), which they only ever see through a pointer:
 * ui.h stays independent of the app's own header, so nothing here can create a dependency cycle.
 */
#pragma once
#include <stdint.h>
#include "vox.h"

/* the app's shared state (app_state.h defines it) */
typedef struct app_state app_state_t;

/* the pages */
enum {
    PG_HOME = 0,          /* the anatomical overview */
    PG_MULTIVOICE,        /* the harmony and the voices */
    PG_GLIDE,             /* the auto-tune */
    PG_FORMANT,           /* the vocal tract */
    PG_FX,                /* the effects chain */
    PG_PRESETS,           /* the factory and the user presets */
    PG_INPUT,             /* the microphone */
    PG_GLOBAL,            /* the system */
    NPAGES
};

/* the interface's state */
typedef struct {
    uint8_t page;                     /* the page on screen */
    uint8_t prev_page;                /* the page before the last change (the A/B) */
    uint8_t fx_focus;                 /* the focused effect slot, 0..3 */
    uint8_t preset_list;              /* the preset browser's list: 0 factory, 1 user */
    uint8_t preset_sel;               /* the preset the browser has on */
    uint8_t dirty;                    /* the screen must be redrawn */
    uint8_t show_help;                /* the help overlay */
    uint32_t now_ms;
    uint32_t last_frame_ms;
} ui_t;

/* the interface's reset */
void ui_init(ui_t *u);

/* the page's name */
const char *ui_page_name(int page);

/* the page's four knob names (K1..K4) */
const char *ui_knob_name(int page, int knob);

/* the input: the buttons and the knobs.
 * buttons is the platform's held-button mask (bit B_*), keys the held keys (bit k = the chromatic key k
 * from F3), enc the seven encoders' detents since the last call (index EN_*), master the MASTER pot. */
void ui_input(app_state_t *a, uint32_t buttons, uint32_t keys, const int32_t enc[NE], uint32_t master);

/* the drawing: the current page into the canvas */
void ui_draw(app_state_t *a, int32_t x, int32_t y, int32_t w, int32_t h);

/* one page's drawing (the screens call these) */
void ui_draw_home(app_state_t *a, int32_t x, int32_t y, int32_t w, int32_t h);
void ui_draw_multivoice(app_state_t *a, int32_t x, int32_t y, int32_t w, int32_t h);
void ui_draw_glide(app_state_t *a, int32_t x, int32_t y, int32_t w, int32_t h);
void ui_draw_formant(app_state_t *a, int32_t x, int32_t y, int32_t w, int32_t h);
void ui_draw_fx(app_state_t *a, int32_t x, int32_t y, int32_t w, int32_t h);
void ui_draw_presets(app_state_t *a, int32_t x, int32_t y, int32_t w, int32_t h);
void ui_draw_input(app_state_t *a, int32_t x, int32_t y, int32_t w, int32_t h);
void ui_draw_global(app_state_t *a, int32_t x, int32_t y, int32_t w, int32_t h);

/* the shared drawing helpers the screens use (ui/common.c) */
void ui_header(app_state_t *a, const char *title, const char *right);
void ui_footer(app_state_t *a, const char *const *knobs);
void ui_meter(app_state_t *a, int32_t x, int32_t y, int32_t w, int32_t h, float level, int red);
void ui_value_bar(app_state_t *a, int32_t x, int32_t y, int32_t w, float value, uint16_t colour);
void ui_panel(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t fill);
