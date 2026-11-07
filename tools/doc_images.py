#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
"""The manual's screen images, from the simulator's shots.

    tools/doc_images.py

The manual (app/docs/MANUAL.md) shows the instrument's own screens. A picture that has drifted from
the firmware is worse than no picture: the reader turns a knob that is not there. So the images are
made from the shots the simulator draws (build.sh shots), not drawn by hand:

    app/build/shots/01_home.png          -> app/docs/img/screen-home.png
    ...

The shots are 240x240 PNGs of a few colours, so they are written as a palette PNG: small enough to
keep the site light, and still the screen the device draws."""
import shutil
import sys
from pathlib import Path

APP = Path(__file__).resolve().parents[1] / "app"
SHOTS = APP / "build" / "shots"
IMGS = APP / "docs" / "img"

# the shot's file name -> the image's name in the manual
SHOTS_TO_IMAGES = {
    "01_home.png": "screen-home.png",
    "02_multivoice.png": "screen-multivoice.png",
    "03_glide.png": "screen-glide.png",
    "04_formant_up.png": "screen-formant-up.png",
    "05_presets.png": "screen-presets.png",
    "06_formant.png": "screen-formant.png",
    "07_fx.png": "screen-fx.png",
    "08_input.png": "screen-input.png",
    "09_global.png": "screen-global.png",
}


def write(src, dst):
    """src -> dst: the same picture as a small palette PNG"""
    try:
        from PIL import Image
    except ImportError:
        shutil.copyfile(src, dst)          # no Pillow: the plain PNG is bigger, but it is right
        return dst.stat().st_size
    with Image.open(src) as im:
        im = im.convert("RGB")
        q = im.quantize(colors=256, method=Image.MEDIANCUT, dither=Image.Dither.NONE)
        q.save(dst, optimize=True)
    return dst.stat().st_size


def main():
    if not SHOTS.is_dir():
        print(f"{SHOTS}: no shots (build.sh shots)", file=sys.stderr)
        return 1
    IMGS.mkdir(parents=True, exist_ok=True)
    missing = [s for s in SHOTS_TO_IMAGES if not (SHOTS / s).exists()]
    if missing:
        print(f"{SHOTS}: no {', '.join(missing)} (build.sh shots)", file=sys.stderr)
        return 1
    for shot, name in SHOTS_TO_IMAGES.items():
        size = write(SHOTS / shot, IMGS / name)
        print(f"doc_images: {name} ({size} B)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
