/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* ANATOMIX VOXINATOR: the 16 factory presets.
 *
 * They are compiled into the firmware (the PRD's §9): the factory presets are protected, the user presets
 * are the ones the instrument saves into. Every preset is a whole vox_preset_t — the engine's parameters,
 * the voices and the effect routing — so a preset is exactly what the instrument plays.
 */
#include "vox.h"

/* a preset with everything at its default, then the fields that matter */
static vox_preset_t base_preset(void)
{
    vox_preset_t p;
    uint32_t i;

    for (i = 0; i < sizeof p; i++)
        ((uint8_t *)&p)[i] = 0;

    p.magic = VOX_MAGIC;
    p.version = VOX_PRESET_VERSION;
    p.mode = 1;                          /* GLIDE */
    p.in_gain = 70;
    p.in_gate = 30;
    p.retune = 35;
    p.glide = 30;
    p.humanize = 20;
    p.flex = 40;
    p.mix = 100;
    p.octave = 0;
    p.voices = 2;
    p.spread = 50;
    p.detune = 20;
    p.character = 30;
    p.harmony = 3;                       /* a minor third */
    p.chaos = 0;
    p.scale.root = 0;                    /* C */
    p.scale.scale = VOX_SCALE_CHROMATIC;
    p.formant = 0;
    p.formant_amt = 100;
    for (i = 0; i < VOX_MAX_VOICES; i++) {
        p.voice[i].on = (uint8_t)(i == 0u ? 1u : 0u);
        p.voice[i].level = 100;
        p.voice[i].pan = 50;
        p.voice[i].width = 50;
        p.voice[i].detune = 20;
        p.voice[i].send = 0;
    }
    for (i = 0; i < VOX_FX_SLOTS; i++) {
        p.fx[i].type = VOX_FX_NONE;
        p.fx[i].on = 0;
        p.fx[i].mix = 100;
    }
    return p;
}

/* the name of a factory preset, for the display */
const char *vox_factory_name(uint32_t index)
{
    static const char *const N[VOX_FACTORY_PRESETS] = {
        "ROBOT BASTARD", "TINY RAT",     "DEMON",       "ALIEN CHOIR",
        "ROTTERDAM 1995", "CHERUB",       "TELEPHONE",   "HUGE HALL",
        "UK GARAGE",     "NIGHTCLUB",     "EVIL TWIN",   "SYNTHETIC",
        "LIQUID",        "RADIO MONSTER", "BROKEN ROBOT", "CLEAN STUDIO"
    };
    return index < VOX_FACTORY_PRESETS ? N[index] : "?";
}

/* the factory preset index. A fresh voice_preset, then only what the preset changes. */
vox_preset_t vox_factory_preset(uint32_t index)
{
    vox_preset_t p = base_preset();

    switch (index) {
    case 0:                              /* ROBOT BASTARD: the extreme pitch correction */
        p.mode = 1;
        p.retune = 100;
        p.glide = 0;
        p.humanize = 0;
        p.flex = 0;
        p.mix = 100;
        p.character = 90;
        p.fx[0].type = VOX_FX_ROBOTIC;
        p.fx[0].on = 1;
        p.fx[0].mix = 60;
        p.fx[1].type = VOX_FX_BITCRUSH;
        p.fx[1].on = 1;
        p.fx[1].mix = 40;
        break;

    case 1:                              /* TINY RAT: the high formant and the octave */
        p.mode = 1;
        p.octave = 1;
        p.formant = 12;
        p.formant_amt = 100;
        p.retune = 60;
        p.mix = 100;
        p.fx[0].type = VOX_FX_FORMANT_FILTER;
        p.fx[0].on = 1;
        p.fx[0].mix = 50;
        break;

    case 2:                              /* DEMON: the deep formant shift */
        p.mode = 1;
        p.octave = -1;
        p.formant = -12;
        p.formant_amt = 100;
        p.retune = 55;
        p.glide = 45;
        p.character = 70;
        p.fx[0].type = VOX_FX_VOCAL_DIST;
        p.fx[0].on = 1;
        p.fx[0].mix = 55;
        break;

    case 3:                              /* ALIEN CHOIR: the multiple harmonies */
        p.mode = 0;
        p.voices = 4;
        p.spread = 80;
        p.detune = 25;
        p.harmony = 7;
        p.humanize = 35;
        p.retune = 70;
        p.mix = 100;
        for (uint32_t i = 0; i < VOX_MAX_VOICES; i++) {
            p.voice[i].on = 1;
            p.voice[i].level = (uint8_t)(i == 0u ? 100u : 70u);
            p.voice[i].pan = (uint8_t)(30u + i * 13u);
            p.voice[i].width = 70;
        }
        p.fx[0].type = VOX_FX_REVERB;
        p.fx[0].on = 1;
        p.fx[0].mix = 40;
        break;

    case 4:                              /* ROTTERDAM 1995: the distorted robotic vocal */
        p.mode = 1;
        p.retune = 90;
        p.glide = 10;
        p.character = 85;
        p.fx[0].type = VOX_FX_BITCRUSH;
        p.fx[0].on = 1;
        p.fx[0].mix = 55;
        p.fx[1].type = VOX_FX_VOCAL_DIST;
        p.fx[1].on = 1;
        p.fx[1].mix = 65;
        p.fx[2].type = VOX_FX_RADIO;
        p.fx[2].on = 1;
        p.fx[2].mix = 45;
        break;

    case 5:                              /* CHERUB: the bright harmonies */
        p.mode = 0;
        p.voices = 3;
        p.spread = 60;
        p.harmony = 4;
        p.octave = 1;
        p.formant = 5;
        p.retune = 50;
        p.humanize = 30;
        for (uint32_t i = 0; i < VOX_MAX_VOICES; i++) {
            p.voice[i].on = 1;
            p.voice[i].level = (uint8_t)(i == 0u ? 100u : 65u);
            p.voice[i].pan = (uint8_t)(38u + i * 12u);
        }
        p.fx[0].type = VOX_FX_CHORUS;
        p.fx[0].on = 1;
        p.fx[0].mix = 45;
        break;

    case 6:                              /* TELEPHONE: the band-limited vocal */
        p.mode = 1;
        p.retune = 45;
        p.fx[0].type = VOX_FX_TELEPHONE;
        p.fx[0].on = 1;
        p.fx[0].mix = 85;
        p.fx[1].type = VOX_FX_COMP;
        p.fx[1].on = 1;
        p.fx[1].mix = 50;
        break;

    case 7:                              /* HUGE HALL: the large reverb */
        p.mode = 0;
        p.voices = 3;
        p.spread = 70;
        p.retune = 45;
        p.humanize = 25;
        p.fx[0].type = VOX_FX_REVERB;
        p.fx[0].on = 1;
        p.fx[0].mix = 85;
        p.fx[1].type = VOX_FX_DELAY;
        p.fx[1].on = 1;
        p.fx[1].mix = 30;
        break;

    case 8:                              /* UK GARAGE: the subtle tuning and the delay */
        p.mode = 1;
        p.retune = 25;
        p.glide = 35;
        p.humanize = 45;
        p.flex = 70;
        p.mix = 70;
        p.fx[0].type = VOX_FX_DELAY;
        p.fx[0].on = 1;
        p.fx[0].mix = 35;
        p.fx[1].type = VOX_FX_COMP;
        p.fx[1].on = 1;
        p.fx[1].mix = 40;
        break;

    case 9:                              /* NIGHTCLUB: the doubler and the compression */
        p.mode = 0;
        p.voices = 3;
        p.spread = 55;
        p.detune = 35;
        p.humanize = 40;
        p.retune = 55;
        p.fx[0].type = VOX_FX_DOUBLER;
        p.fx[0].on = 1;
        p.fx[0].mix = 55;
        p.fx[1].type = VOX_FX_COMP;
        p.fx[1].on = 1;
        p.fx[1].mix = 60;
        break;

    case 10:                             /* EVIL TWIN: the detuned dual voice */
        p.mode = 0;
        p.voices = 2;
        p.spread = 65;
        p.detune = 85;
        p.harmony = 1;
        p.retune = 60;
        p.fx[0].type = VOX_FX_FLANGER;
        p.fx[0].on = 1;
        p.fx[0].mix = 40;
        break;

    case 11:                             /* SYNTHETIC: the hard digital tuning */
        p.mode = 1;
        p.retune = 100;
        p.glide = 0;
        p.humanize = 0;
        p.flex = 0;
        p.character = 100;
        p.mix = 100;
        p.fx[0].type = VOX_FX_ROBOTIC;
        p.fx[0].on = 1;
        p.fx[0].mix = 70;
        p.fx[1].type = VOX_FX_BITCRUSH;
        p.fx[1].on = 1;
        p.fx[1].mix = 35;
        break;

    case 12:                             /* LIQUID: the soft harmoniser */
        p.mode = 0;
        p.voices = 3;
        p.spread = 45;
        p.detune = 12;
        p.harmony = 7;
        p.retune = 40;
        p.glide = 55;
        p.humanize = 45;
        p.fx[0].type = VOX_FX_CHORUS;
        p.fx[0].on = 1;
        p.fx[0].mix = 55;
        p.fx[1].type = VOX_FX_DELAY;
        p.fx[1].on = 1;
        p.fx[1].mix = 25;
        break;

    case 13:                             /* RADIO MONSTER: the radio distortion */
        p.mode = 1;
        p.retune = 65;
        p.fx[0].type = VOX_FX_RADIO;
        p.fx[0].on = 1;
        p.fx[0].mix = 80;
        p.fx[1].type = VOX_FX_BITCRUSH;
        p.fx[1].on = 1;
        p.fx[1].mix = 45;
        p.fx[2].type = VOX_FX_COMP;
        p.fx[2].on = 1;
        p.fx[2].mix = 55;
        break;

    case 14:                             /* BROKEN ROBOT: the pitch glitches */
        p.mode = 1;
        p.retune = 85;
        p.glide = 15;
        p.chaos = 60;
        p.fx[0].type = VOX_FX_PITCH_GLITCH;
        p.fx[0].on = 1;
        p.fx[0].mix = 65;
        p.fx[1].type = VOX_FX_STUTTER;
        p.fx[1].on = 1;
        p.fx[1].mix = 40;
        break;

    case 15:                             /* CLEAN STUDIO: the natural correction */
    default:
        p.mode = 1;
        p.retune = 30;
        p.glide = 25;
        p.humanize = 25;
        p.flex = 65;
        p.mix = 100;
        p.voices = 1;
        p.fx[0].type = VOX_FX_COMP;
        p.fx[0].on = 1;
        p.fx[0].mix = 35;
        break;
    }
    return p;
}
