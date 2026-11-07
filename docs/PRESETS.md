# The presets and the storage

## What a preset is

A preset is exactly what the instrument plays: the whole `vox_preset_t` (in `app/dsp/vox.h`). It holds the
mode, the input's settings, the corrector's settings, the multivoice's settings, the scale, the formant
settings, the four voices' settings and the four effect slots with their routing.

The global settings — the microphone's gain, the output's level, the host mode — are **not** in a preset.
They stay in the settings object, so loading a preset never changes how the microphone is set up.

## The factory presets

They are compiled into the firmware (`app/presets/factory_presets.c`) and cannot be overwritten.

| # | Name | What it is |
|---|---|---|
| 0 | ROBOT BASTARD | the extreme pitch correction: RETUNE 100, no glide, robotic, with a bitcrusher |
| 1 | TINY RAT | a high formant and an octave up — a small, bright voice |
| 2 | DEMON | a deep formant shift, an octave down, a vocal distortion |
| 3 | ALIEN CHOIR | four voices, a wide spread, a large reverb |
| 4 | ROTTERDAM 1995 | the distorted robotic vocal: bitcrush, distortion, radio |
| 5 | CHERUB | the bright harmonies: three voices, an octave up, a chorus |
| 6 | TELEPHONE | the band-limited vocal: a telephone band and a compressor |
| 7 | HUGE HALL | the large reverb and a delay |
| 8 | UK GARAGE | the subtle tuning and a delay, with a loose humanise |
| 9 | NIGHTCLUB | the doubler and the compression |
| 10 | EVIL TWIN | the detuned dual voice with a flanger |
| 11 | SYNTHETIC | the hard digital tuning: RETUNE 100, no humanise, robotic |
| 12 | LIQUID | the soft harmoniser: three voices, a slow glide, a chorus |
| 13 | RADIO MONSTER | the radio distortion, a bitcrusher and a compressor |
| 14 | BROKEN ROBOT | the pitch glitches and a stutter |
| 15 | CLEAN STUDIO | the natural correction: RETUNE 30, a little glide, a compressor |

## The user presets

Sixteen of them, saved into the flash. The browser's BROWSE knob moves between the factory and the user
list; CATEGORY sorts by family (the clean, the harmonic, the robotic, the space); LOAD loads; SAVE writes
the current instrument over the selected user preset.

The factory presets are protected: the user list is a separate object, and a factory preset can only be
copied into a user slot, never written over.

## The storage

The platform's store is a set of objects, each in a pair of 4 KiB sectors (A and B), each with a sequence
number, a CRC over the body and a commit record written **last**. A write goes to the copy that is *not*
current, and the reader takes the copy with the higher sequence whose body verifies. A power cut in the
middle of a write therefore leaves the previous copy intact: the store is torn-write safe, and the
platform reads back what it wrote before it reports success.

| Object | What | Where |
|---|---|---|
| `OBJ_SETTINGS` | the globals and the current preset | 0xFC000 |
| `OBJ_PANEL` | the panel's calibration (the platform's) | 0xDC000 |
| `OBJ_BLE` | Bluetooth's address and pairing (the platform's) | 0xDE000 |
| `OBJ_PRESETS` | the 16 user presets | 0xDA000 |

A store object holds **3840 bytes**. The 16 user presets fit in one object with their header; the settings
and the current preset fit in another.

The settings blob is validated on the way in: the magic, then the ranges (the mode, the voice count, the
preset's magic). A blob that fails the check falls back to the defaults rather than to a broken instrument.

**The blob only grows at the end.** A shorter (older) save loads into the front of the struct and the fields
after it keep their defaults. A change that is not an append needs a new magic, and the old saves are lost.

**Saving is never done in the render.** A flash write stops the sound for a moment — the platform says so
explicitly — so the presets are written when the user presses SAVE, and only then.

## A/B

The instrument keeps the page it was left on and the preset it was playing, and the browser's LOAD and the
user's SAVE are the two halves of the comparison: load one, play, load the other, play. The globals' record
of the last preset and the last page comes back at the next power-on.
