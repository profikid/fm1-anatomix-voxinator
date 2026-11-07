/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the HOME screen: the anatomical overview.
 *
 * The whole picture: the head, the throat, the lungs, the airflow and the effects. The header
 * carries the title, the mode, the detected note and the input's meter; the foot the four knobs.
 */

#include "plat.h"
#include "ui.h"
#include "app_state.h"
#include "anatomy.h"
/* the anatomy's input and its step (ui_common.c) */
void ui_anatomy_in(app_state_t *a, anatomy_in_t *in);
void ui_anatomy_step(app_state_t *a, const anatomy_in_t *in);
/* the bands and the foot's redraw flag (ui_common.c) */
void ui_band_header(app_state_t *a, const char *title, const char *right, int32_t x, int32_t y, int32_t w);
void ui_band_footer(app_state_t *a, const char *const *knobs, int32_t x, int32_t y, int32_t w);
int ui_foot_is_dirty(const app_state_t *a);
void ui_foot_clear(app_state_t *a);

void ui_draw_home(app_state_t *a, int32_t x, int32_t y, int32_t w, int32_t h)
{
    static const char *const K[4] = {"VOICE COUNT", "SPREAD", "WET/DRY", "FX MACRO"};
    anatomy_in_t in;
    ui_anatomy_in(a, &in);
    ui_anatomy_step(a, &in);
    ui_band_header(a, "HOME", "ANATOMIX", x, y, w);
    cv_begin(w, 176, AN_DARK);
    anatomy_draw_head(&a->anatomy, &in, 0, 0, w, 176);
    anatomy_draw_formants(&a->anatomy, &in, 0, 0, w, 176);
    anatomy_draw_lungs(&a->anatomy, &in, 0, 0, w, 176);
    anatomy_draw_folds(&a->anatomy, &in, 0, 0, w, 176);
    anatomy_draw_airflow(&a->anatomy, &in, 0, 0, w, 176);
    anatomy_draw_effects(&a->anatomy, &in, 0, 0, w, 176);
    cv_blit(x, y + 28);
    if (ui_foot_is_dirty(a)) {
        ui_band_footer(a, K, x, y + 204, w);
        ui_foot_clear(a);
    }
}
