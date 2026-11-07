/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the FX screen: the four effect slots.
 *
 * The picture is shown small on the left (the effects are drawn on it: the delay's ghosts, the
 * reverb's rings, the distortion's edge), the four slots are listed on the right and the focused
 * slot's three parameters are along the bottom.
 */

#include "plat.h"
#include "ui.h"
#include "app_state.h"
#include "anatomy.h"
/* the anatomy's input and its step (ui_common.c) */
void ui_anatomy_in(app_state_t *a, anatomy_in_t *in);
void ui_anatomy_step(app_state_t *a, const anatomy_in_t *in);
/* the copy helper (ui_common.c) */
void ui_copy(char *out, const char *src, int max);
/* the numbers (ui_common.c) */
void ui_pct_text(float v, char *out);
/* the bands and the foot's redraw flag (ui_common.c) */
void ui_band_header(app_state_t *a, const char *title, const char *right, int32_t x, int32_t y, int32_t w);
void ui_band_footer(app_state_t *a, const char *const *knobs, int32_t x, int32_t y, int32_t w);
int ui_foot_is_dirty(const app_state_t *a);
void ui_foot_clear(app_state_t *a);

void ui_draw_fx(app_state_t *a, int32_t x, int32_t y, int32_t w, int32_t h)
{
    anatomy_in_t in;
    char name[16], pn[3][12], t[16];
    int32_t fx = a->ui.fx_focus & 3u;
    int i;
    ui_anatomy_in(a, &in);
    ui_anatomy_step(a, &in);

    ui_band_header(a, "FX", vox_fx_name(a->engine.set.fx[fx].type), x, y, w);
    cv_begin(w, 176, AN_DARK);
    /* the picture, small, on the left */
    anatomy_draw_head(&a->anatomy, &in, 0, 0, 140, 103);
    anatomy_draw_formants(&a->anatomy, &in, 0, 0, 140, 103);
    anatomy_draw_lungs(&a->anatomy, &in, 0, 0, 140, 103);
    anatomy_draw_folds(&a->anatomy, &in, 0, 0, 140, 103);
    anatomy_draw_airflow(&a->anatomy, &in, 0, 0, 140, 103);
    anatomy_draw_effects(&a->anatomy, &in, 0, 0, 140, 103);

    /* the four slots, on the right */
    for (i = 0; i < 4; i++) {
        int32_t sy = 3 + i * 25;
        int32_t focus = (i == (int)fx);
        ui_copy(name, vox_fx_name(a->engine.set.fx[i].type), 15);
        if (focus)
            cv_rect(142, sy - 2, 96, 23, an_fade(AN_AMB, AN_DARK, 0.82f));
        {
            char slot[2];
            slot[0] = (char)('1' + i);
            slot[1] = 0;
            cv_text(145, sy, &FONT_T, slot, focus ? AN_AMB : AN_DIM);
        }
        cv_text(158, sy, &FONT_T, name, focus ? AN_AMB : AN_DIM);
        if (a->engine.set.fx[i].on) {
            ui_pct_text((float)a->engine.set.fx[i].mix * 0.01f, t);
            cv_text(236 - text_w(&FONT_T, t), sy, &FONT_T, t, focus ? AN_AMB : AN_DIM);
        } else {
            cv_text(236 - text_w(&FONT_T, "OFF"), sy, &FONT_T, "OFF", AN_DIM);
        }
        cv_rect(158, sy + 14, 78, 3, an_fade(AN_DIM, AN_DARK, 0.5f));
        cv_rect(158, sy + 14, (int32_t)(78.0f * (float)a->engine.set.fx[i].mix * 0.01f), 3,
                focus ? AN_AMB : AN_DIM);
    }

    /* the focused slot's three parameters, along the bottom */
    cv_rect(2, 124, 236, 1, AN_DIM);
    vox_fx_param_name(a->engine.set.fx[fx].type, pn);
    for (i = 0; i < 3; i++) {
        int32_t px = 4 + i * 78;
        ui_copy(name, pn[i], 11);
        cv_text(px, 130, &FONT_T, name, AN_DIM);
        ui_pct_text((float)a->engine.set.fx[fx].p[i] * 0.01f, t);
        cv_text(px, 144, &FONT_XS, t, AN_AMB);
        cv_rect(px, 162, 70, 5, an_fade(AN_DIM, AN_DARK, 0.5f));
        cv_rect(px, 162, (int32_t)(70.0f * (float)a->engine.set.fx[fx].p[i] * 0.01f), 5, AN_AMB);
    }
    cv_blit(x, y + 28);
    if (ui_foot_is_dirty(a)) {
        const char *kn[4];
        char p0[12], p1[12], p2[12];
        vox_fx_param_name(a->engine.set.fx[fx].type, pn);
        ui_copy(p0, pn[0], 11);
        ui_copy(p1, pn[1], 11);
        ui_copy(p2, pn[2], 11);
        kn[0] = p0;
        kn[1] = p1;
        kn[2] = p2;
        kn[3] = "MOVE";
        ui_band_footer(a, kn, x, y + 204, w);
        ui_foot_clear(a);
    }
}
