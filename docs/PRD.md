# ANATOMIX VOXINATOR — Product Requirements

**M-VAVE FM1 Vocal Processing Firmware — v1.0**

> This is the product's requirements, as written, with the platform's findings folded in. What the FM-1's
> hardware actually allows is marked **[PLATFORM]**; those are not changes of intent, they are the facts the
> requirements have to be built on.

| Property | Value |
|---|---|
| Project | ANATOMIX VOXINATOR |
| Hardware | M-VAVE FM1 |
| Firmware base | fm1-base |
| Language | C |
| Audio | USB microphone input |
| Display | 240 × 240 colour TFT |
| Audio rate | 44.1 kHz target |
| Primary modes | MULTIVOICE / GLIDE |
| DSP architecture | Modular real-time processing |
| UI identity | Animated anatomical vocal simulator |
| Presets | 16 factory + 16 user |

---

## 0. The platform's findings (read this first)

**[PLATFORM]** Three things about the FM-1 change how §2 is read. The evidence is in
[USB_AUDIO.md](USB_AUDIO.md).

1. **The FM-1's USB port does not supply VBUS.** A bus-powered USB microphone plugged straight into the
   pedal will not work. The instrument needs a **powered USB hub** between the microphone and the FM-1.
   This is the platform's own position for its USB-MIDI host, and it is not a software matter.
2. **The AC791N's USB controller can act as a host** and the SIE already speaks isochronous endpoints, so a
   USB Audio Class microphone is feasible — but the host driver that ships with the platform is MIDI-only,
   so the isochronous path had to be written. It is done and model-tested; the silicon is not yet verified.
3. **The platform has no audio-input path at all.** There is no microphone on the FM-1, and the only
   inbound audio (the computer's USB audio) is mixed into the speaker, never handed to the app. So the
   microphone's capture is a platform-layer piece of work, not an app one — it now exists behind
   `plat_mic_read()`.

Two smaller corrections, for the record:

* §8.2 lists `ALGORITHM`, `PRESETS`, `SELECT` and `K1`-`K4` among the physical controls. On the real FM-1
  they are **encoders**, not buttons: the ALGO and the PRESET are rotary, and K1-K4 are the four
  contextual knobs.
* The `B_*` names in the platform are `B_FX`, `B_SEL`, `B_ENV`, `B_LFO`, `B_EDIT`, `B_GLO`, `B_HOME`,
  `B_SAVE`, `B_ARP`, `B_SEQ`, `B_PLAY`, `B_REC`, `B_OCTDN`, `B_OCTUP` — fourteen, as the requirement says.

---

## 1. Product Vision

ANATOMIX VOXINATOR transforms the M-VAVE FM1 into a real-time vocal processing instrument.

It combines:

- Auto-Tune-style pitch correction.
- Polyphonic vocal harmonization.
- Monophonic pitch shifting.
- Formant manipulation.
- Vocal tract simulation.
- Robotic and synthetic voice generation.
- Professional vocal processing effects.
- Experimental vocal sound design.

The central design concept is an animated anatomical visualization of the human vocal system.

Instead of conventional synthesizer graphics, the interface displays a schematic human head and throat. The
user can see the vocal system reacting to their voice and to every parameter they change.

For example:

- Increasing pitch makes the vocal folds vibrate faster.
- Increasing formant shift changes the simulated vocal tract shape.
- Increasing nasal resonance activates the nasal cavity.
- Increasing distortion visually roughens the vocal waveform.
- Adding harmonies creates additional vocal paths.
- Increasing compression visually squeezes the lungs and airflow representation.

The UI should be educational, slightly grotesque, humorous and visually memorable.

**Design philosophy: A medical anatomy laboratory meets an experimental electronic music synthesizer.**

## 2. Hardware and Platform

### 2.1 Target hardware

The firmware must be built on fm1-base and follow the same architectural principles as CHOPPAH.

Hardware characteristics:

- JieLi AC791N / WL82 platform.
- 44.1 kHz audio rendering.
- 256-frame audio blocks.
- 240 × 240 colour display.
- Seven rotary encoders.
- 14 physical buttons.
- 16 white keys.
- 11 black keys.
- USB connectivity.
- Existing fm1-base persistent storage.
- Host simulator and browser emulator.

The available processing and memory budgets must be verified against the actual target build before finalizing
the DSP algorithms. **[PLATFORM]** The budgets: 96 KiB of RAM for the app and 336 KiB of RAM pool for the big
buffers; the render's budget is the audio block's own 5.8 ms. The engine's state and the big lines are laid
out against those numbers in [DSP_ARCHITECTURE.md](DSP_ARCHITECTURE.md).

### 2.2 USB microphone input

USB microphone input is a mandatory feature.

The device must receive audio from an external USB microphone connected to the FM1.

**Important hardware requirement:** The existing FM1 USB audio recording capability is not equivalent to USB
microphone host support.

Before implementing the complete audio engine, verify:

1. Whether the AC791N USB controller supports the required USB host functionality. → **Yes** (§0.1).
2. Whether the FM1 USB connector exposes the required host-mode hardware. → **Yes**, the same connector.
3. Whether USB Audio Class microphones can be enumerated. → **The code exists and is model-tested; the
   silicon is unverified.**
4. Whether microphone power can be supplied. → **No. A powered hub is required.**
5. Whether isochronous USB audio transfers can be received reliably. → **Implemented; the silicon is
   untested.**
6. Whether the firmware can process incoming PCM without disrupting the existing audio engine. → **Yes, by
   construction** (a ring buffer, never a wait).

If the existing USB hardware cannot support host-mode microphones, investigate a compatible external USB
host solution. Do not silently substitute a different input architecture. → **The hardware does support the
host mode, so no substitution is needed. The power is the gap, and the answer is a powered hub, not a
different input architecture.**

### USB audio requirements

- USB Audio Class microphone support.
- 44.1 kHz preferred.
- 48 kHz where supported. → **Supported: the capture reports the device's rate and the app converts.**
- Mono and stereo input.
- 16-bit and 24-bit PCM where supported.
- Input level monitoring.
- Input gain control.
- USB connection status.
- Device enumeration feedback.
- Disconnect/reconnect handling.
- Buffer underrun protection.

### Input routing

```text
USB MICROPHONE
      |
      v
USB HOST / UAC CAPTURE
      |
      v
PCM INPUT BUFFER
      |
      v
SAMPLE RATE / FORMAT CONVERSION
      |
      v
INPUT GAIN
      |
      v
VOCAL PROCESSING ENGINE
      |
      v
MASTER MIXER
      |
      v
FM1 AUDIO OUTPUT
```

USB audio output to a computer should remain an optional additional feature, separate from microphone
capture.

## 3. Main Operating Modes

### 3.1 MULTIVOICE

Purpose: Create multiple simultaneous versions of the incoming vocal.

Features: the original vocal, the pitch-shifted harmonies, the octave doubling, the scale-aware harmony,
the detuned voices, the formant-shifted voices, the stereo voice positioning, the independent voice volume
and the independent effect sends.

#### Voice architecture

Initial target: Three processed voices plus the original.

| Voice | Default pitch | Character |
|---|---:|---|
| Voice 1 | 0 | Original / tuned lead |
| Voice 2 | +3 | Upper harmony |
| Voice 3 | +7 | Fifth harmony |
| Voice 4 | -12 | Lower octave |

Each voice has: enable/bypass, pitch offset, fine tuning, formant offset, volume, pan, stereo width,
detune and effect send.

#### Multivoice macro controls

| Parameter | Description |
|---|---|
| VOICE COUNT | Number of active voices |
| SPREAD | Stereo separation |
| DETUNE | Pitch differences between voices |
| HUMANIZE | Small pitch and timing variations |
| CHAOS | Randomized voice behaviour |
| CHARACTER | Natural to robotic |
| HARMONY | Scale-aware harmony selection |

Harmony configuration must support musical key and scale selection.

### 3.2 GLIDE

Purpose: Real-time monophonic pitch correction and exaggerated vocal pitch movement.

The engine detects the fundamental frequency and moves the vocal signal towards the closest permitted musical
note.

#### Pitch correction controls

| Parameter | Range |
|---|---|
| KEY | C–B |
| SCALE | Chromatic, major, minor, pentatonic, custom |
| RETUNE | 0–100 |
| GLIDE | 0–100 |
| HUMANIZE | 0–100 |
| FLEX | 0–100 |
| FORMANT | -12 to +12 |
| OCTAVE | -2 to +2 |
| MIX | 0–100 |

#### Behaviour

**RETUNE** — Controls how quickly the voice snaps to the target pitch: 0 natural, 25 subtle, 50 noticeable,
75 aggressive, 100 instant robotic tuning.

**GLIDE** — Controls the transition between target notes: 0 instant, 25 slightly softened, 50 smooth
portamento, 75 exaggerated sliding, 100 extreme continuous pitch gliding.

**HUMANIZE** — Introduces controlled natural variation into sustained notes.

**FLEX** — Controls how much of the original pitch variation survives correction.

The pitch detector must be designed to handle vibrato, noisy inputs, breath sounds and transitions between
notes.

## 4. ANATOMIX Anatomical Visualization Engine

This is the defining feature of the product. The main screen must show a stylized anatomical cutaway of the
human head and upper torso.

### 4.1 Anatomical components

| Component | Function represented |
|---|---|
| Lungs | Breath and airflow |
| Diaphragm | Breath pressure |
| Trachea | Airflow path |
| Larynx | Vocal production |
| Vocal folds | Fundamental frequency |
| Pharynx | Vocal resonance |
| Tongue | Articulation |
| Oral cavity | Mouth resonance |
| Nasal cavity | Nasal resonance |
| Lips | Mouth opening and articulation |

The drawing should be anatomically recognizable, but stylized for a tiny display. Use a side-profile
cross-section of the human head and throat, with a simplified transparent torso.

### 4.2 Anatomical animation behaviour

| Audio parameter | Visual behaviour |
|---|---|
| Input amplitude | Lungs expand and contract |
| Breath level | Airflow particles move through trachea |
| Fundamental frequency | Vocal folds vibrate faster/slower |
| Pitch correction | Vocal folds snap to target vibration |
| Glide | Vocal folds gradually change vibration |
| Formant shift | Vocal tract shape changes |
| Nasal resonance | Nasal cavity illuminates |
| Compression | Airflow dynamics visually compress |
| Distortion | Waveform becomes jagged |
| Delay | Repeated airflow echoes appear |
| Reverb | Anatomical outline develops resonance trails |
| Harmony | Additional vocal paths appear |

These visualizations are illustrative simulations, not literal anatomical measurements.

### 4.3 Anatomical visual style

Dark medical laboratory background. Warm anatomical pink, red and coral. Bone/off-white outlines. Amber and
cyan DSP indicators. Thin technical annotation lines. Animated airflow particles. Glowing vocal folds. Small
diagnostic readouts. Slightly grotesque but playful anatomy.

The overall appearance should resemble an animated medical textbook crossed with an experimental synthesizer.

Avoid conventional cartoon characters. The anatomy itself is the character.

### 4.4 Main anatomical screen

The diagram should animate continuously, even when no audio is being processed. When the microphone is
disconnected, the anatomy enters an idle breathing state.

## 5. Anatomical Simulation and Parameter Mapping

### 5.1 Vocal fold simulation

Two simplified vocal fold structures, with open/closed movement, vibration speed, colour intensity, a pitch
indicator and a target note indicator. When the user sings, the folds vibrate according to the detected
fundamental frequency. When pitch correction is active, the animation must distinguish between the detected
pitch and the corrected pitch.

### 5.2 Formant simulation

Formants control the simulated vocal tract: the pharynx's length, the oral cavity, the tongue's position and
the mouth's opening. Formant shift must have an obvious visual effect — a high formant a shorter, brighter
tract; a low formant a longer, darker one.

### 5.3 Airflow simulation

Animated particles travel LUNGS → TRACHEA → LARYNX → PHARYNX → MOUTH / NASAL CAVITY. Their speed responds to
the input's amplitude and the airflow pulses with the vocal signal.

### 5.4 Multivoice anatomical simulation

In MULTIVOICE mode, additional vocal fold pairs appear as separate animated processing paths, each with its
own pitch indicator, resonance colour, volume representation and animated fold movement. The original voice
remains the central anatomical structure; the additional voices appear as smaller ghosted anatomical
overlays.

## 6. DSP Architecture

### 6.1 Audio processing pipeline

```text
USB MICROPHONE -> INPUT BUFFER -> INPUT PROCESSING (gain, gate, high-pass)
   -> PITCH DETECTION -> PITCH ENGINE -> { MULTIVOICE | GLIDE }
   -> FORMANT ENGINE -> VOCAL CHARACTER -> EFFECTS CHAIN -> MASTER MIXER
   -> OUTPUT LIMITER -> FM1 AUDIO OUTPUT
```

The two main modes share pitch detection but have separate processing engines.

### 6.2 DSP modules

```text
dsp/
├── vox_input.c
├── vox_pitch_detect.c
├── vox_pitch_correct.c
├── vox_pitch_shift.c
├── vox_formant.c
├── vox_harmonizer.c
├── vox_glide.c
├── vox_voice.c
├── vox_dynamics.c
├── vox_fx.c
├── vox_mixer.c
└── vox_limiter.c
```

**Pitch detection** — investigate lightweight algorithms (autocorrelation, YIN, a hybrid time-domain
method). Requirements: the fundamental's estimation, a confidence measure, voiced/unvoiced detection, stable
note transitions, pitch smoothing and low latency.

**Pitch shifting** — investigate time-domain shifting, PSOLA, granular shifting and lightweight phase
vocoders; benchmark audio quality, CPU, memory and latency on the actual FM1.

**Formant processing** — investigate LPC or spectral-envelope-based formant shifting. Pitch shifting and
formant shifting must remain independent controls.

## 7. Vocal Effects Library

Four reorderable effect slots.

**7.1 Dynamics:** noise gate, compressor, limiter, de-esser, input EQ.
**7.2 Character:** saturation, overdrive, fuzz, bitcrusher, telephone, radio, vocal distortion.
**7.3 Modulation:** chorus, flanger, phaser, tremolo, ring modulation, vocal doubler.
**7.4 Space:** slapback delay, tape delay, ping-pong delay, stereo delay, reverb, shimmer.
**7.5 Experimental:** formant filter, robotic modulation, pitch glitch, granular stutter, vocal freeze,
spectral degradation, vocoder-style coloration.

Each effect must have: on/off, three or four main parameters, wet/dry, preset persistence and animated
anatomical feedback.

## 8. Interface and Navigation

### 8.1 Main screens

HOME (the anatomical overview), MULTIVOICE, GLIDE, FORMANT, EFFECTS, PRESETS, INPUT, GLOBAL.

### 8.2 Physical controls

See §0 for the encoder correction. The mapping the app implements:

| Control | Assignment |
|---|---|
| HOME | Anatomical overview |
| ALGO (encoder) | MULTIVOICE / GLIDE |
| PRESET (encoder) | Preset browser |
| FX | Effects chain |
| EDIT | Pitch and formant editing |
| ENV | Dynamics |
| LFO | Modulation |
| GLO | Global settings |
| SAVE | Save preset |
| PLAY | Effect bypass / freeze |
| REC | Optional vocal capture |
| SEQ | Harmony configuration |
| ARP | Experimental vocal effects |
| OCT− / OCT+ | Pitch shift |
| SELECT | Navigation |
| K1–K4 | Contextual parameters |
| MASTER | Output volume |

### 8.3–8.8 The screens

**HOME** — the complete anatomical visualization. K1 voice count, K2 spread, K3 wet/dry, K4 effect macro.

**MULTIVOICE** — the vocal paths and the independent voices. K1 voice count, K2 harmony, K3 detune, K4 mix.

**GLIDE** — the vocal folds, the detected pitch and the corrected pitch. K1 retune, K2 glide, K3 humanize,
K4 mix.

**FORMANT** — an enlarged anatomical head and vocal tract. K1 formant shift, K2 throat, K3 nasal, K4 preserve.

**EFFECTS** — the four effect slots, the focused slot's three parameters and MIX, and MOVE on K4.

**PRESETS** — the browser: BROWSE, CATEGORY, LOAD, SAVE.

## 9. Preset System

16 factory presets and 16 user presets. Save, Save As, Rename, Load, Copy, Initialize, A/B comparison,
last-used preset restoration and factory preset protection. The preset data includes all processing
parameters and the effect routing. The global settings stay separate. The existing fm1-base storage
conventions and torn-write-safe persistence mechanisms are used.

The factory presets: Robot Bastard, Tiny Rat, Demon, Alien Choir, Rotterdam 1995, Cherub, Telephone, Huge
Hall, UK Garage, Nightclub, Evil Twin, Synthetic, Liquid, Radio Monster, Broken Robot, Clean Studio.

## 10. Animation System

Animations communicate DSP behaviour rather than decorate.

**10.1 Breathing** — the lungs slowly expand and contract when idle; with audio, the animation follows the
input's envelope.
**10.2 Vocal folds** — they vibrate in response to the detected fundamental.
**10.3 Pitch correction** — the folds snap into their corrected position; the original and the corrected
pitch have separate indicators.
**10.4 Formant mutation** — the tract changes shape as the formant parameters change.
**10.5 Multivoice** — the additional paths appear as duplicated anatomical structures.
**10.6 Distortion** — the airflow becomes jagged, the outline pixelated.
**10.7 Delay** — repeated ghost outlines behind the original, fading with the feedback.
**10.8 Reverb** — the head develops expanding resonance rings.
**10.9 Clipping** — the input indicator turns red, the lungs inflate, "OVERLOAD" appears.
**10.10 Performance** — 15–30 FPS; the audio never depends on the animation; procedural graphics where
practical; compact sprites; no dynamic allocation in the audio callback; no expensive full-screen
rendering; graceful degradation under CPU pressure.

## 11. Technical Requirements

| Requirement | Target |
|---|---|
| Audio sample rate | 44.1 kHz |
| Audio block | 256 frames initially |
| Input | USB Audio Class microphone |
| Output | FM1 audio output |
| Pitch tracking | Real-time monophonic F0 |
| Voice processing | Three processed voices initially |
| Modes | Multivoice / Glide |
| Formant | Independent processing |
| Effects | Four reorderable slots |
| Presets | 16 factory + 16 user |
| Display | 240 × 240 |
| UI | 15–30 FPS |
| Monitoring latency | Target ≤20 ms end-to-end |
| Audio reliability | No clicks or buffer underruns |

All targets must be validated on physical hardware. The firmware should prioritize stable audio over visual
effects.

## 12. Repository Structure

See the repository tree in the README. The requirement's tree is realised with two adjustments: the DSP's
maths helpers live in `vox_math.h` beside the modules, and `app/presets/` holds the factory presets while the
user presets live in the flash.

## 13. Development Roadmap

**Phase 1 — USB microphone feasibility.** Verify the host support, the enumeration, the power, the PCM, the
continuous streaming, the output route and the latency. *Acceptance: a USB microphone produces continuous
audio on the FM1.* → **The firmware is done and model-tested; the hardware acceptance needs an FM-1 and a
powered hub.**

**Phase 2 — Vocal engine prototype.** The pitch detection, the detected notes, one pitch-shifted voice, the
scale correction, the hard auto-tune, the benchmarks. *Acceptance: stable real-time pitch correction.* →
**Done and tested on the computer; the benchmark on the silicon is pending.**

**Phase 3 — Multivoice and formant.** *Acceptance: three processed voices plus the original without audio
dropouts.* → **Done and tested; the hardware run is pending.**

**Phase 4 — Effects engine.** *Acceptance: multiple effects operate simultaneously within the processing
budget.* → **Done and tested.**

**Phase 5 — Anatomical UI.** *Acceptance: every major audio parameter has corresponding visual feedback.*
→ **Done.**

**Phase 6 — Presets and performance.** *Acceptance: a complete standalone vocal processing instrument.*
→ **The presets are done; the final optimisation and the MIDI control are pending.**

## 14. Initial Prototype: Version 0.1

1. USB microphone capture. 2. The input meter. 3. The anatomical head and throat visualization.
4. The animated breathing. 5. The vocal fold animation. 6. The fundamental frequency detection.
7. The detected note display. 8. The basic hard auto-tune. 9. One octave-up voice. 10. The dry/wet output.

Success means singing into the USB microphone and hearing a stable, pitch-corrected voice while watching the
anatomical visualization react in real time.

## 15. Product Definition

ANATOMIX VOXINATOR is not intended to be a conventional vocal effects pedal. It is a miniature vocal
anatomy laboratory and experimental vocal synthesizer.

The anatomical visualization is the central interface, not a separate decorative feature. Every important
parameter should have both an audible effect and a corresponding visual simulation.

**ANATOMIX VOXINATOR — Operate the human voice like a synthesizer.**
