/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the DSP engine's public API.
 *
 * Platform-free on purpose: this header includes no plat.h and no hardware header, so app/dsp/ compiles
 * standalone with the computer's own C compiler (tests/dsp/run.sh) exactly like fm1-base's own rules ask.
 * The rules the whole engine obeys:
 *   - float only, never double (the device's compiler rejects it; the tests build with -Wdouble-promotion)
 *   - no malloc: every buffer is static or in the engine's own struct
 *   - no allocation, no flash reads, no blocking in the render path
 *   - every module's state is in a struct the caller owns; the modules hold no hidden globals
 */
#pragma once
#include <stdint.h>

#define VOX_SR 44100u                   /* the engine's rate: the FM-1's audio rate */
#define VOX_BLOCK 256u                  /* frames per block: 5.8 ms at 44.1 kHz */
#define VOX_MAX_VOICES 4u               /* the original lead plus three processed voices */
#define VOX_FX_SLOTS 4u                 /* the four reorderable effect slots */
#define VOX_FACTORY_PRESETS 16u
#define VOX_USER_PRESETS 16u

/* the sizes: the memory is tight (96 KiB of RAM for the app, 336 KiB of pool for the big buffers) */
#define VOX_PSOLA_RING 2048u            /* the shifter's analysis ring: 46 ms at 44.1 kHz */
#define VOX_OLA_LEN 2048u               /* the overlap-add's accumulator: the block plus two grains */
#define VOX_FORMANT_ORDER 12u           /* the LPC order: enough for F5 (5000 Hz) */
#define VOX_FX_DELAY_MAX 16384u         /* the delay line: 372 ms at 44.1 kHz, 16-bit */
#define VOX_FX_REVERB_MAX 8192u         /* the reverb's line: 186 ms at 44.1 kHz, 16-bit */
#define VOX_DET_DEC 1024u               /* the detector's decimated history: 46 ms at 22.05 kHz */
#define VOX_DET_MIN_HZ 65.0f            /* the lowest fundamental the detector looks for */
#define VOX_DET_MAX_HZ 1100.0f          /* the highest */

/* the big buffers the engine needs. The app owns them (it puts them in the platform's RAM pool: the DSP stays
 * platform-free and never allocates), so the engine's own state stays small enough for the device's RAM.
 *
 * The shifters' rings and accumulators live here too: they are the biggest part of the engine (four
 * voices with a ring and an accumulator each), and they belong in the pool rather than in the app's
 * 96 KiB of RAM. */
typedef struct {
    int16_t *delay_l;                  /* VOX_FX_DELAY_MAX samples */
    int16_t *delay_r;
    int16_t *reverb_l;                 /* VOX_FX_REVERB_MAX samples */
    int16_t *reverb_r;
    float *ring[VOX_MAX_VOICES];       /* VOX_PSOLA_RING samples each: the shifter's analysis ring */
    float *ola[VOX_MAX_VOICES];        /* VOX_OLA_LEN samples each: the shifter's overlap-add */
} vox_buffers_t;

/* the buffers the engine needs, ready for the platform's pool (the caller fills the pointers in) */
void vox_buffers_sizes(uint32_t *delay_len, uint32_t *reverb_len, uint32_t *ring_len, uint32_t *ola_len);

/* ------------------------------------------------------------------ the scales --- */

/* the scale's mode */
enum {
    VOX_SCALE_CHROMATIC = 0,
    VOX_SCALE_MAJOR,
    VOX_SCALE_MINOR,
    VOX_SCALE_MINOR_PENTA,
    VOX_SCALE_MAJOR_PENTA,
    VOX_SCALE_DORIAN,
    VOX_SCALE_BLUES,
    VOX_SCALE_CUSTOM,                  /* the 12-bit custom mask below */
    VOX_SCALE_COUNT
};

/* a key and a scale: the pitch classes the corrector snaps to */
typedef struct {
    uint8_t root;                      /* 0..11, C..B */
    uint8_t scale;                     /* VOX_SCALE_* */
    uint8_t custom[12];                /* 0 or 1 per pitch class, for VOX_SCALE_CUSTOM */
} vox_scale_t;

/* the scale's note names, for the display ("C", "C#", ...)
 * vox_scale_note_name(scale, pitch_class) writes up to 3 bytes into out and returns it */
char *vox_scale_note_name(const vox_scale_t *scale, int pitch_class, char *out);

/* is a pitch class in the scale? */
int vox_scale_has(const vox_scale_t *scale, int pitch_class);

/* the scale's pitch classes, in semitones from the root: vox_scale_steps writes them into out (up to 12)
 * and returns the count */
int vox_scale_steps(const vox_scale_t *scale, int *out);

/* the scale's defaults (the scale field set) */
void vox_scale_default(vox_scale_t *scale, int mode, int root);

/* ------------------------------------------------------------------ the pitch --- */

/* what the detector found */
typedef struct {
    float f0;                          /* Hz, 0 when unvoiced */
    float clarity;                     /* 0..1: how periodic the input is */
    float rms;                         /* 0..1: the input's level */
    float period;                      /* samples, 0 when unvoiced */
    int8_t note;                       /* the MIDI note (60 = C4), -1 when none */
    int8_t octave;                     /* the note's octave, -1 when none */
    int8_t voiced;                     /* 1 when the last block was voiced */
} vox_pitch_t;

/* the detector's state */
typedef struct {
    float hist[VOX_BLOCK];             /* the ring of the last block's samples */
    uint32_t hist_pos;                 /* where the next sample goes */
    uint32_t filled;                   /* how much of the ring is valid */
    float dec[VOX_DET_DEC];            /* the decimated history (2x): the analysis window */
    uint32_t dec_pos;                  /* where the next decimated sample goes */
    uint32_t dec_filled;               /* how much of the decimated history is valid */
    float dec_carry;                   /* the decimator's carry */
    float f0;                          /* the smoothed f0 */
    float clarity;
    float rms;
    float period;
    int8_t note;
    int8_t octave;
    int8_t voiced;
    uint32_t seed;                     /* the detector's noise (deterministic) */
} vox_detect_t;

/* reset the detector */
void vox_detect_init(vox_detect_t *d);

/* find the fundamental in a block of n samples (n <= VOX_BLOCK).
 * Returns 1 when the block is voiced (d->f0 is then the fundamental), 0 when it is not.
 * The detector must be robust against: breath noise, a noisy room, vibrato, and a note change inside
 * the block. It reports its confidence in d->clarity. */
int vox_detect(vox_detect_t *d, const float *x, uint32_t n);

/* ------------------------------------------------------------------ the corrector --- */

/* the glide: the curve between two notes (the portamento).
 * The corrector owns the note's choice; the glide owns the travel between the notes. */
typedef struct {
    float from;                        /* Hz, where the voice came from */
    float to;                          /* Hz, where it is going */
    float t;                           /* 0..1, how far it has travelled */
    uint32_t start_ms;                 /* when the note changed */
    uint8_t moving;                    /* 1 while the glide is running */
} vox_glide_t;

void vox_glide_init(vox_glide_t *g);

/* start a glide towards a new note */
void vox_glide_start(vox_glide_t *g, float from_hz, float to_hz, uint32_t now_ms);

/* the glide's current pitch, in Hz.
 * retune (0..100) sets how fast the voice arrives at the note; glide_amt (0..100) sets how long the
 * travel itself takes: 0 = an instant change, 50 = a smooth portamento, 100 = an extreme glide that never
 * quite arrives. The result is the pitch the voice should sound right now. */
float vox_glide_at(vox_glide_t *g, uint8_t retune, uint8_t glide_amt, uint32_t now_ms);

/* the corrector's state: the target pitch curve, smoothed and snapped */
typedef struct {
    float target;                      /* Hz, the note the voice is heading for */
    float cur;                         /* Hz, where the target is now (the glide) */
    float bend;                        /* semitones, the current offset from the detected pitch */
    int8_t note;                       /* the MIDI note the target is (60 = C4), -1 when none */
    int8_t octave;
    uint8_t active;                    /* 1 when a note is held */
    uint32_t last_voiced_ms;           /* the tick of the last voiced block */
    uint32_t seed;                     /* the humanise's dice */
    vox_glide_t glide;                 /* the travel between the notes */
} vox_correct_t;

/* the corrector's parameters (a copy of the preset's, for the engine) */
typedef struct {
    uint8_t retune;                    /* 0..100: how fast the voice snaps to the note */
    uint8_t glide;                     /* 0..100: how soft the note changes are */
    uint8_t humanize;                  /* 0..100: the natural variation in a held note */
    uint8_t flex;                      /* 0..100: how much of the original pitch survives */
    uint8_t mix;                       /* 0..100: the corrected signal against the dry one */
    int8_t octave;                     /* -2..+2: the whole voice's octave */
    vox_scale_t scale;
} vox_correct_param_t;

void vox_correct_init(vox_correct_t *c);

/* the corrector's target for the detected pitch.
 * The behaviour (the PRD's GLIDE):
 *   RETUNE  0 = natural, 25 = subtle, 50 = noticeable, 75 = aggressive, 100 = instant
 *   GLIDE   0 = instant note changes, 50 = smooth portamento, 100 = extreme gliding
 *   HUMANIZE introduces a controlled drift into a held note
 *   FLEX    lets the original pitch variation survive the correction
 * The returned Hz is the target the voice should sound; c->note / c->octave are the target's note.
 * now_ms is the engine's millisecond clock (for the note-change timing). */
float vox_correct_target(vox_correct_t *c, const vox_correct_param_t *p, const vox_pitch_t *in, uint32_t now_ms);

/* ------------------------------------------------------------------ the voice --- */

/* a voice's parameters (a copy of the preset's) */
typedef struct {
    uint8_t on;                        /* 0 = the voice is silent */
    int8_t offset;                     /* semitones, the voice's pitch offset */
    int8_t fine;                       /* cents, -50..+50 */
    int8_t formant;                    /* -12..+12, the voice's formant offset */
    uint8_t level;                     /* 0..100 */
    uint8_t pan;                       /* 0..100, 50 = centre */
    uint8_t width;                     /* 0..100, the stereo width */
    uint8_t detune;                    /* 0..100, the pitch jitter */
    uint8_t send;                      /* 0..100, the effect send */
} vox_voice_param_t;

/* a voice's state: the shifter, the formant filter and its own envelopes */
typedef struct {
    /* the shifter's buffers: the caller's (the pool's) */
    float *ring;                        /* VOX_PSOLA_RING samples: the analysis ring */
    float *ola;                         /* VOX_OLA_LEN samples: the overlap-add accumulator */
    /* the shifter */
    float out[VOX_BLOCK];              /* the voice's own output, the dry part */
    uint32_t ring_pos;
    float last_period;                 /* the last input period the shifter used */
    uint32_t written;                  /* the shifter's sample counter */
    float delay;                       /* the shifter's analysis delay, in samples */
    float src;                         /* the shifter's output clock */
    float ana;                         /* the shifter's content clock */
    float frac_acc;                    /* the content step's accumulator */
    uint8_t have_tail;                 /* the shifter has a tail to align against */
    float tail[64];                    /* the alignment's correlation tail */
    /* the formant filter */
    float lpc_a[VOX_FORMANT_ORDER + 1u];
    float lpc_b[VOX_FORMANT_ORDER + 1u];
    float lpc_hist[VOX_FORMANT_ORDER];
    float lpc_state[VOX_FORMANT_ORDER];
    float resid[VOX_BLOCK];
    /* the voice's own mix */
    float pan_l, pan_r;
    float level;
} vox_voice_t;

/* the voice's reset: it takes the shifter's buffers from the caller's pool (the voice's own number
 * picks which ring and which accumulator) */
void vox_voice_init(vox_voice_t *v, const vox_buffers_t *bufs, uint32_t index);

/* the formant engine (vox_formant.c): the tract's shift, independent of the pitch */
void vox_formant_init(vox_voice_t *v);
void vox_formant_shift(vox_voice_t *v, const float *in, uint32_t n, float semitones, float *out);

/* the shifter (vox_pitch_shift.c): the pitch moves, the duration does not */
void vox_shift_init(vox_voice_t *v);
void vox_shift_process(vox_voice_t *v, const float *in, uint32_t n, float period, float semitones, float cents,
                       float *out);

/* the voice's dry output for a block.
 * in is the input block (mono, n samples); the detected period is the input's pitch period (0 = unknown).
 * The shifter moves the voice by the given semitone offset and fine cents, and keeps its formant offset
 * independent of the pitch. The result is in v->out (n samples, mono). */
void vox_voice_render(vox_voice_t *v, const float *in, uint32_t n, float period, float semitones, float cents,
                      float formant_semitones);

/* ------------------------------------------------------------------ the harmoniser --- */

/* the harmony: which interval each voice sounds */
typedef struct {
    uint8_t count;                     /* 1..VOX_MAX_VOICES */
    int8_t interval[VOX_MAX_VOICES];   /* semitones, the voice's interval from the root */
    uint8_t spread;                    /* 0..100: the stereo spread */
    uint8_t detune;                    /* 0..100: the pitch differences between the voices */
    uint8_t character;                 /* 0..100: natural to robotic */
    uint8_t humanize;                  /* 0..100: the small pitch and timing variations */
    uint8_t chaos;                     /* 0..100: the randomised voice behaviour */
    uint8_t harmony;                   /* the scale-aware interval selection, 0..11 */
    uint32_t seed;                     /* the chaos' dice */
} vox_harm_t;

void vox_harm_init(vox_harm_t *h);

/* the harmoniser's plan for the current note.
 * The voice count, the intervals (the lead plus the harmony), the spread, the detune and the chaos are
 * worked out here; the pitches are relative to the detected note. */
void vox_harm_plan(vox_harm_t *h, const vox_scale_t *scale, int8_t note, int8_t octave, uint32_t now_ms);

/* the voice's pitch offset, in semitones, from the plan (voice 0 is the lead) */
float vox_harm_offset(const vox_harm_t *h, uint32_t voice, float detune_cents, uint32_t now_ms);

/* ------------------------------------------------------------------ the input --- */

/* the input's state: the gain, the gate and the high-pass before the engine */
typedef struct {
    float gain;                        /* the linear gain */
    float hp_z[2];                     /* the high-pass's state */
    float env;                         /* the envelope follower (0..1) */
    float gate_open;                   /* 1 = the gate is open */
    float level;                       /* the meter's level (0..1) */
    float clip;                        /* 1 when the input overloaded */
} vox_input_t;

void vox_input_init(vox_input_t *in);

/* the input's conditioning: the gain, the noise gate and the high-pass.
 * out is the conditioned block (n samples); the block's level is left in in->level. */
void vox_input_process(vox_input_t *in, const float *x, float *out, uint32_t n, uint8_t gate_pct);

/* ------------------------------------------------------------------ the dynamics --- */

typedef struct {
    float thresh;                      /* the gate's threshold, linear */
    float env;                         /* the envelope follower */
    float gain;                        /* the compressor's gain, linear */
    float atk, rel;                    /* the one-pole coefficients */
    float deess;                       /* the de-esser's reduction, 0..1 */
    float deess_env;
    float hp_z[2];
} vox_dyn_t;

void vox_dyn_init(vox_dyn_t *d);

/* the dynamics: the compressor, the de-esser and the output limiter's helper.
 * The de-esser finds the sibilance around 6-9 kHz and takes it off the voice. */
void vox_dyn_process(vox_dyn_t *d, float *x, uint32_t n, float thresh_db, float ratio, float makeup,
                     float deess_amount);

/* ------------------------------------------------------------------ the effects --- */

/* the effect types (the four slots hold these) */
enum {
    VOX_FX_NONE = 0,
    /* the dynamics */
    VOX_FX_GATE,
    VOX_FX_COMP,
    VOX_FX_LIMITER,
    VOX_FX_DEESSER,
    VOX_FX_EQ,
    /* the character */
    VOX_FX_SATURATE,
    VOX_FX_OVERDRIVE,
    VOX_FX_FUZZ,
    VOX_FX_BITCRUSH,
    VOX_FX_TELEPHONE,
    VOX_FX_RADIO,
    VOX_FX_VOCAL_DIST,
    /* the modulation */
    VOX_FX_CHORUS,
    VOX_FX_FLANGER,
    VOX_FX_PHASER,
    VOX_FX_TREMOLO,
    VOX_FX_RING,
    VOX_FX_DOUBLER,
    /* the space */
    VOX_FX_DELAY,
    VOX_FX_TAPE_DELAY,
    VOX_FX_PINGPONG,
    VOX_FX_STEREO_DELAY,
    VOX_FX_REVERB,
    VOX_FX_SHIMMER,
    /* the experimental */
    VOX_FX_FORMANT_FILTER,
    VOX_FX_ROBOTIC,
    VOX_FX_PITCH_GLITCH,
    VOX_FX_STUTTER,
    VOX_FX_FREEZE,
    VOX_FX_SPECTRAL,
    VOX_FX_VOCODER,
    VOX_FX_COUNT
};

/* an effect slot */
typedef struct {
    uint8_t type;                      /* VOX_FX_* */
    uint8_t on;                        /* 0 = the slot is bypassed */
    uint8_t p[3];                      /* the three parameters, 0..100 */
    uint8_t mix;                       /* the wet against the dry, 0..100 */
} vox_fxslot_t;

/* the effect chain's state: the four slots and their lines */
typedef struct {
    int16_t *delay_l, *delay_r;        /* the caller's delay lines */
    int16_t *reverb_l, *reverb_r;      /* the caller's reverb lines */
    uint32_t delay_pos;
    uint32_t reverb_pos;
    float lfo;
    uint32_t seed;
    float dry_l[VOX_BLOCK];            /* the dry copy, for the wet/dry mix */
    float dry_r[VOX_BLOCK];
    float hold[VOX_BLOCK];             /* the freeze's held block */
    float held[VOX_BLOCK];
    uint8_t held_len;
    uint8_t hold_pos;
    uint32_t glitch_until;
    uint32_t stutter_count;
} vox_fx_state_t;

/* the effect chain's state, with the caller's lines */
void vox_fx_init(vox_fx_state_t *fx, const vox_buffers_t *bufs);

/* the effect's name, for the display: "DISTORTION" */
const char *vox_fx_name(int type);

/* the effect's three parameter names: vox_fx_param_name writes them into out[3][12] */
void vox_fx_param_name(int type, char out[3][12]);

/* the chain: the four slots in order, each over the previous one's output.
 * l and r are the stereo block (n frames). The effect's feedback (the delay, the reverb) is kept in the
 * state. The chain must not allocate and must not read flash. */
void vox_fx_process(vox_fx_state_t *fx, const vox_fxslot_t *slots, float *l, float *r, uint32_t n,
                    uint32_t now_ms);

/* one effect over a stereo block, for a single slot */
void vox_fx_one(vox_fx_state_t *fx, int type, const uint8_t p[3], uint8_t mix, float *l, float *r,
                uint32_t n, uint32_t now_ms);

/* ------------------------------------------------------------------ the mixer --- */

/* the mixer's state */
typedef struct {
    float dry_l[VOX_BLOCK], dry_r[VOX_BLOCK];
    float wet_l[VOX_BLOCK], wet_r[VOX_BLOCK];
} vox_mix_t;

void vox_mix_init(vox_mix_t *m);

/* the mixer: the voices are summed into the wet bus (what the effect chain works on), and the original
 * signal goes into the dry bus (what the correction is measured against). */
void vox_mix_voice(vox_mix_t *m, uint32_t voice, const float *x, uint32_t n, float level, float pan,
                   float width);

/* the dry bus: the original signal */
void vox_mix_dry(vox_mix_t *m, const float *x, uint32_t n, float level);

/* the mixer's output: the dry and the wet are mixed into l and r */
void vox_mix_out(vox_mix_t *m, float *l, float *r, uint32_t n, float dry, float wet, uint32_t now_ms);

/* ------------------------------------------------------------------ the limiter --- */

typedef struct {
    float env;
    float gain;
    float atk, rel;
    float peak;
} vox_limit_t;

void vox_limit_init(vox_limit_t *l);

/* the output limiter: a fast peak limiter with a lookahead-free gain reduction */
void vox_limit_process(vox_limit_t *l, float *x, uint32_t n, float ceiling);

/* ------------------------------------------------------------------ the engine --- */

/* a whole preset: every parameter of the instrument */
typedef struct {
    uint32_t magic;                    /* VOX_MAGIC */
    uint8_t version;
    uint8_t mode;                      /* 0 = MULTIVOICE, 1 = GLIDE */
    /* the input */
    uint8_t in_gain;                   /* 0..100 */
    uint8_t in_gate;                   /* 0..100 */
    /* GLIDE */
    uint8_t retune, glide, humanize, flex, mix;
    int8_t octave;                     /* -2..+2 */
    /* MULTIVOICE */
    uint8_t voices;                    /* 1..VOX_MAX_VOICES */
    uint8_t spread, detune, character, harmony;
    uint8_t chaos;
    /* the scale */
    vox_scale_t scale;
    /* the formant */
    int8_t formant;                    /* -12..+12 */
    uint8_t formant_amt;               /* 0..100 */
    /* the voices */
    vox_voice_param_t voice[VOX_MAX_VOICES];
    /* the effects */
    vox_fxslot_t fx[VOX_FX_SLOTS];
} vox_preset_t;

#define VOX_MAGIC 0x304F5856u           /* "VXO0" */
#define VOX_PRESET_VERSION 1u

/* the engine's state: every module's state and the working buffers */
typedef struct {
    vox_preset_t set;                  /* the parameters */
    vox_buffers_t bufs;                /* the caller's big lines */
    vox_input_t input;
    vox_detect_t detect;
    vox_correct_t correct;
    vox_harm_t harm;
    vox_voice_t voice[VOX_MAX_VOICES];
    vox_dyn_t dyn;
    vox_fx_state_t fx;
    vox_mix_t mix;
    vox_limit_t limit;
    vox_limit_t limit_r;
    /* the working buffers */
    float block[VOX_BLOCK];            /* the conditioned input */
    float target[VOX_BLOCK];           /* the corrector's per-sample target */
    float l[VOX_BLOCK], r[VOX_BLOCK];
    /* what the display reads */
    vox_pitch_t pitch;                 /* the detected pitch */
    float target_f0;                   /* the corrected pitch, Hz */
    float level;                       /* the input's level */
    float breathe;                     /* the phase of the idle breathing, 0..1 */
    uint32_t now_ms;
    uint32_t seed;
    uint8_t dirty;                     /* the parameters changed */
} vox_engine_t;

/* the engine's defaults (the "CLEAN STUDIO" preset) */
void vox_engine_defaults(vox_engine_t *e);

/* the engine's reset: the detectors, the shifters and the effects. The buffers are the caller's. */
void vox_engine_init(vox_engine_t *e, const vox_buffers_t *bufs);

/* the engine's block: the whole chain for n frames.
 * The input is in `in` (mono, n samples); the output is in e->l and e->r (n frames).
 * The engine does the pitch detection, the correction, the voices, the mixer, the effects and the limiter. */
void vox_engine_process(vox_engine_t *e, const float *in, uint32_t n, uint32_t now_ms);

/* the engine's note names, for the display */
const char *vox_note_name(int note, char *out);

/* the factory presets' names, for the display: "ROBOT BASTARD", ... (app/presets/factory_presets.c) */
const char *vox_factory_name(uint32_t index);
