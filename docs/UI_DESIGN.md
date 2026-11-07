# The interface and the anatomy

The screen is 240 × 240 and the platform has no framebuffer: it has one off-screen canvas that the app
draws into and the panel receives in one DMA transfer. That shapes everything here.

---

## The screen model

The screen is used as horizontal bands. Each band is drawn in its own pass (`cv_begin` clears the canvas,
so a band that changes is drawn whole) and then blitted to its place on the panel.

```text
rows    0 ..  27   the header    the title, the mode, the detected note, the input's level
rows   28 .. 203   the anatomy   the instrument's main picture
rows  204 .. 239   the foot      the four knob names (K1..K4) and the page's name
```

**The anatomy band is redrawn every frame.** 240 × 176 × 2 bytes = 84 KiB over the panel's SPI at 12 MHz
costs about 56 ms, so the anatomy runs at roughly **15-18 frames a second** — inside the PRD's 15-30 FPS
target, at the low end, and the reason the band is 176 rows rather than the full 240. The header and the
foot are cheap and can be redrawn whenever they change.

The platform's canvas is `APP_CANVAS_PX` (240 × 176 pixels = 84 KiB) and lives in the RAM pool, not in the
app's 96 KiB of RAM.

### What the drawing primitives are

The platform gives: `cv_begin`, `cv_band`, `cv_blit`, `cv_blit_from`, `cv_rect`, `cv_line`, `cv_text`,
`text_w`, `cv_pset`. **There is no circle, no sprite, no gradient and no image blit.** The anatomy is drawn
procedurally with those, plus the helpers the app writes for itself (an ellipse, a filled polygon, a
mid-point circle) on top of `cv_pset` and `cv_line`.

The fonts are generated from the platform's fontset: `FONT_T` (10 px), `FONT_XS` (12), `FONT_S` (15),
`FONT_B` (15), `FONT_M` (23), `FONT_L` (31). The large ones carry upper case and digits only.

## The anatomy

A side-profile cross-section of a head and throat with a transparent torso behind it. It has to be
*recognisably anatomical* — the PRD's premise is a medical textbook that moves, not a cartoon character.

The structures, and where they live in the band:

| Structure | What it shows |
|---|---|
| the skull's outline | bone white, the profile's line |
| the nasal cavity | the upper front; it lights when the nasal resonance is up |
| the oral cavity and the lips | the mouth's opening; it changes with the articulation |
| the tongue | a mass in the mouth; it moves with the formants |
| the pharynx | the throat behind the mouth; its length changes with the formants |
| the larynx | at the bottom of the pharynx |
| the vocal folds | two small structures at the larynx — the picture's focus |
| the trachea | the windpipe down to the lungs |
| the lungs and the diaphragm | two lobes and a dome under them |

The colours: a dark laboratory ground, bone white for the outlines, warm pink / red / coral for the tissue,
amber and cyan for the DSP's readouts, and thin annotation lines with small labels ("NASAL", "PHARYNX",
"LARYNX", "TRACHEA", "LUNGS") in the platform's small font.

## The animation

Each of these is driven by a real value, and all of them are visible:

| The value | What moves |
|---|---|
| the input's amplitude | the lungs expand and contract; with no input they breathe slowly |
| the airflow | the particles travel lungs → trachea → larynx → pharynx → mouth (and the nose), at the input's speed |
| the detected f0 | the folds vibrate faster or slower |
| the correction | a second, dimmer indicator shows the target: the original and the corrected are told apart |
| the formant shift | the tract's shape: a high formant shortens and brightens it, a low one lengthens and darkens it |
| the distortion | the particles go jagged; the outline grows a pixelated edge |
| the delay | ghost outlines of the tract behind the original, fading with the feedback |
| the reverb | resonance rings expanding from the head, fading with the tail |
| the overload | the level turns red, the lungs inflate, "OVERLOAD" appears |
| the voices | the extra voices appear as smaller ghosted anatomical overlays, each with its own pitch, colour, level and moving folds |

The animation never depends on the audio and the audio never depends on the animation: the render
publishes a handful of scalars (the level, the f0, the target, the clarity) and the frame reads them once.

## The eight screens

| Page | The picture | K1 | K2 | K3 | K4 |
|---|---|---|---|---|---|
| **HOME** | the whole anatomy | VOICE COUNT | SPREAD | WET/DRY | FX MACRO |
| **MULTIVOICE** | the vocal paths, the voices | VOICE COUNT | HARMONY | DETUNE | MIX |
| **GLIDE** | the folds, the detected and the corrected pitch | RETUNE | GLIDE | HUMANIZE | MIX |
| **FORMANT** | an enlarged head and tract | FORMANT | THROAT | NASAL | PRESERVE |
| **FX** | the four slots | the slot's parameter 1 | parameter 2 | parameter 3 | MOVE |
| **PRESETS** | the browser | BROWSE | CATEGORY | LOAD | SAVE |
| **INPUT** | the microphone's state | GAIN | MONITOR | FORMAT | HOST |
| **GLOBAL** | the system | MIDI | OUT | THEME | VERSION |

The knobs write straight into the engine's parameters (`vox_preset_t` in `app/dsp/vox.h`); the foot always
shows what the knobs do *on this page*.

## The controls

The FM-1's real controls, from `plat.h`: fourteen buttons (`B_FX`, `B_SEL`, `B_ENV`, `B_LFO`, `B_EDIT`,
`B_GLO`, `B_HOME`, `B_SAVE`, `B_ARP`, `B_SEQ`, `B_PLAY`, `B_REC`, `B_OCTDN`, `B_OCTUP`) and seven encoders
(`EN_SELECT`, `EN_ALGO`, `EN_PRESET`, `EN_K1`..`EN_K4`).

Note that the PRD's table lists `ALGORITHM`, `PRESETS`, `SELECT` and `K1`-`K4` as if they were buttons:
on the real FM-1 they are **encoders**. `ALGORITHM` and `PRESETS` are printed "ALGO" and "PRESET".

The encoders are read as *detents since the last call*, and the device zeroes the accumulator on every
read — so every encoder must be polled every frame or its movement is lost.

The mapping the app uses:

| Control | What it does |
|---|---|
| HOME | the anatomical overview |
| EDIT | the FORMANT page |
| FX | the effects chain |
| SELECT (encoder, pushed) | the page's own action: on PRESETS, the browser's focus |
| ALGO (encoder) | MULTIVOICE ↔ GLIDE |
| PRESET (encoder) | the preset browser |
| K1..K4 | the page's four parameters |
| OCT- / OCT+ | the octave, down and up |
| SAVE | store the preset |
| PLAY | the effects' bypass, the freeze |
| REC | the microphone's monitoring |
| SEL, ENV, LFO, GLO, ARP, SEQ | the pages they name |

Every change is announced with `app_say()`, so the instrument can be driven with a screen reader.

## Testing the interface

The host simulator renders the real screen and writes it as a PNG:

```sh
./build.sh shots          # app/build/shots/*.png, one per screen
```

`app/tests/screens.txt` is the script: it presses the buttons that change the page and shoots each one.
The simulator's script language is documented in `base/host/plat_host.c`'s header comment — `wait`, `shot`,
`press`, `release`, `tap`, `key`, `turn`, `master`, `expect`, `listen`, `wav`, `echo`, `reboot`.
