/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the MULTIVOICE screen: the harmony and the voices.
 *
 * The original stays in the middle of the picture; the other voices are drawn beside it as smaller,
 * ghosted overlays, each with its own colour, its own pitch, its own level and its own moving folds.
 */

#include "plat.h"
#include "ui.h"
#include "app_state.h"
#include "anatomy.h"
#include "vox_math.h"
/* the anatomy's input and its step (ui_common.c) */
void ui_anatomy_in(app_state_t *a, anatomy_in_t *in);
void ui_anatomy_step(app_state_t *a, const anatomy_in_t *in);
/* the landmarks the ghosts are drawn from (anatomy_formants.c) */
int an_silhouette(const anatomy_in_t *in, float *out, int max);
void an_map(int32_t x, int32_t y, int32_t w, int32_t h, int32_t sx, int32_t sy, int32_t sw, int32_t sh,
            float *p, int n);
/* the notes (ui_common.c) */
void ui_note_text(int note, char *out);
/* the bands and the foot's redraw flag (ui_common.c) */
void ui_band_header(app_state_t *a, const char *title, const char *right, int32_t x, int32_t y, int32_t w);
void ui_band_footer(app_state_t *a, const char *const *knobs, int32_t x, int32_t y, int32_t w);
int ui_foot_is_dirty(const app_state_t *a);
void ui_foot_clear(app_state_t *a);

void ui_draw_multivoice(app_state_t *a, int32_t x, int32_t y, int32_t w, int32_t h)
{
    static const char *const K[4] = {"VOICE COUNT", "HARMONY", "DETUNE", "MIX"};
    static const uint16_t VC[3] = {AN_CYAN, AN_AMB, AN_LAB};
    anatomy_in_t in;
    char note[8];
    int i;
    ui_anatomy_in(a, &in);
    ui_anatomy_step(a, &in);
    ui_band_header(a, "MULTIVOICE", "ANATOMIX", x, y, w);
    cv_begin(w, 176, AN_DARK);
    anatomy_draw_head(&a->anatomy, &in, 0, 0, 240, 176);
    anatomy_draw_formants(&a->anatomy, &in, 0, 0, 240, 176);
    anatomy_draw_lungs(&a->anatomy, &in, 0, 0, 240, 176);
    anatomy_draw_folds(&a->anatomy, &in, 0, 0, 240, 176);
    anatomy_draw_airflow(&a->anatomy, &in, 0, 0, 240, 176);
    anatomy_draw_effects(&a->anatomy, &in, 0, 0, 240, 176);

    /* the extra voices: a small ghost of the whole body, one under the other in the left column */
    for (i = 1; i < in.voices && i < 4; i++) {
        float sil[96];
        int n = an_silhouette(&in, sil, 48);
        uint16_t col = VC[i - 1];
        int32_t gx = 3, gy = 8 + (i - 1) * 60;
        float lev = in.voice_level[i];
        an_map(gx, gy, 50, 36, 0, 0, 240, 176, sil, n);
        an_poly(sil, n, an_fade(col, AN_DARK, 0.35f + 0.5f * lev));
        /* the voice's own folds, moving at its own pitch */
        {
            float ph = a->anatomy.fold_phase[i];
            int32_t fx = gx + 26, fy = gy + 12;
            float gap = 1.0f + 3.0f * lev + 1.0f * vox_sin_turns(ph);
            cv_line(fx, fy - (int32_t)gap, fx + 12, fy - (int32_t)gap + 1, col);
            cv_line(fx, fy + (int32_t)gap, fx + 12, fy + (int32_t)gap - 1, col);
        }
        /* the voice's pitch and its level */
        if (in.voice_f0[i] > 1.0f) {
            int32_t midi = (int32_t)(vox_midi_of(in.voice_f0[i]) + 0.5f);
            ui_note_text(midi, note);
            cv_text(gx, gy + 38, &FONT_T, note, col);
        }
        cv_text(gx + 34, gy + 38, &FONT_T, i == 1 ? "V2" : i == 2 ? "V3" : "V4", an_fade(col, AN_DARK, 0.4f));
        cv_rect(gx, gy + 52, 50, 3, an_fade(AN_DIM, AN_DARK, 0.5f));
        cv_rect(gx, gy + 52, (int32_t)(50.0f * lev), 3, col);
    }
    cv_blit(x, y + 28);
    if (ui_foot_is_dirty(a)) {
        ui_band_footer(a, K, x, y + 204, w);
        ui_foot_clear(a);
    }
}
