/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the interface's shared helpers: the drawing primitives the anatomy needs, the
 * header and the foot, the page model and the input that changes the engine's parameters.
 *
 * The screens and the anatomy are compiled into the app's unity unit, so the platform's drawing
 * primitives (cv_rect, cv_line, cv_text, cv_pset, cv_blit) and its fonts are in scope here.
 *
 * What lives in this file:
 *   - the drawing primitives the anatomy shares (an_*): the polygons, the discs, the rings, the
 *     smooth curves, the colour mixer. The platform has no circle and no gradient, so the anatomy
 *     brings its own.
 *   - the interface's own helpers (ui_*): the header, the foot, the meters, the panels.
 *   - the page model: the pages' names, the knob names, the knob values and the input (the buttons
 *     and the encoders) that writes the engine's parameters.
 */

#include "plat.h"                /* the platform's controls and the clock */
#include "vox.h"                 /* the engine's parameters and its note names */
#include "vox_math.h"            /* the engine's maths (the platform has no libm) */
#include "ui.h"                  /* the interface's own API */
#include "app_state.h"           /* the state the screens read */
#include "anatomy.h"             /* the picture's API */

/* the screen's three bands: the header on top, the anatomy in the middle, the foot at the bottom.
 * The canvas holds one band at a time (the app sizes it for the anatomy band), so each band is
 * begun, drawn and blitted on its own. */
#define UI_HEAD_H 28u
#define UI_BODY_H 176u
#define UI_FOOT_H 36u
#define UI_SCREEN_H 240u

/* the app's screen reader (app.h declares it; the app's own file defines it) */
void app_say(const char *a, const char *b);

/* the helpers this file shares with the screens (the prototypes live here: ui.h is the contract's) */
void ui_int_text(int32_t v, char *out);
void ui_pct_text(float v, char *out);
void ui_note_text(int note, char *out);
void ui_signed_text(int32_t v, const char *unit, char *out);
void ui_copy(char *out, const char *src, int max);
void ui_knob_value(app_state_t *a, int knob, char *out);
void ui_anatomy_in(app_state_t *a, anatomy_in_t *in);
void ui_anatomy_step(app_state_t *a, const anatomy_in_t *in);
const char *ui_midi_name(int d);
void ui_preset_name(const app_state_t *a, int sel, char *out);
int ui_midi_dest(const app_state_t *a);
void ui_midi_dest_set(app_state_t *a, int d);
int ui_monitor(const app_state_t *a);
void ui_monitor_set(app_state_t *a, int on);
int ui_theme(void);
void ui_theme_set(int i);
uint16_t an_fade(uint16_t c, uint16_t bg, float t);
void an_ellf(float cx, float cy, float rx, float ry, uint16_t c);

/* ------------------------------------------------------------------ the primitives --- */

/* the square root, without libm (the device has none): Newton's method, five steps is plenty for
 * the radii we draw */
float an_sqrt(float a)
{
    float x = a > 1.0f ? a * 0.5f : 1.0f;
    int i;
    if (a <= 0.0f)
        return 0.0f;
    for (i = 0; i < 6; i++)
        x = 0.5f * (x + a / x);
    return x;
}

/* a closed polygon's outline */
void an_poly(const float *p, int n, uint16_t c)
{
    int i;
    for (i = 0; i < n; i++) {
        int j = (i + 1) % n;
        cv_line((int32_t)p[i * 2], (int32_t)p[i * 2 + 1], (int32_t)p[j * 2], (int32_t)p[j * 2 + 1], c);
    }
}

/* a closed polygon's fill: the scanlines between the edges, in pairs */
void an_polyf(const float *p, int n, uint16_t c)
{
    float lo = 1.9e9f, hi = -1.9e9f;
    float xs[64];
    int i, j, k, m;
    if (n < 3)
        return;
    for (i = 0; i < n; i++) {
        if (p[i * 2 + 1] < lo)
            lo = p[i * 2 + 1];
        if (p[i * 2 + 1] > hi)
            hi = p[i * 2 + 1];
    }
    for (j = (int32_t)lo; j <= (int32_t)hi; j++) {
        float y = (float)j + 0.5f;
        m = 0;
        for (i = 0; i < n; i++) {
            int b = (i + 1) % n;
            float y0 = p[i * 2 + 1], y1 = p[b * 2 + 1];
            if ((y0 <= y && y1 > y) || (y1 <= y && y0 > y)) {
                float x0 = p[i * 2], x1 = p[b * 2];
                if (m < 64)
                    xs[m++] = x0 + (y - y0) * (x1 - x0) / (y1 - y0);
            }
        }
        for (i = 1; i < m; i++) {                 /* the crossings, in order */
            float v = xs[i];
            for (k = i; k > 0 && xs[k - 1] > v; k--)
                xs[k] = xs[k - 1];
            xs[k] = v;
        }
        for (i = 0; i + 1 < m; i += 2) {
            int32_t x0 = (int32_t)xs[i], x1 = (int32_t)xs[i + 1];
            if (x1 > x0)
                cv_rect(x0, j, x1 - x0, 1, c);
        }
    }
}

/* a filled disc */
void an_disc(float cx, float cy, float r, uint16_t c)
{
    int32_t dy, r2 = (int32_t)r;
    for (dy = -r2; dy <= r2; dy++) {
        float t = r * r - (float)dy * dy;
        float hw;
        int32_t x0, x1;
        if (t < 0.0f)
            continue;
        hw = an_sqrt(t);
        x0 = (int32_t)(cx - hw);
        x1 = (int32_t)(cx + hw) + 1;
        if (x1 > x0)
            cv_rect(x0, (int32_t)cy + dy, x1 - x0, 1, c);
    }
}

/* a circle's outline (the midpoint circle: no trigonometry, no gaps) */
void an_ring(float cx, float cy, float r, uint16_t c)
{
    int32_t x = (int32_t)r, y = 0, e = 1 - (int32_t)r;
    int32_t px = (int32_t)cx, py = (int32_t)cy;
    while (x >= y) {
        cv_pset(px + x, py + y, c);
        cv_pset(px + y, py + x, c);
        cv_pset(px - y, py + x, c);
        cv_pset(px - x, py + y, c);
        cv_pset(px - x, py - y, c);
        cv_pset(px - y, py - x, c);
        cv_pset(px + y, py - x, c);
        cv_pset(px + x, py - y, c);
        y++;
        if (e < 0)
            e += 2 * y + 1;
        else {
            x--;
            e += 2 * (y - x) + 1;
        }
    }
}

/* an ellipse's outline: the parametric form, in 48 steps */
void an_ell(float cx, float cy, float rx, float ry, uint16_t c)
{
    int i;
    for (i = 0; i < 48; i++) {
        float t = (float)i * (1.0f / 48.0f);
        float u = (float)((i + 1) % 48) * (1.0f / 48.0f);
        int32_t x0 = (int32_t)(cx + rx * vox_sin_turns(t + 0.25f));
        int32_t y0 = (int32_t)(cy - ry * vox_sin_turns(t));
        int32_t x1 = (int32_t)(cx + rx * vox_sin_turns(u + 0.25f));
        int32_t y1 = (int32_t)(cy - ry * vox_sin_turns(u));
        cv_line(x0, y0, x1, y1, c);
    }
}

/* a filled ellipse: the skull's cavity and the brain's mass */
void an_ellf(float cx, float cy, float rx, float ry, uint16_t c)
{
    int32_t dy, r2 = (int32_t)ry;
    if (rx < 0.5f || ry < 0.5f)
        return;
    for (dy = -r2; dy <= r2; dy++) {
        float t = 1.0f - ((float)dy * (float)dy) / (ry * ry);
        float hw;
        int32_t x0, x1;
        if (t < 0.0f)
            continue;
        hw = rx * an_sqrt(t);
        x0 = (int32_t)(cx - hw);
        x1 = (int32_t)(cx + hw) + 1;
        if (x1 > x0)
            cv_rect(x0, (int32_t)cy + dy, x1 - x0, 1, c);
    }
}

/* a smooth curve through the points: a quadratic per segment, so the line bends at every point.
 * The curve runs from the midpoints to the midpoints, with the point itself as the control. */
void an_curve(const float *p, int n, uint16_t c)
{
    int i, k;
    for (i = 0; i + 1 < n; i++) {
        float x0, y0, x1, y1, cx = p[(i + 1) * 2], cy = p[(i + 1) * 2 + 1];
        if (i == 0) {
            x0 = p[0];
            y0 = p[1];
        } else {
            x0 = 0.5f * (p[(i - 1) * 2] + p[i * 2]);
            y0 = 0.5f * (p[(i - 1) * 2 + 1] + p[i * 2 + 1]);
        }
        if (i + 2 >= n) {
            x1 = p[i * 2 + 2];
            y1 = p[i * 2 + 3];
        } else {
            x1 = 0.5f * (p[i * 2 + 2] + p[(i + 2) * 2]);
            y1 = 0.5f * (p[i * 2 + 3] + p[(i + 2) * 2 + 1]);
        }
        for (k = 0; k < 10; k++) {
            float t = (float)k * 0.1f, u = 1.0f - t;
            cv_pset((int32_t)(u * u * x0 + 2.0f * u * t * cx + t * t * x1),
                    (int32_t)(u * u * y0 + 2.0f * u * t * cy + t * t * y1), c);
        }
    }
}

/* a dashed line: the annotation's leader */
void an_dash(float x0, float y0, float x1, float y1, uint16_t c)
{
    float dx = x1 - x0, dy = y1 - y0;
    float len = an_sqrt(dx * dx + dy * dy);
    int32_t i, n;
    if (len < 1.0f)
        return;
    n = (int32_t)len;
    for (i = 0; i < n; i++) {
        if ((i & 3) < 2)
            cv_pset((int32_t)(x0 + dx * (float)i / len), (int32_t)(y0 + dy * (float)i / len), c);
    }
}

/* a colour mixed toward another one: the ghosts and the fades.
 * The arithmetic is signed: the two channels' difference can be negative. */
uint16_t an_fade(uint16_t c, uint16_t bg, float t)
{
    int32_t r = (int32_t)((c >> 11) & 31u), g = (int32_t)((c >> 5) & 63u), b = (int32_t)(c & 31u);
    int32_t br = (int32_t)((bg >> 11) & 31u), bg2 = (int32_t)((bg >> 5) & 63u), bb = (int32_t)(bg & 31u);
    int32_t nr, ng, nb;
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;
    nr = r + (int32_t)((float)(br - r) * t);
    ng = g + (int32_t)((float)(bg2 - g) * t);
    nb = b + (int32_t)((float)(bb - b) * t);
    if (nr < 0)
        nr = 0;
    if (ng < 0)
        ng = 0;
    if (nb < 0)
        nb = 0;
    if (nr > 31)
        nr = 31;
    if (ng > 63)
        ng = 63;
    if (nb > 31)
        nb = 31;
    return (uint16_t)((uint32_t)nr << 11 | (uint32_t)ng << 5 | (uint32_t)nb);
}

/* the annotation's leader with its label: a thin dashed line and a small name */
void an_leader(float x0, float y0, float x1, float y1, const char *label, uint16_t lc, uint16_t tc)
{
    an_dash(x0, y0, x1, y1, lc);
    cv_pset((int32_t)x1, (int32_t)y1, lc);
    if (label)
        cv_text((int32_t)x0 + 2, (int32_t)y0 - 5, &FONT_XS, label, tc);
}

/* ------------------------------------------------------------------ the text --- */

/* the build's version (the app's header gives it to the platform; the app's own name is the title) */
#ifdef APP_VERSION
#define APP_VERSION_TEXT APP_VERSION
#else
#define APP_VERSION_TEXT "DEV"
#endif

/* an integer, as text */
void ui_int_text(int32_t v, char *out)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *out++ = '-';
    while (n)
        *out++ = t[--n];
    *out = 0;
}

/* a percentage, as text */
void ui_pct_text(float v, char *out)
{
    int32_t p = (int32_t)(v * 100.0f + (v < 0.0f ? -0.5f : 0.5f));
    ui_int_text(p, out);
    {
        int n = 0;
        while (out[n])
            n++;
        out[n++] = '%';
        out[n] = 0;
    }
}

/* a note with its octave, as text ("C#4"); the engine's own names */
void ui_note_text(int note, char *out)
{
    int i = 0, oct;
    if (note < 0) {
        out[0] = 0;
        return;
    }
    vox_note_name(note, out);
    while (out[i])
        i++;
    oct = note / 12 - 1;
    if (oct < 0) {
        out[i++] = '-';
        oct = -oct;
    }
    out[i++] = (char)('0' + (oct / 10) % 10);
    out[i++] = (char)('0' + oct % 10);
    out[i] = 0;
}

/* a signed number with a unit, as text ("+3 ST") */
void ui_signed_text(int32_t v, const char *unit, char *out)
{
    int i = 0;
    if (v >= 0)
        out[i++] = '+';
    ui_int_text(v, out + i);
    i = 0;
    while (out[i])
        i++;
    while (*unit)
        out[i++] = *unit++;
    out[i] = 0;
}

/* a copy, cut to fit */
void ui_copy(char *out, const char *src, int max)
{
    int i = 0;
    if (max < 1) {
        out[0] = 0;
        return;
    }
    while (src[i] && i < max - 1) {
        out[i] = src[i];
        i++;
    }
    out[i] = 0;
}

/* ------------------------------------------------------------------ the anatomy --- */

/* the effect slot's parameter with a name we look for (the feedback, the decay): the engine names
 * its own parameters, so the picture follows them */
static int ui_fx_param(const char pn[3][12], const char *key)
{
    int i;
    for (i = 0; i < 3; i++)
        if (pn[i][0] == key[0] && pn[i][1] == key[1])
            return i;
    return 0;
}

/* the anatomy's input: what the picture reacts to. The engine's own state and the published pitch. */
void ui_anatomy_in(app_state_t *a, anatomy_in_t *in)
{
    const vox_preset_t *s = &a->engine.set;
    int i;
    in->level = a->pub.level;
    in->peak = a->pub.peak;
    in->f0 = a->pub.f0;
    in->target_f0 = a->pub.target_f0;
    in->clarity = a->pub.clarity;
    in->voiced = a->pub.voiced;
    in->formant = (float)s->formant;
    /* the nasal resonance: the lead voice's formant offset carries it (the engine has no field of
     * its own for it, so the FORMANT page's NASAL knob writes that offset) */
    in->nasal = (float)(s->voice[0].formant + 12) * (1.0f / 24.0f);
    in->distort = 0.0f;
    in->delay_amt = 0.0f;
    in->reverb_amt = 0.0f;
    in->compress = 0.0f;
    in->overload = (a->pub.peak > 0.985f || a->overload) ? 1 : 0;
    in->voices = s->voices ? s->voices : 1;
    for (i = 0; i < 4; i++) {
        in->voice_f0[i] = 0.0f;
        in->voice_level[i] = (float)s->voice[i].level * 0.01f;
    }
    in->voice_f0[0] = a->pub.f0;
    for (i = 1; i < 4; i++) {
        int iv = (int)a->engine.harm.interval[i];
        if (a->pub.f0 > 1.0f && iv != 0)
            in->voice_f0[i] = a->pub.f0 * vox_exp2f((float)iv * (1.0f / 12.0f));
    }
    /* the effect chain: the character effects the picture shows */
    for (i = 0; i < 4; i++) {
        char pn[3][12];
        int t = s->fx[i].type;
        if (!s->fx[i].on)
            continue;
        vox_fx_param_name(t, pn);
        switch (t) {
        case VOX_FX_SATURATE:
        case VOX_FX_OVERDRIVE:
        case VOX_FX_FUZZ:
        case VOX_FX_VOCAL_DIST:
        case VOX_FX_BITCRUSH:
            in->distort = (float)s->fx[i].p[0] * 0.01f;
            break;
        case VOX_FX_DELAY:
        case VOX_FX_TAPE_DELAY:
        case VOX_FX_PINGPONG:
        case VOX_FX_STEREO_DELAY:
            in->delay_amt = (float)s->fx[i].p[ui_fx_param(pn, "FB")] * 0.01f;
            break;
        case VOX_FX_REVERB:
        case VOX_FX_SHIMMER:
            in->reverb_amt = (float)s->fx[i].p[ui_fx_param(pn, "DE")] * 0.01f;
            break;
        case VOX_FX_COMP:
        case VOX_FX_LIMITER:
            in->compress = (float)s->fx[i].mix * 0.01f;
            break;
        default:
            break;
        }
    }
    in->now_ms = plat_ms();
}

/* the anatomy's step: the phases, the particles and the rings move. The app may also drive the
 * update itself (it does, in its frame); then this call is in the same millisecond and only the
 * parameters the interface knows are read again, so the effects the picture shows (the delay's
 * ghosts, the reverb's rings) follow the engine's own chain. */
void ui_anatomy_step(app_state_t *a, const anatomy_in_t *in)
{
    anatomy_update(&a->anatomy, in, in->now_ms);
}

/* ------------------------------------------------------------------ the panels --- */

/* a panel: a flat ground with a thin edge */
void ui_panel(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t fill)
{
    cv_rect(x, y, w, h, fill);
    cv_rect(x, y, w, 1, an_fade(fill, AN_BONE, 0.25f));
}

/* a level meter: the ground, the level, the frame */
void ui_meter(app_state_t *a, int32_t x, int32_t y, int32_t w, int32_t h, float level, int red)
{
    float v = level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level);
    int32_t n = (int32_t)((float)w * v);
    (void)a;
    cv_rect(x, y, w, h, an_fade(AN_DIM, AN_DARK, 0.55f));
    cv_rect(x, y, n, h, red ? AN_RED : AN_CYAN);
    cv_rect(x, y, w, 1, an_fade(AN_BONE, AN_DARK, 0.45f));
}

/* a value's bar: 0..1 across the width */
void ui_value_bar(app_state_t *a, int32_t x, int32_t y, int32_t w, float value, uint16_t colour)
{
    float v = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    int32_t n = (int32_t)((float)w * v);
    (void)a;
    cv_rect(x, y, w, 4, an_fade(AN_DIM, AN_DARK, 0.6f));
    if (n > 0)
        cv_rect(x, y, n, 4, colour);
    cv_rect(x, y, n > 0 ? n : 1, 1, an_fade(colour, AN_BONE, 0.4f));
}

/* ------------------------------------------------------------------ the header --- */

/* the header's band: the title, the mode, the detected note and the input's meter. The band is 28
 * rows: the names on the top, the meter and the note under them. It is blitted where the caller says. */
void ui_band_header(app_state_t *a, const char *title, const char *right, int32_t x, int32_t y, int32_t w)
{
    char note[8];
    int32_t nw;
    cv_begin(w, UI_HEAD_H, AN_DARK);
    cv_text(2, 1, &FONT_T, title, AN_BONE);
    cv_text(88, 1, &FONT_T, a->engine.set.mode ? "GLIDE" : "MULTIVOICE", AN_CYAN);
    if (right && right[0])
        cv_text(238 - text_w(&FONT_T, right), 1, &FONT_T, right, an_fade(AN_AMB, AN_DARK, 0.25f));
    if (a->pub.note >= 0) {
        ui_note_text(a->pub.note, note);
        nw = text_w(&FONT_B, note);
        cv_text(238 - nw, 12, &FONT_B, note, a->pub.voiced ? AN_CYAN : AN_DIM);
    }
    ui_meter(a, 2, 17, 138, 7, a->pub.level, a->pub.peak > 0.985f);
    cv_text(144, 16, &FONT_T, "IN", AN_DIM);
    if (a->pub.peak > 0.985f)
        cv_text(166, 16, &FONT_T, "OVERLOAD", AN_RED);
    cv_blit((uint32_t)x, (uint32_t)y);
}

/* the header, at the screen's top (the contract's entry point) */
void ui_header(app_state_t *a, const char *title, const char *right)
{
    ui_band_header(a, title, right, 0, 0, 240);
}

/* the foot's band: the four knobs' names and their values. The band is 36 rows. */
void ui_band_footer(app_state_t *a, const char *const *knobs, int32_t x, int32_t y, int32_t w)
{
    char v[16];
    int i;
    cv_begin(w, UI_FOOT_H, AN_DARK);
    cv_rect(0, 0, 240, 1, an_fade(AN_DIM, AN_DARK, 0.4f));
    for (i = 0; i < 4; i++) {
        cv_text(3 + i * 60, 3, &FONT_T, knobs[i], AN_DIM);
        ui_knob_value(a, i, v);
        cv_text(3 + i * 60, 16, &FONT_XS, v, AN_AMB);
    }
    cv_blit((uint32_t)x, (uint32_t)y);
}

/* the foot, at the screen's bottom (the contract's entry point) */
void ui_footer(app_state_t *a, const char *const *knobs)
{
    ui_band_footer(a, knobs, 0, 204, 240);
}

/* ------------------------------------------------------------------ the pages --- */

/* the page's name */
const char *ui_page_name(int page)
{
    switch (page) {
    case PG_MULTIVOICE: return "MULTIVOICE";
    case PG_GLIDE:      return "GLIDE";
    case PG_FORMANT:    return "FORMANT";
    case PG_FX:         return "EFFECTS";
    case PG_PRESETS:    return "PRESETS";
    case PG_INPUT:      return "INPUT";
    case PG_GLOBAL:     return "GLOBAL";
    default:            return "HOME";
    }
}

/* the page's four knob names */
const char *ui_knob_name(int page, int knob)
{
    static const char *const HOME[4]    = {"VOICE COUNT", "SPREAD", "WET/DRY", "FX MACRO"};
    static const char *const MULTI[4]   = {"VOICE COUNT", "HARMONY", "DETUNE", "MIX"};
    static const char *const GLIDE[4]   = {"RETUNE", "GLIDE", "HUMANIZE", "MIX"};
    static const char *const FORM[4]    = {"FORMANT", "THROAT", "NASAL", "PRESERVE"};
    static const char *const FXK[4]     = {"FX P1", "FX P2", "FX P3", "MOVE"};
    static const char *const PRESET[4]  = {"BROWSE", "CATEGORY", "LOAD", "SAVE"};
    static const char *const INPUT[4]   = {"GAIN", "MONITOR", "FORMAT", "HOST"};
    static const char *const GLOBAL[4]  = {"MIDI", "OUT", "THEME", "VERSION"};
    const char *const *k;
    switch (page) {
    case PG_MULTIVOICE: k = MULTI; break;
    case PG_GLIDE:      k = GLIDE; break;
    case PG_FORMANT:    k = FORM; break;
    case PG_FX:         k = FXK; break;
    case PG_PRESETS:    k = PRESET; break;
    case PG_INPUT:      k = INPUT; break;
    case PG_GLOBAL:     k = GLOBAL; break;
    default:            k = HOME; break;
    }
    return k[knob & 3];
}

/* the page's knob value, as the foot shows it */
void ui_knob_value(app_state_t *a, int knob, char *out)
{
    const vox_preset_t *s = &a->engine.set;
    int32_t fx = a->ui.fx_focus & 3u;
    out[0] = 0;
    switch (a->ui.page) {
    case PG_HOME:
        if (knob == 0)
            ui_int_text(s->voices, out);
        else if (knob == 1)
            ui_pct_text((float)s->spread * 0.01f, out);
        else if (knob == 2)
            ui_pct_text((float)s->mix * 0.01f, out);
        else
            ui_pct_text((float)s->fx[fx].mix * 0.01f, out);
        break;
    case PG_MULTIVOICE:
        if (knob == 0)
            ui_int_text(s->voices, out);
        else if (knob == 1)
            ui_int_text(s->harmony, out);
        else if (knob == 2)
            ui_pct_text((float)s->detune * 0.01f, out);
        else {
            int32_t sum = 0;
            int i;
            for (i = 1; i < 4; i++)
                sum += s->voice[i].level;
            ui_pct_text((float)sum * 0.01f / 3.0f, out);
        }
        break;
    case PG_GLIDE:
        if (knob == 3)
            ui_pct_text((float)s->mix * 0.01f, out);
        else
            ui_pct_text((float)(knob == 0 ? s->retune : knob == 1 ? s->glide : s->humanize) * 0.01f, out);
        break;
    case PG_FORMANT:
        if (knob == 0)
            ui_signed_text(s->formant, "ST", out);
        else if (knob == 1)
            ui_pct_text((float)s->formant_amt * 0.01f, out);
        else if (knob == 2)
            ui_pct_text((float)(s->voice[0].formant + 12) * (1.0f / 24.0f), out);
        else
            ui_pct_text((float)s->flex * 0.01f, out);
        break;
    case PG_FX:
        if (knob == 3) {
            ui_int_text((int32_t)fx + 1, out);
            out[1] = '/';
            out[2] = '4';
            out[3] = 0;
        } else
            ui_pct_text((float)s->fx[fx].p[knob] * 0.01f, out);
        break;
    case PG_PRESETS:
        if (knob == 0)
            ui_int_text(a->ui.preset_sel + 1, out);
        else if (knob == 1)
            ui_int_text(a->ui.preset_list, out);      /* 0 factory, 1 user */
        else if (knob == 2)
            ui_int_text(a->current, out);
        else
            out[0] = 0;
        break;
    case PG_INPUT:
        if (knob == 0)
            ui_pct_text((float)a->globals.mic_gain * 0.01f, out);
        else if (knob == 1) {
            out[0] = ui_monitor(a) ? '1' : '0';
            out[1] = 0;
        }
        else if (knob == 2)
            ui_int_text((int32_t)a->mic.rate / 1000u, out);
        else
            out[0] = a->globals.mic_on ? '1' : '0', out[1] = 0;
        break;
    default:
        if (knob == 0) {
            const char *n = ui_midi_name(ui_midi_dest(a));
            int i = 0;
            while (n[i] && i < 15) {
                out[i] = n[i];
                i++;
            }
            out[i] = 0;
        } else if (knob == 1)
            ui_pct_text((float)a->globals.out_level * 0.01f, out);
        else if (knob == 2)
            ui_int_text(ui_theme(), out);
        else
            out[0] = 0;
        break;
    }
}

/* ------------------------------------------------------------------ the input --- */

/* the interface's own scratch lives in the app's globals' two reserved bytes (the contract's
 * vox_globals_t has rsv[2], and the interface needs a place of its own: the screens are one
 * translation unit, so a file's static would be every screen's).
 *
 *   rsv[0]  bit 0..7   the first eight buttons the interface watches: was it held last time
 *   rsv[1]  bit 0..3   the other four buttons
 *           bit 4      the foot must be drawn again
 *           bit 5..6   where the MIDI goes
 *           bit 7      the input's monitor
 */
#define UI_RSV_BTN0 0u          /* rsv[0]: the buttons' history, the first eight */
#define UI_RSV_BTN1 0u          /* rsv[1]'s low four bits: the other buttons */
#define UI_RSV_FOOT 4u          /* rsv[1]'s bit 4: the foot must be drawn again */
#define UI_RSV_MIDI 5u          /* rsv[1]'s bits 5..6: where the MIDI goes */
#define UI_RSV_MON 7u           /* rsv[1]'s bit 7: the input's monitor */

/* the buttons the interface watches, and the bit each one takes in the history */
static const uint8_t UI_BTN[12] = {B_FX,   B_SEL,  B_ENV,  B_LFO,  B_EDIT, B_GLO,
                                    B_HOME, B_SAVE, B_ARP,  B_SEQ,  B_OCTDN, B_OCTUP};

/* the buttons held last time, as the interface's own bits */
static uint16_t ui_btn_hist(const app_state_t *a)
{
    return (uint16_t)(a->globals.rsv[0] | ((uint16_t)(a->globals.rsv[1] & 0x0Fu) << 8));
}

/* the buttons held now, as the interface's own bits */
static uint16_t ui_btn_now(uint32_t buttons)
{
    uint16_t h = 0;
    int i;
    for (i = 0; i < 12; i++)
        if (buttons >> UI_BTN[i] & 1u)
            h |= (uint16_t)(1u << i);
    return h;
}

static void ui_btn_hist_set(app_state_t *a, uint16_t h)
{
    a->globals.rsv[0] = (uint8_t)(h & 0xFFu);
    a->globals.rsv[1] = (uint8_t)((a->globals.rsv[1] & 0xF0u) | ((h >> 8) & 0x0Fu));
}

/* the interface's own bit for a platform button */
#define UI_BIT(b) (1u << (uint8_t)((b) == B_FX ? 0 : (b) == B_SEL ? 1 : (b) == B_ENV ? 2 : (b) == B_LFO ? 3 \
                            : (b) == B_EDIT ? 4 : (b) == B_GLO ? 5 : (b) == B_HOME ? 6 : (b) == B_SAVE ? 7 \
                            : (b) == B_ARP ? 8 : (b) == B_SEQ ? 9 : (b) == B_OCTDN ? 10 : 11))

/* the buttons that went down since the last call */
static uint16_t ui_btn_down(app_state_t *a, uint32_t buttons)
{
    uint16_t now = ui_btn_now(buttons);
    uint16_t was = ui_btn_hist(a);
    ui_btn_hist_set(a, now);
    return (uint16_t)(now & ~was);
}

/* the foot must be drawn again: it holds the knob names and their values */
void ui_foot_dirty(app_state_t *a)
{
    a->globals.rsv[1] |= (uint8_t)(1u << UI_RSV_FOOT);
}

int ui_foot_is_dirty(const app_state_t *a)
{
    return (a->globals.rsv[1] >> UI_RSV_FOOT) & 1u;
}

void ui_foot_clear(app_state_t *a)
{
    a->globals.rsv[1] &= (uint8_t)~(1u << UI_RSV_FOOT);
}

/* the MIDI's destination, in the globals' reserved bytes */
int ui_midi_dest(const app_state_t *a)
{
    return (int)((a->globals.rsv[1] >> UI_RSV_MIDI) & 3u);
}

void ui_midi_dest_set(app_state_t *a, int d)
{
    a->globals.rsv[1] = (uint8_t)((a->globals.rsv[1] & 0x9Fu) | ((uint32_t)d & 3u) << UI_RSV_MIDI);
}

/* a preset's name: the factory's names are built in, the user slots carry their own */
void ui_preset_name(const app_state_t *a, int sel, char *out)
{
    sel &= 15;
    if (a->ui.preset_list) {
        ui_copy(out, a->user_name[sel], 16);
        if (!out[0]) {                       /* a slot that was never named */
            out[0] = 'U';
            out[1] = (char)('0' + (sel / 10));
            out[2] = (char)('0' + (sel % 10));
            out[3] = 0;
        }
    } else {
        ui_copy(out, vox_factory_name((uint32_t)sel), 16);
    }
}

/* the MIDI's destination, as a name */
const char *ui_midi_name(int d)
{
    switch (d & 3) {
    case 1:  return "USB";
    case 2:  return "BT";
    case 3:  return "ALL";
    default: return "OFF";
    }
}

/* the input's monitor */
int ui_monitor(const app_state_t *a)
{
    return (a->globals.rsv[1] >> UI_RSV_MON) & 1u;
}

void ui_monitor_set(app_state_t *a, int on)
{
    if (on)
        a->globals.rsv[1] |= (uint8_t)(1u << UI_RSV_MON);
    else
        a->globals.rsv[1] &= (uint8_t)~(1u << UI_RSV_MON);
}

/* the theme: the platform's own palette (gfx.c's PALETTES, in scope here) */
int ui_theme(void)
{
    uint32_t i;
    for (i = 0; i < NPALETTES; i++)
        if (pal[3] == PALETTES[i].c[3] && pal[4] == PALETTES[i].c[4])
            return (int)i;
    return 0;
}
void ui_theme_set(int i)
{
    palette_set((uint32_t)(i < 0 ? 0 : (int)NPALETTES <= i ? (int)NPALETTES - 1 : i));
}

/* a knob's step on the engine's parameters: the page decides */
static void ui_knob(app_state_t *a, int knob, int32_t d)
{
    vox_preset_t *s = &a->engine.set;
    int32_t fx = a->ui.fx_focus & 3u;
    int32_t v;
    char t[16];
    switch (a->ui.page) {
    case PG_HOME:
        if (knob == 0) {
            v = (int32_t)s->voices + d;
            s->voices = (uint8_t)(v < 1 ? 1 : v > 4 ? 4 : v);
            ui_int_text(s->voices, t);
            app_say("VOICES", t);
        } else if (knob == 1) {
            s->spread = (uint8_t)vox_clampi((int32_t)s->spread + d * 2, 0, 100);
            ui_pct_text((float)s->spread * 0.01f, t);
            app_say("SPREAD", t);
        } else if (knob == 2) {
            s->mix = (uint8_t)vox_clampi((int32_t)s->mix + d * 2, 0, 100);
            ui_pct_text((float)s->mix * 0.01f, t);
            app_say("WET/DRY", t);
        } else {
            int32_t i;
            for (i = 0; i < 4; i++)
                s->fx[i].mix = (uint8_t)vox_clampi((int32_t)s->fx[i].mix + d * 2, 0, 100);
            ui_pct_text((float)s->fx[fx].mix * 0.01f, t);
            app_say("FX MACRO", t);
        }
        break;
    case PG_MULTIVOICE:
        if (knob == 0) {
            v = (int32_t)s->voices + d;
            s->voices = (uint8_t)(v < 1 ? 1 : v > 4 ? 4 : v);
            ui_int_text(s->voices, t);
            app_say("VOICES", t);
        } else if (knob == 1) {
            s->harmony = (uint8_t)vox_clampi((int32_t)s->harmony + d, 0, 11);
            ui_int_text(s->harmony, t);
            app_say("HARMONY", t);
        } else if (knob == 2) {
            s->detune = (uint8_t)vox_clampi((int32_t)s->detune + d * 2, 0, 100);
            ui_pct_text((float)s->detune * 0.01f, t);
            app_say("DETUNE", t);
        } else {
            int32_t i;
            for (i = 1; i < 4; i++)
                s->voice[i].level = (uint8_t)vox_clampi((int32_t)s->voice[i].level + d * 2, 0, 100);
            ui_pct_text((float)s->voice[1].level * 0.01f, t);
            app_say("MIX", t);
        }
        break;
    case PG_GLIDE:
        if (knob == 0)
            s->retune = (uint8_t)vox_clampi((int32_t)s->retune + d * 2, 0, 100);
        else if (knob == 1)
            s->glide = (uint8_t)vox_clampi((int32_t)s->glide + d * 2, 0, 100);
        else if (knob == 2)
            s->humanize = (uint8_t)vox_clampi((int32_t)s->humanize + d * 2, 0, 100);
        else
            s->mix = (uint8_t)vox_clampi((int32_t)s->mix + d * 2, 0, 100);
        ui_pct_text((float)(knob == 0 ? s->retune : knob == 1 ? s->glide : knob == 2 ? s->humanize
                                                                                : s->mix) * 0.01f, t);
        app_say(knob == 0 ? "RETUNE" : knob == 1 ? "GLIDE" : knob == 2 ? "HUMANIZE" : "MIX", t);
        break;
    case PG_FORMANT:
        if (knob == 0) {
            s->formant = (int8_t)vox_clampi((int32_t)s->formant + d, -12, 12);
            ui_signed_text(s->formant, "ST", t);
            app_say("FORMANT", t);
        } else if (knob == 1) {
            s->formant_amt = (uint8_t)vox_clampi((int32_t)s->formant_amt + d * 2, 0, 100);
            ui_pct_text((float)s->formant_amt * 0.01f, t);
            app_say("THROAT", t);
        } else if (knob == 2) {
            s->voice[0].formant = (int8_t)vox_clampi((int32_t)s->voice[0].formant + d, -12, 12);
            ui_pct_text((float)(s->voice[0].formant + 12) * (1.0f / 24.0f), t);
            app_say("NASAL", t);
        } else {
            s->flex = (uint8_t)vox_clampi((int32_t)s->flex + d * 2, 0, 100);
            ui_pct_text((float)s->flex * 0.01f, t);
            app_say("PRESERVE", t);
        }
        break;
    case PG_FX:
        if (knob == 3) {
            int32_t to = (fx + (d > 0 ? 1 : 3)) & 3;
            vox_fxslot_t tmp = s->fx[fx];
            s->fx[fx] = s->fx[to];
            s->fx[to] = tmp;
            a->ui.fx_focus = (uint8_t)to;
            app_say("MOVE", ui_knob_name(PG_FX, 0));
        } else {
            s->fx[fx].p[knob] = (uint8_t)vox_clampi((int32_t)s->fx[fx].p[knob] + d * 2, 0, 100);
            ui_pct_text((float)s->fx[fx].p[knob] * 0.01f, t);
            app_say("FX", t);
        }
        break;
    case PG_PRESETS:
        if (knob == 0) {
            a->ui.preset_sel = (uint8_t)vox_clampi((int32_t)a->ui.preset_sel + d, 0, 15);
            ui_int_text(a->ui.preset_sel + 1, t);
            app_say("PRESET", t);
        } else if (knob == 1) {
            a->ui.preset_list ^= 1u;
            app_say("LIST", a->ui.preset_list ? "USER" : "FACTORY");
        } else if (knob == 2) {
            const vox_preset_t *src = a->ui.preset_list ? &a->user[a->ui.preset_sel]
                                                         : &a->factory[a->ui.preset_sel];
            s->magic = src->magic;
            s->version = src->version;
            s->mode = src->mode;
            s->in_gain = src->in_gain;
            s->in_gate = src->in_gate;
            s->retune = src->retune;
            s->glide = src->glide;
            s->humanize = src->humanize;
            s->flex = src->flex;
            s->mix = src->mix;
            s->octave = src->octave;
            s->voices = src->voices;
            s->spread = src->spread;
            s->detune = src->detune;
            s->character = src->character;
            s->harmony = src->harmony;
            s->chaos = src->chaos;
            s->scale = src->scale;
            s->formant = src->formant;
            s->formant_amt = src->formant_amt;
            {
                uint32_t i;
                for (i = 0; i < VOX_MAX_VOICES; i++)
                    s->voice[i] = src->voice[i];
                for (i = 0; i < VOX_FX_SLOTS; i++)
                    s->fx[i] = src->fx[i];
            }
            a->current = (uint8_t)((a->ui.preset_list ? VOX_FACTORY_PRESETS : 0) + a->ui.preset_sel);
            {
                char pn[16];
                ui_preset_name(a, a->ui.preset_sel, pn);
                app_say("LOADED", pn);
            }
        } else {
            a->user[a->ui.preset_sel] = *s;
            a->user[a->ui.preset_sel].magic = VOX_MAGIC;
            a->user[a->ui.preset_sel].version = VOX_PRESET_VERSION;
            a->dirty = 1u;
            {
                char pn[16];
                ui_preset_name(a, a->ui.preset_sel, pn);
                app_say("SAVED", pn);
            }
        }
        break;
    case PG_INPUT:
        if (knob == 0) {
            a->globals.mic_gain = (uint8_t)vox_clampi((int32_t)a->globals.mic_gain + d * 2, 0, 100);
            ui_pct_text((float)a->globals.mic_gain * 0.01f, t);
            app_say("GAIN", t);
        } else if (knob == 1) {
            ui_monitor_set(a, !ui_monitor(a));
            app_say("MONITOR", ui_monitor(a) ? "ON" : "OFF");
        } else if (knob == 2) {
            a->mic.rate = (a->mic.rate == 44100u) ? 48000u : 44100u;
            a->mic.bits = (a->mic.bits == 16u) ? 24u : 16u;
            ui_int_text((int32_t)a->mic.rate / 1000u, t);
            app_say("FORMAT", t);
        } else {
            a->globals.mic_on ^= 1u;
            app_say("HOST", a->globals.mic_on ? "ON" : "OFF");
        }
        break;
    default:
        if (knob == 0) {
            int nd = ui_midi_dest(a) + (d > 0 ? 1 : 3);
            ui_midi_dest_set(a, nd & 3);
            app_say("MIDI", ui_midi_name(nd & 3));
        } else if (knob == 1) {
            a->globals.out_level = (uint8_t)vox_clampi((int32_t)a->globals.out_level + d * 2, 0, 100);
            ui_pct_text((float)a->globals.out_level * 0.01f, t);
            app_say("OUT", t);
        } else if (knob == 2) {
            int nt = (ui_theme() + (d > 0 ? 1 : (int)NPALETTES - 1)) % (int)NPALETTES;
            ui_theme_set(nt);
            ui_int_text(nt, t);
            app_say("THEME", t);
        } else {
            app_say("VERSION", APP_VERSION_TEXT);
        }
        break;
    }
    a->engine.dirty = 1u;
    ui_foot_dirty(a);               /* the foot's values have moved */
}

/* the page a button opens */
static void ui_goto(app_state_t *a, int page)
{
    if ((uint8_t)page != a->ui.page) {
        a->ui.prev_page = a->ui.page;
        a->ui.page = (uint8_t)page;
        ui_foot_dirty(a);             /* the page's name and its knobs have changed */
        app_say(ui_page_name(page), "");
    }
}

/* the input: the buttons and the encoders.
 * The buttons' edges are kept in the globals' reserved bytes (the interface's own scratch); the app's
 * buttons_prev is left alone. */
void ui_input(app_state_t *a, uint32_t buttons, uint32_t keys, const int32_t enc[NE], uint32_t master)
{
    uint16_t down = ui_btn_down(a, buttons);
    int k;
    (void)keys;
    (void)master;

    if (down & UI_BIT(B_HOME))
        ui_goto(a, PG_HOME);
    if (down & UI_BIT(B_EDIT))
        ui_goto(a, PG_FORMANT);
    if (down & UI_BIT(B_FX))
        ui_goto(a, PG_FX);
    if (down & UI_BIT(B_GLO))
        ui_goto(a, PG_GLOBAL);
    if (down & UI_BIT(B_SEL))
        ui_goto(a, PG_PRESETS);
    if (down & UI_BIT(B_ENV))
        ui_goto(a, PG_INPUT);
    if (down & UI_BIT(B_LFO))
        ui_goto(a, PG_MULTIVOICE);
    if (down & UI_BIT(B_ARP)) {                  /* the two modes: the engine's mode follows */
        a->engine.set.mode = a->engine.set.mode ? 0 : 1;
        a->engine.dirty = 1u;
        ui_goto(a, a->engine.set.mode ? PG_GLIDE : PG_MULTIVOICE);
    }
    if (down & UI_BIT(B_SEQ))
        ui_goto(a, PG_MULTIVOICE);
    if (down & UI_BIT(B_SAVE)) {                /* the preset is stored in the user slots */
        a->user[a->ui.preset_sel] = a->engine.set;
        a->user[a->ui.preset_sel].magic = VOX_MAGIC;
        a->user[a->ui.preset_sel].version = VOX_PRESET_VERSION;
        a->dirty = 1u;
        a->ui.dirty = 1u;
        {
            char pn[16];
            ui_preset_name(a, a->ui.preset_sel, pn);
            app_say("SAVED", pn);
        }
    }
    if (down & UI_BIT(B_OCTDN)) {               /* the octave: the preset's own field */
        a->engine.set.octave = (int8_t)vox_clampi((int32_t)a->engine.set.octave - 1, -2, 2);
        a->engine.dirty = 1u;
        ui_foot_dirty(a);
        app_say("OCTAVE", "");
    }
    if (down & UI_BIT(B_OCTUP)) {
        a->engine.set.octave = (int8_t)vox_clampi((int32_t)a->engine.set.octave + 1, -2, 2);
        a->engine.dirty = 1u;
        ui_foot_dirty(a);
        app_say("OCTAVE", "");
    }

    for (k = 0; k < NE; k++) {
        int32_t d = enc[k];
        if (!d)
            continue;
        if (k == EN_K1 || k == EN_K2 || k == EN_K3 || k == EN_K4) {
            ui_knob(a, k - EN_K1, d);
        } else if (k == EN_ALGO) {              /* the two modes */
            a->engine.set.mode = a->engine.set.mode ? 0 : 1;
            a->engine.dirty = 1u;
            ui_goto(a, a->engine.set.mode ? PG_GLIDE : PG_MULTIVOICE);
            app_say("MODE", a->engine.set.mode ? "GLIDE" : "MULTIVOICE");
        } else if (k == EN_PRESET) {           /* the preset browser */
            a->ui.preset_sel = (uint8_t)vox_clampi((int32_t)a->ui.preset_sel + d, 0, 15);
            ui_foot_dirty(a);
            ui_goto(a, PG_PRESETS);
        } else {                               /* SELECT: the page's own list */
            if (a->ui.page == PG_FX)
                a->ui.fx_focus = (uint8_t)((a->ui.fx_focus + (d > 0 ? 1 : 3)) & 3u);
            else if (a->ui.page == PG_PRESETS)
                a->ui.preset_sel = (uint8_t)vox_clampi((int32_t)a->ui.preset_sel + d, 0, 15);
            ui_foot_dirty(a);
        }
    }
}

/* ------------------------------------------------------------------ the drawing --- */

/* the interface's reset */
void ui_init(ui_t *u)
{
    u->page = PG_HOME;
    u->prev_page = PG_HOME;
    u->fx_focus = 0;
    u->preset_list = 0;
    u->preset_sel = 0;
    u->dirty = 1u;
    u->show_help = 0u;
    u->now_ms = 0u;
    u->last_frame_ms = 0u;
}

/* the current page's drawing */
void ui_draw(app_state_t *a, int32_t x, int32_t y, int32_t w, int32_t h)
{
    switch (a->ui.page) {
    case PG_MULTIVOICE: ui_draw_multivoice(a, x, y, w, h); break;
    case PG_GLIDE:      ui_draw_glide(a, x, y, w, h); break;
    case PG_FORMANT:    ui_draw_formant(a, x, y, w, h); break;
    case PG_FX:         ui_draw_fx(a, x, y, w, h); break;
    case PG_PRESETS:    ui_draw_presets(a, x, y, w, h); break;
    case PG_INPUT:      ui_draw_input(a, x, y, w, h); break;
    case PG_GLOBAL:     ui_draw_global(a, x, y, w, h); break;
    default:            ui_draw_home(a, x, y, w, h); break;
    }
}
