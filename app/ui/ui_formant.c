/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the FORMANT screen: the enlarged head and the vocal tract.
 *
 * The tract is shown close: the pharynx's length, the oral cavity and the tongue's place all follow
 * the formant shift, and the nasal port opens with the nasal amount. The readouts are the shift
 * itself, the throat's amount and the nasal share.
 */

#include "plat.h"
#include "ui.h"
#include "app_state.h"
#include "anatomy.h"
/* the anatomy's input and its step (ui_common.c) */
void ui_anatomy_in(app_state_t *a, anatomy_in_t *in);
void ui_anatomy_step(app_state_t *a, const anatomy_in_t *in);
/* the picture, magnified (anatomy_formants.c) */
void an_head_zoom(const anatomy_in_t *in, int32_t x, int32_t y, int32_t w, int32_t h, int32_t sx,
                  int32_t sy, int32_t sw, int32_t sh);
/* the numbers (ui_common.c) */
void ui_int_text(int32_t v, char *out);
/* the bands and the foot's redraw flag (ui_common.c) */
void ui_band_header(app_state_t *a, const char *title, const char *right, int32_t x, int32_t y, int32_t w);
void ui_band_footer(app_state_t *a, const char *const *knobs, int32_t x, int32_t y, int32_t w);
int ui_foot_is_dirty(const app_state_t *a);
void ui_foot_clear(app_state_t *a);
void ui_signed_text(int32_t v, const char *unit, char *out);
void ui_pct_text(float v, char *out);

void ui_draw_formant(app_state_t *a, int32_t x, int32_t y, int32_t w, int32_t h)
{
    static const char *const K[4] = {"FORMANT", "THROAT", "NASAL", "PRESERVE"};
    /* the design window: the head and the throat, magnified to the band */
    const int32_t sx = 44, sy = -12, sw = 164, sh = 120;
    anatomy_in_t in;
    char t[16];
    ui_anatomy_in(a, &in);
    ui_anatomy_step(a, &in);
    ui_band_header(a, "FORMANT", "ANATOMIX", x, y, w);
    cv_begin(w, 176, AN_DARK);
    an_head_zoom(&in, 0, 0, w, 176, sx, sy, sw, sh);

    /* the readouts, in a panel on the right */
    cv_rect(150, 4, 88, 74, AN_DARK);
    cv_rect(150, 4, 88, 1, AN_DIM);
    cv_text(154, 8, &FONT_T, "FORMANT", AN_DIM);
    ui_signed_text(a->engine.set.formant, "ST", t);
    cv_text(154, 20, &FONT_B, t, a->engine.set.formant > 0 ? AN_AMB : AN_CYAN);
    cv_text(154, 40, &FONT_T, "THROAT", AN_DIM);
    ui_pct_text((float)a->engine.set.formant_amt * 0.01f, t);
    cv_text(154, 52, &FONT_T, t, AN_AMB);
    cv_text(154, 66, &FONT_T, "NASAL", AN_DIM);

    /* the nasal port's share, as a bar under the panel */
    cv_rect(154, 78, 80, 5, an_fade(AN_DIM, AN_DARK, 0.5f));
    cv_rect(154, 78, (int32_t)(80.0f * in.nasal), 5, AN_CYAN);

    /* the tract's shift, drawn on the picture: the pharynx's roof is picked out */
    if (a->engine.set.formant != 0) {
        uint16_t col = a->engine.set.formant > 0 ? AN_AMB : AN_CYAN;
        cv_rect(150, 88, 88, 14, AN_DARK);
        cv_text(154, 90, &FONT_T, a->engine.set.formant > 0 ? "BRIGHTER" : "DARKER", col);
    }
    cv_blit(x, y + 28);
    if (ui_foot_is_dirty(a)) {
        ui_band_footer(a, K, x, y + 204, w);
        ui_foot_clear(a);
    }
}
