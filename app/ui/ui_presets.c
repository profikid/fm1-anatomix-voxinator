/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the PRESETS screen: the factory and the user presets.
 *
 * One column of names, as the product's sketch shows: the factory's names are built in, the user slots
 * carry their own. The loaded preset is marked with a star, and the right side shows what is in the
 * preset (the mode, the voice count and the formant shift).
 */

#include "plat.h"
#include "ui.h"
#include "app_state.h"
#include "anatomy.h"
/* the copy helper (ui_common.c) */
void ui_copy(char *out, const char *src, int max);
/* the numbers (ui_common.c) */
void ui_int_text(int32_t v, char *out);
/* the bands and the foot's redraw flag (ui_common.c) */
void ui_band_header(app_state_t *a, const char *title, const char *right, int32_t x, int32_t y, int32_t w);
void ui_band_footer(app_state_t *a, const char *const *knobs, int32_t x, int32_t y, int32_t w);
int ui_foot_is_dirty(const app_state_t *a);
void ui_foot_clear(app_state_t *a);
/* the preset's name (ui_common.c) */
void ui_preset_name(const app_state_t *a, int sel, char *out);

#define PRESET_ROW 11u             /* a row's height: 16 rows fit the body exactly */
#define PRESET_TOP 2u

void ui_draw_presets(app_state_t *a, int32_t x, int32_t y, int32_t w, int32_t h)
{
    static const char *const K[4] = {"BROWSE", "CATEGORY", "LOAD", "SAVE"};
    int list = a->ui.preset_list ? 1 : 0;
    int sel = a->ui.preset_sel;
    int i;
    ui_band_header(a, "PRESETS", list ? "USER" : "FACTORY", x, y, w);
    cv_begin(w, 176, AN_DARK);
    for (i = 0; i < 16; i++) {
        int32_t py = (int32_t)PRESET_TOP + (int32_t)i * (int32_t)PRESET_ROW;
        int cur = (int)a->current == (list ? (int)VOX_FACTORY_PRESETS + i : i);
        int focus = (i == sel);
        const vox_preset_t *p = list ? &a->user[i] : &a->factory[i];
        char name[16];
        if (focus)
            cv_rect(2, py - 1, w - 4, (int32_t)PRESET_ROW, an_fade(AN_CYAN, AN_DARK, 0.85f));
        ui_preset_name(a, i, name);
        cv_text(6, py, &FONT_T, name, focus ? AN_CYAN : AN_DIM);
        if (cur)
            cv_text(96, py, &FONT_T, "*", AN_AMB);
        /* the summary: the mode, the voice count and the formant shift */
        {
            char t[12];
            int n = 0;
            t[n++] = p->mode ? 'G' : 'M';
            t[n++] = ' ';
            t[n++] = (char)('0' + (p->voices > 9 ? 9 : p->voices));
            t[n++] = 'V';
            t[n++] = ' ';
            t[n++] = p->formant < 0 ? '-' : '+';
            t[n++] = (char)('0' + (p->formant < 0 ? -p->formant : p->formant) / 10);
            t[n++] = (char)('0' + (p->formant < 0 ? -p->formant : p->formant) % 10);
            t[n] = 0;
            cv_text(150, py, &FONT_T, t, focus ? AN_AMB : AN_DIM);
        }
    }
    cv_blit(x, y + 28);
    if (ui_foot_is_dirty(a)) {
        ui_band_footer(a, K, x, y + 204, w);
        ui_foot_clear(a);
    }
}
