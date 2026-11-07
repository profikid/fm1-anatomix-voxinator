# ANATOMIX VOXINATOR

**A miniature vocal anatomy laboratory and experimental vocal synthesizer for the M-VAVE FM-1.**

It turns the FM-1 into a real-time vocal processing instrument: a USB microphone goes in, and a corrected,
harmonised, formant-shifted and effected voice comes out of the pedal — while the screen shows a stylised
cross-section of a human head and throat that reacts to everything you do.

> *Operate the human voice like a synthesizer.*

---

## What it is

| | |
|---|---|
| Hardware | M-VAVE FM-1 (JieLi AC791N / WL82) |
| Input | a USB Audio Class microphone (a **powered hub** is required — see below) |
| Output | the FM-1's own audio output |
| Modes | **MULTIVOICE** (the harmonies) and **GLIDE** (the auto-tune) |
| Display | 240 × 240 colour TFT |
| Presets | 16 factory + 16 user |
| Interface | an animated anatomical cutaway, not a synthesizer graphic |

The anatomy *is* the interface. Raising the pitch makes the vocal folds vibrate faster; raising the formant
shift shortens and brightens the simulated vocal tract; the harmonies appear as ghosted anatomical
overlays; the distortion makes the airflow jagged; the delay leaves ghost outlines of the throat; the
reverb sends resonance rings out from the head. Every parameter has both an audible effect and a visible
one.

## The honest status

| Phase | State |
|---|---|
| **1 — USB microphone** | the firmware work is **done and tested against the platform's SIE model**; the silicon is **not yet verified**. **The FM-1's USB port drives no VBUS, so a powered hub is required** — see [docs/USB_AUDIO.md](docs/USB_AUDIO.md). |
| **2 — the vocal engine** | the pitch detector, the corrector, the glide and the shifter are implemented and tested on this machine. |
| **3 — multivoice and formant** | the harmoniser, the voice engine and the formant shifter are implemented and tested. |
| **4 — the effects** | the four-slot chain, all the effect types, implemented and tested. |
| **5 — the anatomical UI** | implemented: the eight screens, the animation engine. |
| **6 — the presets** | the 16 factory presets, the user presets and the storage are in. |

What has **not** happened yet: the firmware has never been flashed, because this machine has no JieLi
toolchain and no SDK. Everything that can be built and run without them — the whole DSP, the whole
interface, the host simulator with its screenshots — is built and run here.

## Building it

```sh
git clone --recursive https://github.com/profikid/fm1-anatomix-voxinator
cd fm1-anatomix-voxinator
./scripts/bootstrap.sh       # the platform: base/ and its own submodule

./build.sh test              # the DSP's tests: the computer's own compiler, no toolchain
./build.sh audio             # the microphone path in the simulator: the tones and the checks
./build.sh --host            # the host simulator: app/build/host/anatomix_host
./build.sh shots             # the screens, as PNGs in app/build/shots/
./build.sh                   # the firmware package: app/build/anatomix.fwsc (needs the SDK)
./build.sh flash             # install it on a connected FM-1
```

The **host simulator** runs the whole firmware on the computer: the screen, the buttons, the encoders, the
keys, the storage, the audio. It is how the interface was developed, and how it is tested:

```sh
./build.sh --host
app/build/host/anatomix_host app/tests/screens.txt app/build/shots
```

## The platform, and the two changes it needs

The microphone capture is **the platform's** work, not the app's: the FM-1 is the USB *host*, the capture
lives in the SIE driver, and the app reads it through `plat_mic_*`. Those changes are in **fm1-base**, in
the branch `feature/usb-mic-capture`:

| file | what |
|---|---|
| `device/usb_mic.c` | the capture: the UAC descriptor walk, the isochronous IN endpoint, the ring, the session's states |
| `include/plat.h` | the `plat_mic_*` API the app talks to |
| `device/plat_fm1.c`, `device/main_fm1.c`, `device/unity.c` | the wiring |
| `host/plat_host.c` | the simulator's side of the same API (a WAV file as the capture) |
| `tests/usb_mic_test.c` | the test: the production capture against a modelled SIE |
| `docs/USB_AUDIO.md` | the platform's own document on the host port, the power and the endurance |
| `tools/gen_font.py` | a portability fix: the font generator falls back when Pillow has no libraqm |

The app's side of the contract — the jitter buffer, the rate conversion, the engine, the screens — is in
this repository and needs nothing from the platform but those six functions.

## The memory

| | |
|---|---|
| the engine's state | 28,200 B of the app's 96 KiB of RAM |
| the app's pool (the delay, the reverb, the shifters' rings, the jitter buffer) | 168 KiB |
| the base's own pool (the canvas, the capture's ring, the mirror, the words) | 102 KiB |
| **the pool's total** | **270 KiB of 336 KiB** (66 KiB spare) |

The numbers are from the declarations and from `nm` over the host build's unity unit: the device build
could not be run here (no SDK, no toolchain), so `tools/build.py`'s own line
("RAM .data+.bss … of …; pool … of …") is still to be read on a machine that has them.

## The repository

```text
fm1-anatomix-voxinator/
├── base/                    fm1-base, the platform layer (a submodule)
├── app/
│   ├── app.c                the lifecycle, the capture, the presets, the wiring
│   ├── app.h                the contract with the platform
│   ├── app_state.h          the state the render and the frame share
│   ├── fm1.toml             the build's settings
│   ├── dsp/                 the engine: platform-free, float only, no malloc
│   │   ├── vox.h            the engine's API and the preset's format
│   │   ├── vox_math.h       the maths (no libm, no double)
│   │   ├── vox_input.c      vox_pitch_detect.c   vox_pitch_correct.c
│   │   ├── vox_pitch_shift.c vox_formant.c       vox_harmonizer.c
│   │   ├── vox_glide.c      vox_voice.c          vox_dynamics.c
│   │   └── vox_fx.c         vox_mixer.c          vox_limiter.c
│   ├── ui/                  the eight screens
│   ├── animation/           the anatomical engine
│   └── presets/             the 16 factory presets
├── tests/
│   ├── dsp/                 the engine's tests (plain cc, no toolchain)
│   └── scenarios/           the simulator's scripted runs
├── docs/
│   ├── PRD.md               the product's requirements
│   ├── DSP_ARCHITECTURE.md  how the voice is processed
│   ├── USB_AUDIO.md         Phase 1: the microphone, the host port, the power
│   ├── UI_DESIGN.md         the screens and the anatomy
│   └── PRESETS.md           the presets and the storage
├── build.sh
└── README.md
```

## The microphone, and the power

The FM-1's USB port **does not supply VBUS**. A bus-powered USB microphone plugged straight into it will
not work. Use a **powered hub** between the microphone and the FM-1. This is the platform's own position
for its USB-MIDI host, and it is the single most important thing to know before buying a microphone.

The full Phase 1 findings, the evidence behind them, and the eight things a person with an FM-1 must test,
are in [docs/USB_AUDIO.md](docs/USB_AUDIO.md).

## Licence

The app's own files are MPL-2.0, like the platform it is built on. It is built on **fm1-base** and
**felucca-bsp**, from Felucca by Leo Kuroshita (Hügelton Instruments), and on the JieLi AC79 SDK.
