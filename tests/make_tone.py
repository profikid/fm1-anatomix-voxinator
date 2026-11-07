#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 ANATOMIX VOXINATOR
"""The test tones for the microphone path, as 16-bit PCM WAV files.

The host simulator's "wavin" takes a WAV file as the USB microphone's capture, the way
device/usb_mic.c would deliver it. The tones cover the cases the converter has to get right:
a mono device at the engine's rate, a stereo device above it, and a mono device above it.

    python3 tests/make_tone.py OUTDIR [HZ]
"""
import math
import os
import struct
import sys
import wave

RATE = 44100
HZ = 220.0            # A3: the note the engine must report (57)


def tone(path, rate, channels, hz, seconds=3.0, amp=0.5):
    n = int(rate * seconds)
    frames = bytearray()
    for i in range(n):
        v = int(amp * 32767.0 * math.sin(2.0 * math.pi * hz * i / rate))
        for _ in range(channels):
            frames += struct.pack("<h", v)
    with wave.open(path, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(bytes(frames))


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else "."
    hz = float(sys.argv[2]) if len(sys.argv) > 2 else HZ
    os.makedirs(outdir, exist_ok=True)
    tone(os.path.join(outdir, "sine_mono_44100.wav"), 44100, 1, hz)
    tone(os.path.join(outdir, "sine_stereo_48000.wav"), 48000, 2, hz)
    tone(os.path.join(outdir, "sine_mono_48000.wav"), 48000, 1, hz)
    print("make_tone: the tones are in %s" % outdir)


if __name__ == "__main__":
    main()
