/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the app's half of the contract with fm1-base.
 *
 * This file is the glue: it owns the platform's lifecycle (the seven functions the platform calls), the
 * microphone's capture and its format conversion, the presets and their storage, and the wiring between
 * the DSP engine and the interface.
 *
 * The audio interrupt (app_render) and the UI frame (app_frame) share one state (app_state.h). The render
 * owns the engine and publishes what the display reads; the frame owns the interface and the settings.
 */
#include "app.h"
#include "vox.h"
#include "vox_math.h"

/* the state the whole app shares */
static app_state_t st;

/* the USB host's mode, as it was last asked for (0xFF = never) */
static uint8_t usb_host_applied;

static void app_usb_host(void);

/* a copy, cut to fit (the interface has one of its own; the app's own code cannot use it, because the
 * interface's file is read in at the end of this one) */
static void app_copy(char *out, const char *src, uint32_t max)
{
    uint32_t i = 0;
    if (max < 1u) {
        out[0] = 0;
        return;
    }
    while (src[i] && i < max - 1u) {
        out[i] = src[i];
        i++;
    }
    out[i] = 0;
}

/* ------------------------------------------------------------------ the pool --- */

/* The big buffers. They live in the platform's RAM pool (not in .bss): the pool is 336 KiB, the app's RAM
 * is 96 KiB, and the DSP must not allocate. The engine's own state is small enough for the RAM.
 *
 * The shifters' rings and accumulators are here too: four voices with a ring and an accumulator each
 * is 96 KiB, which would not fit in the app's RAM. */
static int16_t delay_l[VOX_FX_DELAY_MAX] PLAT_POOL;
static int16_t delay_r[VOX_FX_DELAY_MAX] PLAT_POOL;
static int16_t reverb_l[VOX_FX_REVERB_MAX] PLAT_POOL;
static int16_t reverb_r[VOX_FX_REVERB_MAX] PLAT_POOL;
static float psola_ring[VOX_MAX_VOICES][VOX_PSOLA_RING] PLAT_POOL;
static float psola_ola[VOX_MAX_VOICES][VOX_OLA_LEN] PLAT_POOL;

/* The microphone's jitter buffer.
 *
 * The device delivers frames in 1 ms packets (44 at 44.1 kHz, 48 at 48 kHz) and the app's block is
 * 256 frames at 44.1 kHz, so the producer is smooth and the consumer is bursty: the buffer has to
 * carry a whole block. The app reads only when the buffer holds MIC_TARGET frames, and the level
 * is kept there by a small correction to the read rate (the device's crystal and the engine's are
 * never exactly the same, and without the correction the buffer would fill up or run dry over a
 * few minutes).
 *
 * The FIFO holds one channel per row and counts frames, not samples. The counters only grow (they
 * are never wrapped), so "tail - head" is always the number of frames waiting. */
#define MIC_FIFO 1024u                       /* the frames in the buffer: 23 ms at 44.1 kHz */
#define MIC_TARGET 128u                      /* the frames the servo holds (before a block): 2.9 ms */
#define MIC_READY 32u                        /* below this the block cannot be covered: the app waits */
#define MIC_LOW 8u                           /* below this the device is late: the app holds its place */
#define MIC_PULL 512u                        /* the frames the app takes from the capture at once */
static int32_t mic_fifo[2][MIC_FIFO] PLAT_POOL;
static uint32_t mic_head, mic_tail;         /* the FIFO's ends, in frames */
static float mic_phase;                     /* the read position inside the frame at mic_head */
static float mic_last;                      /* the last value out (the hold, when the device is late) */
static float mic_corr;                      /* the read rate's correction (the level's servo) */
static float mic_level_f;                    /* the level, averaged: the servo steers on this */
static int32_t mic_raw[MIC_PULL * 2u];      /* the capture's interleaved samples */

/* ------------------------------------------------------------------ the presets --- */

#include "presets/factory_presets.c"

/* the user presets, in one store object: a header, the slots' names, then the 16 presets */
typedef struct {
    uint32_t magic;                     /* "AVP1" */
    uint8_t count;                      /* how many are written */
    uint8_t rsv[3];
    char name[VOX_USER_PRESETS][16];    /* the slots' names */
    vox_preset_t preset[VOX_USER_PRESETS];
} user_presets_t;

#define USER_MAGIC 0x31505641u           /* "AVP1" */
_Static_assert(sizeof(user_presets_t) <= 3840u, "a store object holds 3840 bytes");

/* ------------------------------------------------------------------ the settings --- */

/* the settings blob: the globals and the current preset.
 * It only grows at the end: a shorter (older) save loads into the front and the fields after it keep their
 * defaults. A change that is not an append needs a new magic (and the saves are lost). */
typedef struct {
    uint32_t magic;
    vox_globals_t globals;
    vox_preset_t current;
} settings_t;

static settings_t saved;

/* the defaults: the "CLEAN STUDIO" preset and the default globals */
static void defaults(void)
{
    memset(&st.globals, 0, sizeof st.globals);
    st.globals.magic = SET_MAGIC;
    st.globals.mic_on = 1;
    st.globals.mic_gain = 70;
    st.globals.out_level = 100;
    st.globals.last_preset = 15;         /* CLEAN STUDIO */
    st.globals.last_page = PG_HOME;
    st.engine.set = st.factory[15];      /* the factory's CLEAN STUDIO */
    st.current = 15;
}

/* ------------------------------------------------------------------ the storage --- */

static void load_settings(void)
{
    int n = plat_store_load(OBJ_SETTINGS, &saved, sizeof saved);
    if (n != (int)sizeof saved || saved.magic != SET_MAGIC) {
        defaults();
        return;
    }
    st.globals = saved.globals;
    st.engine.set = saved.current;
    if (st.engine.set.magic != VOX_MAGIC || st.engine.set.mode > 1u || st.engine.set.voices < 1u ||
        st.engine.set.voices > VOX_MAX_VOICES) {
        defaults();
        return;
    }
    if (st.globals.last_preset < VOX_FACTORY_PRESETS)
        st.current = st.globals.last_preset;
    st.engine.set.magic = VOX_MAGIC;
}

static void save_settings(void)
{
    saved.magic = SET_MAGIC;
    saved.globals = st.globals;
    saved.current = st.engine.set;
    if (plat_store_save(OBJ_SETTINGS, &saved, sizeof saved) != 0)
        app_say("SAVE", "FAILED");
}

static void load_user_presets(void)
{
    user_presets_t up;
    uint32_t i;
    if (plat_store_load(OBJ_PRESETS, &up, sizeof up) != (int)sizeof up || up.magic != USER_MAGIC) {
        for (i = 0; i < VOX_USER_PRESETS; i++) {
            st.user[i] = st.factory[i];
            /* a fresh slot carries the factory preset's name, so the browser reads sensibly */
            app_copy(st.user_name[i], vox_factory_name(i), 16);
        }
        up.magic = USER_MAGIC;
        up.count = VOX_USER_PRESETS;
        for (i = 0; i < VOX_USER_PRESETS; i++) {
            up.preset[i] = st.factory[i];
            app_copy(up.name[i], vox_factory_name(i), 16);
        }
        plat_store_save(OBJ_PRESETS, &up, sizeof up);
        return;
    }
    for (i = 0; i < VOX_USER_PRESETS; i++) {
        st.user[i] = up.preset[i];
        app_copy(st.user_name[i], up.name[i], 16);
    }
}

static void save_user_preset(uint32_t index)
{
    user_presets_t up;
    uint32_t i;
    up.magic = USER_MAGIC;
    up.count = VOX_USER_PRESETS;
    for (i = 0; i < VOX_USER_PRESETS; i++) {
        up.preset[i] = st.user[i];
        app_copy(up.name[i], st.user_name[i], 16);
    }
    up.preset[index & 15u] = st.engine.set;
    if (plat_store_save(OBJ_PRESETS, &up, sizeof up) != 0)
        app_say("SAVE", "FAILED");
}

/* ------------------------------------------------------------------ the microphone --- */

/* the converter: the device's PCM to the engine's float at 44.1 kHz.
 *
 * The stage belongs here (the app's side of the platform's boundary): the engine always works at
 * VOX_SR with mono floats. The jitter buffer holds the frames that were read but not used yet, so
 * that a device at 48 kHz is read a little slower and nothing is lost or repeated.
 *
 * ready says the buffer holds enough for a whole block (the caller checked the level before the
 * capture was read). The correction is the servo: the level decides how much faster or slower the
 * content is read, so that the buffer keeps its level whatever the two clocks do. */
static void mic_convert(uint32_t channels, uint32_t bits, float *out, uint32_t n, int ready)
{
    float scale = bits > 16u ? (1.0f / 8388608.0f) : (1.0f / 32768.0f);
    float base = 1.0f;
    float step;
    uint32_t i;
    uint32_t live = mic_tail - mic_head;    /* the frames in the buffer */

    if (st.mic.rate && st.mic.rate != VOX_SR)
        base = (float)st.mic.rate / (float)VOX_SR;
    step = base * (1.0f + mic_corr);
    if (channels < 1u)
        channels = 1u;

    for (i = 0; i < n; i++) {
        float l = 0.0f, r = 0.0f, frac;
        uint32_t k;

        if (!ready) {                        /* the buffer is not ready yet: silence */
            out[i] = 0.0f;
            continue;
        }
        if (live <= MIC_LOW) {               /* the device is late: the app holds its place and
                                             * repeats the last value (a hold is quiet, a zero is a
                                             * click) */
            out[i] = mic_last;
            continue;
        }
        frac = mic_phase;
        k = mic_head % MIC_FIFO;
        l = (float)mic_fifo[0][k] * scale;
        r = (float)mic_fifo[1][k] * scale;
        if (frac > 0.0f && live > 1u) {      /* the next frame is there: interpolate */
            uint32_t k1 = (k + 1u) % MIC_FIFO;
            l += ((float)mic_fifo[0][k1] * scale - l) * frac;
            r += ((float)mic_fifo[1][k1] * scale - r) * frac;
        }
        out[i] = channels > 1u ? (l + r) * 0.5f : l;
        mic_last = out[i];

        /* the next frame's place: the step's fraction, then the whole frames */
        mic_phase += step;
        while (mic_phase >= 1.0f) {
            mic_phase -= 1.0f;
            mic_head++;
            live--;
        }
    }

    /* the servo: the level decides the correction. A thousandth of a percent is a hundredth of a
     * cent, so the correction is inaudible; it only keeps the buffer where it belongs. */
    {
        float err, c;

        /* The level is averaged first: the device's 1 ms packets make the raw level sawtooth, and
         * chasing that sawtooth would put a slow wobble on the pitch. */
        mic_level_f += ((float)live - mic_level_f) * 0.05f;
        err = mic_level_f - (float)MIC_TARGET;
        c = err * 1.0e-4f;

        if (c > 0.003f)
            c = 0.003f;
        if (c < -0.003f)
            c = -0.003f;
        mic_corr = c;
    }
}

/* ------------------------------------------------------------------ the platform's contract --- */

void app_init(void)
{
    uint32_t i;
    vox_buffers_t bufs;

    for (i = 0; i < VOX_FACTORY_PRESETS; i++)
        st.factory[i] = vox_factory_preset(i);

    load_user_presets();
    load_settings();

    bufs.delay_l = delay_l;
    bufs.delay_r = delay_r;
    bufs.reverb_l = reverb_l;
    bufs.reverb_r = reverb_r;
    for (i = 0; i < VOX_MAX_VOICES; i++) {
        bufs.ring[i] = psola_ring[i];
        bufs.ola[i] = psola_ola[i];
    }

    vox_engine_init(&st.engine, &bufs);
    st.engine.set = st.factory[st.current < VOX_FACTORY_PRESETS ? st.current : 15u];

    st.ui.page = st.globals.last_page < NPAGES ? st.globals.last_page : PG_HOME;
    ui_init(&st.ui);
    anatomy_init(&st.anatomy);
    mic_head = mic_tail = 0;
    mic_phase = 0.0f;

    /* the USB port becomes a host when the microphone is on: the FM-1 gives the port no power, so a
     * powered hub is needed (docs/USB_AUDIO.md) */
    usb_host_applied = 0xFFu;
    app_usb_host();
}

/* the USB host's mode follows the INPUT page's switch. plat_usb_host is asked from the frame (the
 * main loop), never from the audio interrupt: connecting blocks the main loop a moment. */
static void app_usb_host(void)
{
    uint8_t want = st.globals.mic_on ? 1u : 0u;
    if (usb_host_applied == want)
        return;
    usb_host_applied = want;
    plat_usb_host((int)want);
}

void app_panic(void)
{
    /* silence now: the update is about to rewrite the flash */
    st.engine.set.mix = 0;
    st.engine.input.gain = 0.0f;
}

void app_ui_init(void)
{
    ui_init(&st.ui);
    st.ui.dirty = 1;
}

void app_say(const char *a, const char *b)
{
    (void)a;
    (void)b;
    /* the platform's screen reader announces through app_say; the app's own screens show the values */
}

/* the audio interrupt: n stereo frames of 24-bit output. */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static float in[VOX_BLOCK];
    uint32_t i, got = 0;
    float gain;
    int ready = 0;

    if (n > VOX_BLOCK)
        n = VOX_BLOCK;

    /* the microphone: the platform's capture (the USB host's PCM) */
    st.mic.state = (uint8_t)plat_mic_state();
    st.mic.rate = plat_mic_rate();
    st.mic.channels = (uint8_t)plat_mic_channels();
    st.mic.bits = (uint8_t)plat_mic_bits();
    if (st.mic.state == 2 && st.mic.rate) {
        uint32_t ch = st.mic.channels ? st.mic.channels : 1u;
        uint32_t live = mic_tail - mic_head;
        uint32_t room = MIC_FIFO - live;
        uint32_t frames = room > MIC_PULL ? MIC_PULL : room;
        uint32_t want = frames * ch;

        /* The capture is drained at every block: the buffer in the app is the one that smooths the
         * device's 1 ms packets against the app's 5.8 ms blocks. The capture hands over whole
         * frames, so the split below keeps the channels together. */
        got = (uint32_t)plat_mic_read(mic_raw, want);
        for (i = 0; i + ch <= got; i += ch) {
            uint32_t k = mic_tail % MIC_FIFO;
            mic_fifo[0][k] = mic_raw[i];
            if (ch > 1u)
                mic_fifo[1][k] = mic_raw[i + 1u];
            mic_tail++;
        }
        ready = live >= MIC_READY;              /* enough for a whole block? */
    } else {
        mic_head = mic_tail = 0;              /* the microphone is gone: nothing is pending */
        mic_phase = 0.0f;
        mic_last = 0.0f;
        mic_corr = 0.0f;
        ready = 0;
    }
    st.mic.got = got;
    mic_convert(st.mic.channels ? st.mic.channels : 1u, st.mic.bits ? st.mic.bits : 16u, in, n, ready);

    /* the engine */
    st.engine.now_ms = plat_ms();
    vox_engine_process(&st.engine, in, n, st.engine.now_ms);

    /* what the display reads */
    st.pub.level = st.engine.level;
    st.pub.peak = st.engine.input.clip ? 1.0f : st.engine.level;
    st.pub.f0 = st.engine.pitch.f0;
    st.pub.target_f0 = st.engine.target_f0;
    st.pub.clarity = st.engine.pitch.clarity;
    st.pub.note = st.engine.pitch.note;
    st.pub.octave = st.engine.pitch.octave;
    st.pub.target_note = st.engine.correct.note;
    st.pub.voiced = st.engine.pitch.voiced;

    /* out: the MASTER pot's gain, the platform's scale (24 bit) */
    gain = (float)master_q12 * (1.0f / 4096.0f) * 8300000.0f;
    for (i = 0; i < n; i++) {
        lr[2u * i] = (int32_t)(st.engine.l[i] * gain);
        lr[2u * i + 1u] = (int32_t)(st.engine.r[i] * gain);
    }
}

/* between the frames: the input only */
void app_input_only(void)
{
    uint32_t b = plat_buttons();
    uint32_t k = plat_keys();
    int32_t e;

    st.buttons = b;
    st.keys = k;
    for (e = 0; e < NE; e++)
        st.enc[e] = plat_enc(e);

    /* the encoders' movement goes to the interface as well: the host calls this between the frames
     * (15 times out of 16), and the knobs must not wait for the next full frame */
    ui_input(&st, b, k, st.enc, st.master);
}

/* the frame: the input and a redraw, about 60 times a second */
void app_frame(void)
{
    uint32_t b = plat_buttons();
    uint32_t k = plat_keys();
    int32_t e;

    st.buttons = b;
    st.keys = k;
    for (e = 0; e < NE; e++)
        st.enc[e] = plat_enc(e);
    st.master = plat_master();
    st.frame_ms = plat_ms();

    ui_input(&st, b, k, st.enc, st.master);

    /* the anatomy's state: what it reacts to */
    {
        anatomy_in_t in;
        in.level = st.pub.level;
        in.peak = st.pub.peak;
        in.f0 = st.pub.f0;
        in.target_f0 = st.pub.target_f0;
        in.clarity = st.pub.clarity;
        in.voiced = st.pub.voiced;
        in.formant = (float)st.engine.set.formant;
        in.nasal = 0.0f;
        in.distort = 0.0f;
        in.delay_amt = 0.0f;
        in.reverb_amt = 0.0f;
        in.compress = 0.0f;
        in.overload = st.engine.input.clip ? 1.0f : 0.0f;
        in.voices = st.engine.set.voices;
        in.voice_f0[0] = st.pub.f0;
        in.voice_level[0] = st.pub.level;
        in.now_ms = st.frame_ms;
        anatomy_update(&st.anatomy, &in, st.frame_ms);
    }

    ui_draw(&st, 0, 0, 240, 240);
}

/* ------------------------------------------------------------------ the simulator's checks --- */

#ifdef PLAT_HOST
/* The host simulator's checks: the page, the mode, the detected note, the level, the preset.
 * Host-only: the device's libc has no strcmp, and nothing on the device needs these. */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got)
{
    if (!what)
        return 0;
    if (!strcmp(what, "page"))
        *got = st.ui.page;
    else if (!strcmp(what, "mode"))
        *got = st.engine.set.mode;
    else if (!strcmp(what, "note"))
        *got = st.pub.note;
    else if (!strcmp(what, "target_note"))
        *got = st.pub.target_note;
    else if (!strcmp(what, "voices"))
        *got = st.engine.set.voices;
    else if (!strcmp(what, "preset"))
        *got = st.current;
    else if (!strcmp(what, "dirty"))
        *got = st.dirty;
    else if (!strcmp(what, "mix"))
        *got = st.engine.set.mix;
    else if (!strcmp(what, "f0"))
        *got = (int)(st.pub.f0 * 10.0f);
    else if (!strcmp(what, "level"))
        *got = (int)(st.pub.level * 1000.0f);
    else if (!strcmp(what, "mic_state"))
        *got = st.mic.state;
    else if (!strcmp(what, "mic_got"))
        *got = (int)st.mic.got;
    else if (!strcmp(what, "mic_rate"))
        *got = (int)st.mic.rate;
    else if (!strcmp(what, "in_gain"))
        *got = st.engine.set.in_gain;
    else if (!strcmp(what, "gain"))
        *got = (int)(st.engine.input.gain * 100.0f);
    else if (!strcmp(what, "clarity"))
        *got = (int)(st.pub.clarity * 100.0f);
    else if (!strcmp(what, "rms"))
        *got = (int)(st.engine.pitch.rms * 1000.0f);
    else if (!strcmp(what, "voiced"))
        *got = st.pub.voiced;
    else if (!strcmp(what, "gate"))
        *got = (int)(st.engine.input.gate_open * 100.0f);
    else if (!strcmp(what, "env"))
        *got = (int)(st.engine.input.env * 1000.0f);
    else if (!strcmp(what, "block")) {
        uint32_t i;
        float pk = 0.0f;
        for (i = 0; i < VOX_BLOCK; i++) {
            float a = st.engine.block[i] < 0.0f ? -st.engine.block[i] : st.engine.block[i];
            if (a > pk)
                pk = a;
        }
        *got = (int)(pk * 1000.0f);
    }
    else
        return 0;
    return 1;
}
#endif /* PLAT_HOST */

/* ------------------------------------------------------------------ the screens --- */

#include "ui/ui_common.c"
#include "ui/ui_home.c"
#include "ui/ui_multivoice.c"
#include "ui/ui_glide.c"
#include "ui/ui_formant.c"
#include "ui/ui_fx.c"
#include "ui/ui_presets.c"
#include "ui/ui_input.c"
#include "ui/ui_global.c"

#include "animation/anatomy_lungs.c"
#include "animation/anatomy_vocal_folds.c"
#include "animation/anatomy_airflow.c"
#include "animation/anatomy_formants.c"
#include "animation/anatomy_effects.c"
