# The DSP architecture

How a live voice becomes ANATOMIX VOXINATOR's output, module by module, and what each one costs.

The engine lives in `app/dsp/`. It is **platform-free**: it includes no `plat.h` and no hardware header, so
it compiles on its own with the computer's C compiler (`tests/dsp/run.sh`) and the same source runs on the
FM-1. The rules the whole engine obeys, and the platform's build enforces:

* **float only.** Never `double` — the device's compiler is single precision and the tests build with
  `-Wdouble-promotion -Wfloat-conversion -Werror` to keep it that way.
* **No `malloc`.** No allocator exists on the device. Every buffer is either in the engine's own state or
  owned by the app, which puts the big ones in the platform's RAM pool.
* **No flash reads, no blocking, no allocation** in anything the audio interrupt calls. A flash write stops
  the sound for a moment, so the presets are saved only when the user asks.
* **No libm.** `app/dsp/vox_math.h` has the approximations (`vox_exp2f`, `vox_log2f`, `vox_sin_turns`,
  `vox_tan_pi`) with their errors stated.

---

## The chain

```text
USB MICROPHONE
      |
      v
[platform] plat_mic_read()        the host's PCM, as the device sent it
      |
      v
[app] the format conversion       the device's rate and depth to 44.1 kHz float, mono
      |
      v
vox_input.c      the gain, the noise gate, the high-pass, the meter
      |
      +--> the level, the envelope, the overload flag
      |
      v
vox_pitch_detect.c    the fundamental, the clarity, the note
      |
      v
      +--> MULTIVOICE                GLIDE
      |    |                          |
      v    v                          v
      |  vox_harmonizer.c       vox_pitch_correct.c
      |    (the scale-aware        (the note's choice)
      |     intervals)               |
      |       |                      v
      |       |                 vox_glide.c  (the travel)
      |       |                      |
      |       +----------+-----------+
      |                  |
      v                  v
vox_voice.c  <---- the shifter + the formant filter, per voice
      |
      v
vox_mixer.c    the voices, the dry and the wet, the panning
      |
      v
vox_fx.c       the four slots in order
      |
      v
vox_limiter.c  the ceiling
      |
      v
the FM-1's output
```

The two modes share the detector and the formant engine; only the note's source differs.

---

## The modules

| Module | What it owns | What it does |
|---|---|---|
| `vox_input.c` | the gain, the gate, the high-pass, the meter | conditions the input before anything else sees it |
| `vox_pitch_detect.c` | the detector's ring and its smoothed state | the fundamental, the confidence, the note |
| `vox_pitch_correct.c` | the target, the note, the bend | snaps the detected pitch to the scale and hands the travel to the glide |
| `vox_glide.c` | the portamento's state | the curve between two notes |
| `vox_pitch_shift.c` | the analysis ring, the window | the PSOLA shifter: the pitch changes, the duration does not |
| `vox_formant.c` | the LPC coefficients, the residual | the formant shift, independent of the pitch |
| `vox_harmonizer.c` | the intervals, the spread, the chaos | the scale-aware harmony plan |
| `vox_voice.c` | the voice's state | a voice = the shifter + the formant filter |
| `vox_dynamics.c` | the compressor's and the de-esser's state | the gate's helper, the compressor, the de-esser |
| `vox_fx.c` | the delay and the reverb lines, the LFO | the four-slot chain |
| `vox_mixer.c` | the dry and the wet buffers | the voices, the panning, the width, the sends |
| `vox_limiter.c` | the envelope and the gain | the output ceiling |

---

## The decisions worth explaining

### The detector: a normalised difference function, not a plain autocorrelation

A plain autocorrelation of a 256-sample block is O(n²) per lag, and the useful lag range (65 Hz to 1200 Hz
at 44.1 kHz is 37 to 678 samples) makes it too slow for four voices' worth of budget. The detector uses a
cumulative-mean normalised difference function (the YIN idea): the normalisation is what makes it survive a
noisy room, and it gives the **clarity** for free — the ratio of the chosen lag's difference to the running
mean. The clarity is what the interface draws as "how voiced is this".

The detector keeps the previous block in a ring, so a note that changes at the block's edge is not mistaken
for a different note, and the pitch is smoothed across the blocks rather than jumping.

### The shifter: two clocks and a grain, because the duration must not move

The instrument's voices are harmonies of one voice. A harmony that changes the length is a harmony that
falls apart, so the shifter must change the pitch and keep the duration. That rules out plain resampling
(which changes both) and rules in the time-domain overlap-add methods:

* the input's pitch period comes from the detector;
* the grains sit **one source period apart in the output**, and each grain is **two periods long** with a
  Hann window, so the windows sum to one and the level does not change;
* a grain's **content is the input read at the ratio's rate**, so the input's period appears in the output
  as `period / ratio`: that is the pitch shift itself;
* the content's clock walks the input at the **input's own rate**, so the singer reaches the end of the
  input exactly when the output reaches the end of the grain: the duration is kept;
* before a grain is taken, the input is searched for the place whose head **best continues** the grain
  before it: that is the alignment (SOLA), and it is what keeps the carrier's phase locked.

Two clocks, and they must not be confused:

| clock | what it walks | what it decides |
|---|---|---|
| the output's (`src`) | the output's samples | where the grains land (one per source period) |
| the content's (`ana`) | the input's samples | which part of the input each grain carries |

The content's **step** is the part that needs care. Two grains overlap and their carrier has to add
instead of cancelling, so the content's step must be a whole number of source periods away from the step
the output's spacing asks for. If the ratio is a whole number the two agree and the step is simply one
period. If the ratio is not (a third, a fifth), the step is a period times the ratio's fraction *plus* a
whole number of periods, and a fractional accumulator decides how often the long step is taken — so that
the steps **average one period**: the duration is kept and the carrier's phase stays locked.

The alignment searches a window of ±0.2 period around the nominal place and scores the candidates by a
**difference function** (the sum of the squared differences against the continuing grain), not by
correlation: correlation prefers the loudest place, which is not the place that continues the wave.

The output's clock runs **one period ahead** of the read pointer, so a grain's leading half is never
dropped at the block's edge. The grains reach over the block's edges, so the sum is kept in an accumulator
that the block reads and clears: a grain placed in one block is still adding to the next block's samples.

The result is the same length at the new pitch, with the formants left where they were — which is exactly
what a harmony needs, and why the formant shifter is a separate stage.

The test suite sweeps all 25 semitone ratios and checks that the output lands on the wanted pitch, that the
length is kept and that the spectrum has no splatter: `tests/dsp/test_chain.c`.

### The formants: LPC, and deliberately independent of the pitch

The voice's size is its formants, not its pitch. A child and an adult sing the same note at the same pitch
and sound completely different, so the formant shift must be a separate control that the pitch shifter
cannot touch.

The engine estimates the LPC of the block (order 12 — enough for F5 at 5 kHz), keeps the residual (the
excitation), shifts the formants by the requested number of semitones (a frequency ratio of 2^(n/12)) and
resynthesises with the shifted filter and the original excitation. The pitch is untouched by this, and the
pitch shifter is untouched by it. The test suite checks exactly this: a +12 formant shift must not move the
detected pitch.

### The corrector and the glide: two jobs, two modules

The corrector decides *which* note the voice should be on (the scale, the octave, the humanise, the flex).
The glide decides *how* it gets there (the portamento's shape and its time).

`RETUNE` is the speed of arrival; `GLIDE` is the time the travel itself takes. At `GLIDE 0` a new note is
instant. At `GLIDE 100` the voice keeps drifting — an extreme continuous glide that never quite arrives.
`HUMANIZE` adds a slow random walk of a few cents to a held note. `FLEX` decides how much of the singer's
own vibrato survives the correction: at 0 the output is dead on the note, at 100 the vibrato is left alone.

An unvoiced input never snaps: the corrector returns the input's own pitch and clears the note.

### The harmony: the scale decides, not the interval table

The harmoniser's plan is scale-aware. The chosen intervals are checked against the scale
(`vox_scale_has`), so the same "third" is the minor third in C minor and the major third in C major. The
lead is always voice 0; the others follow the plan for the chosen harmony index. `SPREAD`, `DETUNE`,
`CHARACTER`, `HUMANIZE` and `CHAOS` shape the plan; `CHAOS` is deterministic for a seed, so a preset
replays the same way.

### The effects: four slots, each with a wet/dry

The chain is four slots in order, each over the previous one's output. The types are grouped as the PRD
groups them — dynamics, character, modulation, space, experimental — and each effect has three parameters
and a wet/dry mix.

The delay and the reverb lines are the only big buffers in the engine, and they belong to the **app**, not
to the DSP: the app puts them in the platform's RAM pool and hands the pointers over
(`vox_buffers_t`). The DSP stays platform-free and never allocates. The same goes for the shifters' rings
and accumulators: four voices with a ring and an accumulator each would be 96 KiB of the engine's own
state, so they live in the pool too and the engine's state shrank from 104,848 bytes to 28,200.

### The memory

The app's own buffers, and where they live:

| What | Where | How much |
|---|---|---|
| the engine's state (the detectors, the four voices, the working buffers) | `.bss` (the app's RAM) | 28,200 B |
| the app's own state (the settings, the user presets, the interface's scratch) | `.bss` | ~2 KiB |
| the effect's delay lines (2 × 16384 samples, 16-bit) | the pool | 64 KiB |
| the effect's reverb lines (2 × 8192 samples, 16-bit) | the pool | 32 KiB |
| the shifters' analysis rings (4 × 2048 samples, float) | the pool | 32 KiB |
| the shifters' overlap-add accumulators (4 × 2048 samples, float) | the pool | 32 KiB |
| the microphone's jitter buffer (2 × 1024 frames, 32-bit) | the pool | 8 KiB |
| **the app's pool total** | | **168 KiB** |

The base's own use of the same pool: the canvas (240 × 176 RGB565) 84 KiB, the capture's ring 4 KiB, the
screen mirror 9 KiB, the backup's buffer 4 KiB and the words 2.5 KiB — **102 KiB**.

| | |
|---|---|
| the pool's total | 270 KiB |
| the pool's region (the FM-1's `POOL`) | 336 KiB |
| **the headroom** | **66 KiB** (`tools/build.py` insists on 8 KiB) |

So the pool fits, with room to grow: the delay lines are 372 ms deep and the reverb's 186 ms, which is far
more than a pedal needs, and both can be halved if something else ever needs the room.

> **This is measured, not built.** The JieLi SDK and the toolchain are not on this machine, so the device
> build could not be run and the linker could not confirm the numbers. The figures come from the
> declarations (the buffers' sizes are fixed by their types) and from `nm` over the host build's unity
> unit, which has the same layout as the device's. **The first thing to do on a machine with the SDK is
> to run the device build and read `tools/build.py`'s own line: "RAM .data+.bss … of …; pool … of …".**

The device's RAM is 96 KiB and the pool is 336 KiB, so the big buffers are deliberately in the pool and the
engine's own state is deliberately small.

---

## The budget

The render's budget is the audio block's own period: 256 frames at 44.1 kHz is 5.8 ms, and the platform
calls `app_overload()` when the render passes 90 % of it. The engine's cost is dominated by the shifter
(the only per-voice, per-sample work) and by the effect chain.

The rules that keep it inside the budget:

* the shifter is O(n) per block, with a bounded local search for the pitch mark;
* the detector runs once per block, not once per voice;
* the effect chain's expensive effects (the reverb, the delay) work on the stereo block once, not per
  voice;
* nothing in the render allocates, reads the flash or blocks.

The platform reports the load as `plat_cpu_pct()` and the xruns as `plat_xruns()`; the GLOBAL screen shows
them, and a voice that is too expensive shows up there before it shows up as a click.

---

## Testing it

`tests/dsp/run.sh` builds `app/dsp/` on its own with the computer's compiler and runs two programs:

* `tests/dsp/test_pitch.c` — the detector (a sine at four pitches, a vibrato'd note, white noise, a note
  change inside the analysis region), the corrector (the snap, the scale, the unvoiced case), the glide (the
  time it takes), the harmoniser (the scale awareness), and the pitch/formant independence;
* `tests/dsp/test_chain.c` — the input's gate, the shifter (the pitch it lands on, the length it keeps),
  every effect type (finite output, a sane peak), the delay's bounded feedback, the limiter's ceiling and
  the mixer's panning.

The harness is four lines long — there is no framework:

```c
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)
```

and the program exits non-zero if a check failed.
