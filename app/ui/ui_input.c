/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the INPUT screen: the microphone's configuration.
 *
 * The signal's way through the instrument is drawn: the microphone, the gain, the gate, the pitch
 * detector and the engine, each with its live value. Below them the capture's own state.
 */

#include "plat.h"
#include "ui.h"
#include "app_state.h"
#include "anatomy.h"
/* the numbers (ui_common.c) */
void ui_int_text(int32_t v, char *out);
int ui_monitor(const app_state_t *a);
void ui_pct_text(float v, char *out);
void ui_note_text(int note, char *out);
/* the bands and the foot's redraw flag (ui_common.c) */
void ui_band_header(app_state_t *a, const char *title, const char *right, int32_t x, int32_t y, int32_t w);
void ui_band_footer(app_state_t *a, const char *const *knobs, int32_t x, int32_t y, int32_t w);
int ui_foot_is_dirty(const app_state_t *a);
void ui_foot_clear(app_state_t *a);

void ui_draw_input(app_state_t *a, int32_t x, int32_t y, int32_t w, int32_t h)
{
    static const char *const K[4] = {"GAIN", "MONITOR", "FORMAT", "HOST"};
    static const char *const ST[4] = {"NONE", "READY", "STREAM", "ERROR"};
    char t[16];
    int i;
    ui_band_header(a, "INPUT", "ANATOMIX", x, y, w);
    cv_begin(w, 176, AN_DARK);

    /* the chain */
    for (i = 0; i < 5; i++) {
        int32_t px = 4 + i * 47, py = 8;
        const char *nm = i == 0 ? "MIC" : i == 1 ? "GAIN" : i == 2 ? "GATE" : i == 3 ? "PITCH" : "ENGINE";
        cv_rect(px, py, 43, 40, an_fade(AN_DIM, AN_DARK, 0.75f));
        cv_rect(px, py, 43, 1, an_fade(AN_DIM, AN_BONE, 0.3f));
        cv_text(px + 3, py + 3, &FONT_T, nm, AN_DIM);
        t[0] = 0;
        if (i == 0)
            ui_copy(t, ST[a->mic.state > 3 ? 3 : a->mic.state], 12);
        else if (i == 1)
            ui_pct_text((float)a->globals.mic_gain * 0.01f, t);
        else if (i == 2)
            ui_pct_text((float)a->engine.set.in_gate * 0.01f, t);
        else if (i == 3) {
            if (a->pub.note >= 0)
                ui_note_text(a->pub.note, t);
        } else
            ui_copy(t, a->engine.set.mode ? "GLIDE" : "MULTI", 12);
        cv_text(px + 3, py + 22, &FONT_XS, t, i == 0 ? AN_AMB : AN_CYAN);
        if (i < 4)
            cv_line(px + 44, py + 20, px + 46, py + 20, AN_DIM);
    }

    /* the capture's own state */
    cv_rect(4, 58, 232, 1, AN_DIM);
    cv_text(6, 62, &FONT_T, "CAPTURE", AN_DIM);
    ui_int_text((int32_t)(a->mic.rate / 1000u), t);
    {
        int n = 0;
        while (t[n])
            n++;
        t[n++] = 'K';
        t[n] = 0;
    }
    cv_text(80, 62, &FONT_T, t, AN_AMB);
    cv_text(118, 62, &FONT_T, "BIT", AN_DIM);
    ui_int_text((int32_t)a->mic.bits, t);
    cv_text(146, 62, &FONT_T, t, AN_AMB);
    cv_text(170, 62, &FONT_T, "CH", AN_DIM);
    ui_int_text((int32_t)a->mic.channels, t);
    cv_text(190, 62, &FONT_T, t, AN_AMB);

    cv_text(6, 84, &FONT_T, "OVERRUNS", AN_DIM);
    ui_int_text((int32_t)a->mic.overruns, t);
    cv_text(80, 84, &FONT_T, t, a->mic.overruns ? AN_RED : AN_AMB);

    cv_text(6, 106, &FONT_T, "MONITOR", AN_DIM);
    cv_text(80, 106, &FONT_T, ui_monitor(a) ? "ON" : "OFF", ui_monitor(a) ? AN_AMB : AN_DIM);
    cv_text(6, 128, &FONT_T, "USB HOST", AN_DIM);
    cv_text(80, 128, &FONT_T, a->globals.mic_on ? "ON" : "OFF", a->globals.mic_on ? AN_AMB : AN_DIM);

    /* the input's meter, big */
    cv_text(6, 150, &FONT_T, "LEVEL", AN_DIM);
    cv_rect(56, 152, 180, 8, an_fade(AN_DIM, AN_DARK, 0.55f));
    cv_rect(56, 152, (int32_t)(180.0f * (a->pub.level > 1.0f ? 1.0f : a->pub.level)), 8,
            a->pub.peak > 0.985f ? AN_RED : AN_CYAN);
    cv_rect(56, 152, 180, 1, an_fade(AN_BONE, AN_DARK, 0.45f));
    cv_blit(x, y + 28);
    if (ui_foot_is_dirty(a)) {
        ui_band_footer(a, K, x, y + 204, w);
        ui_foot_clear(a);
    }
}
