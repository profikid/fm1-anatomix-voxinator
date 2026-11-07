/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the GLIDE screen: the folds, the detected pitch and the corrected one.
 *
 * The larynx is magnified: the detected pitch's folds are drawn bright, the corrected pitch's ghost
 * pair beside them, so the correction can be seen. The two notes and the distance between them (in
 * cents) are the readouts.
 */

#include "plat.h"
#include "ui.h"
#include "app_state.h"
#include "anatomy.h"
/* the anatomy's input and its step (ui_common.c) */
void ui_anatomy_in(app_state_t *a, anatomy_in_t *in);
void ui_anatomy_step(app_state_t *a, const anatomy_in_t *in);
/* the picture, magnified (anatomy_formants.c, anatomy_vocal_folds.c) */
void an_head_zoom(const anatomy_in_t *in, int32_t x, int32_t y, int32_t w, int32_t h, int32_t sx,
                  int32_t sy, int32_t sw, int32_t sh);
void an_folds_zoom(const anatomy_t *a, const anatomy_in_t *in, int32_t x, int32_t y, int32_t w, int32_t h,
                   int32_t sx, int32_t sy, int32_t sw, int32_t sh);
/* the notes and the numbers (ui_common.c) */
void ui_note_text(int note, char *out);
/* the bands and the foot's redraw flag (ui_common.c) */
void ui_band_header(app_state_t *a, const char *title, const char *right, int32_t x, int32_t y, int32_t w);
void ui_band_footer(app_state_t *a, const char *const *knobs, int32_t x, int32_t y, int32_t w);
int ui_foot_is_dirty(const app_state_t *a);
void ui_foot_clear(app_state_t *a);
void ui_int_text(int32_t v, char *out);
void ui_signed_text(int32_t v, const char *unit, char *out);

void ui_draw_glide(app_state_t *a, int32_t x, int32_t y, int32_t w, int32_t h)
{
    static const char *const K[4] = {"RETUNE", "GLIDE", "HUMANIZE", "MIX"};
    /* the design window: the larynx and the folds around it, magnified to the band */
    const int32_t sx = 86, sy = 62, sw = 86, sh = 63;
    anatomy_in_t in;
    char note[8], t[16];
    int32_t nw;
    ui_anatomy_in(a, &in);
    ui_anatomy_step(a, &in);
    ui_band_header(a, "GLIDE", "ANATOMIX", x, y, w);
    cv_begin(w, 176, AN_DARK);
    an_head_zoom(&in, 0, 0, w, 176, sx, sy, sw, sh);
    an_folds_zoom(&a->anatomy, &in, 0, 0, w, 176, sx, sy, sw, sh);

    /* the detected pitch, top left */
    cv_rect(2, 2, 62, 32, an_fade(AN_DARK, AN_DARK, 0.0f));
    cv_rect(2, 2, 62, 1, AN_DIM);
    cv_text(5, 4, &FONT_T, "DETECTED", AN_DIM);
    if (a->pub.note >= 0) {
        ui_note_text(a->pub.note, note);
        cv_text(5, 16, &FONT_B, note, a->pub.voiced ? AN_CYAN : AN_DIM);
    } else
        cv_text(5, 16, &FONT_B, "--", AN_DIM);

    /* the corrected pitch, top right */
    cv_rect(176, 2, 62, 32, AN_DARK);
    cv_rect(176, 2, 62, 1, AN_DIM);
    nw = text_w(&FONT_T, "TARGET");
    cv_text(235 - nw, 4, &FONT_T, "TARGET", AN_DIM);
    if (a->pub.target_note >= 0) {
        ui_note_text(a->pub.target_note, note);
        nw = text_w(&FONT_B, note);
        cv_text(235 - nw, 16, &FONT_B, note, AN_AMB);
    } else
        cv_text(220, 16, &FONT_B, "--", AN_DIM);

    /* the distance between them, in cents, on a bar */
    {
        float cents = 0.0f;
        int32_t mark;
        if (a->pub.f0 > 1.0f && a->pub.target_f0 > 1.0f)
            cents = 12.0f * vox_log2f(a->pub.target_f0 / a->pub.f0) * 100.0f;
        cv_rect(2, 150, 236, 24, AN_DARK);
        cv_rect(2, 150, 236, 1, AN_DIM);
        ui_signed_text((int32_t)(cents < 0.0f ? cents - 0.5f : cents + 0.5f), "CENTS", t);
        cv_text(5, 153, &FONT_T, t, AN_AMB);
        cv_rect(5, 167, 230, 3, an_fade(AN_DIM, AN_DARK, 0.5f));
        cv_rect(119, 167, 2, 3, AN_DIM);
        mark = 120 + (int32_t)(cents * 1.4f);
        if (mark < 5)
            mark = 5;
        if (mark > 233)
            mark = 233;
        cv_rect(mark - 1, 165, 3, 7, cents > 0.5f ? AN_AMB : (cents < -0.5f ? AN_CYAN : AN_DIM));
    }
    cv_blit(x, y + 28);
    if (ui_foot_is_dirty(a)) {
        ui_band_footer(a, K, x, y + 204, w);
        ui_foot_clear(a);
    }
}
