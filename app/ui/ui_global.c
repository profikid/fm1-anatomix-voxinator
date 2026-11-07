/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the GLOBAL screen: MIDI, the audio and the system.
 *
 * What the instrument is connected to and how it is set up: where the MIDI goes, the output's level,
 * the screen's theme and the build's version.
 */

#include "plat.h"
#include "ui.h"
#include "app_state.h"
#include "anatomy.h"
/* the build's version (the app's header gives it to the platform) */
#ifdef APP_VERSION
#define APP_VERSION_TEXT APP_VERSION
#else
#define APP_VERSION_TEXT "DEV"
#endif

/* the numbers (ui_common.c) */
void ui_int_text(int32_t v, char *out);
void ui_pct_text(float v, char *out);
void ui_copy(char *out, const char *src, int max);
/* the MIDI's destination (ui_common.c) */
const char *ui_midi_name(int d);
/* the bands and the foot's redraw flag (ui_common.c) */
void ui_band_header(app_state_t *a, const char *title, const char *right, int32_t x, int32_t y, int32_t w);
void ui_band_footer(app_state_t *a, const char *const *knobs, int32_t x, int32_t y, int32_t w);
int ui_foot_is_dirty(const app_state_t *a);
void ui_foot_clear(app_state_t *a);

void ui_draw_global(app_state_t *a, int32_t x, int32_t y, int32_t w, int32_t h)
{
    static const char *const K[4] = {"MIDI", "OUT", "THEME", "VERSION"};
    static const char *const THEME[5] = {"GREEN", "AMBER", "CYAN", "RED", "MONO"};
    char t[16];
    int i;
    ui_band_header(a, "GLOBAL", "ANATOMIX", x, y, w);
    cv_begin(w, 176, AN_DARK);

    /* the MIDI */
    cv_rect(4, 6, 232, 56, an_fade(AN_DIM, AN_DARK, 0.8f));
    cv_text(8, 10, &FONT_T, "MIDI", AN_DIM);
    cv_text(52, 10, &FONT_T, "TO", AN_DIM);
    cv_text(68, 10, &FONT_T, ui_midi_name(ui_midi_dest(a)), AN_AMB);
    cv_text(140, 10, &FONT_T, "ROOT", AN_DIM);
    {
        const char *nm = "C";
        static const char *const NOTES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        nm = NOTES[a->engine.set.scale.root % 12u];
        cv_text(168, 10, &FONT_T, nm, AN_AMB);
    }
    cv_text(8, 32, &FONT_T, "MIDI IN", AN_DIM);
    ui_int_text((int32_t)plat_midi_count(PORT_USB, 0), t);
    cv_text(60, 32, &FONT_T, t, AN_CYAN);
    cv_text(100, 32, &FONT_T, "BT", AN_DIM);
    ui_int_text((int32_t)plat_midi_count(PORT_BLE, 0), t);
    cv_text(120, 32, &FONT_T, t, AN_CYAN);
    cv_text(8, 48, &FONT_T, "RADIO", AN_DIM);
    {
        const char *bs = "OFF";
        if (plat_ble_state() == BLE_READY)
            bs = "READY";
        else if (plat_ble_state() == BLE_STARTING)
            bs = "SCAN";
        else if (plat_ble_state() == BLE_FAILED)
            bs = "FAIL";
        cv_text(60, 48, &FONT_T, bs, AN_AMB);
    }

    /* the audio */
    cv_rect(4, 70, 232, 44, an_fade(AN_DIM, AN_DARK, 0.8f));
    cv_text(8, 74, &FONT_T, "AUDIO", AN_DIM);
    cv_text(52, 74, &FONT_T, "OUT", AN_DIM);
    ui_pct_text((float)a->globals.out_level * 0.01f, t);
    cv_text(80, 74, &FONT_T, t, AN_AMB);
    cv_text(130, 74, &FONT_T, "RATE", AN_DIM);
    ui_int_text((int32_t)(a->mic.rate / 1000u), t);
    cv_text(166, 74, &FONT_T, t, AN_AMB);
    cv_text(8, 94, &FONT_T, "BLOCK", AN_DIM);
    ui_int_text((int32_t)VOX_BLOCK, t);
    cv_text(60, 94, &FONT_T, t, AN_CYAN);

    /* the system */
    cv_rect(4, 122, 232, 44, an_fade(AN_DIM, AN_DARK, 0.8f));
    cv_text(8, 126, &FONT_T, "SYSTEM", AN_DIM);
    cv_text(52, 126, &FONT_T, "THEME", AN_DIM);
    ui_copy(t, THEME[ui_theme() % 5], 12);
    cv_text(96, 126, &FONT_T, t, AN_AMB);
    /* the theme's colours, as a little strip (the platform's own palette table) */
    for (i = 0; i < 5; i++)
        cv_rect(150 + i * 12, 126, 10, 10, PALETTES[ui_theme() % (int)NPALETTES].c[i]);
    cv_text(8, 146, &FONT_T, "VERSION", AN_DIM);
    cv_text(60, 146, &FONT_T, APP_VERSION_TEXT, AN_CYAN);
    cv_blit(x, y + 28);
    if (ui_foot_is_dirty(a)) {
        ui_band_footer(a, K, x, y + 204, w);
        ui_foot_clear(a);
    }
}
