#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
# Build ANATOMIX VOXINATOR with fm1-base (base/: run scripts/bootstrap.sh once first).
#   ./build.sh                    app/build/anatomix.fwsc (the firmware package)
#   ./build.sh --release X.Y      app/build/anatomix-X.Y.fwsc
#   ./build.sh --host             the host simulator: app/build/host/anatomix_host
#   ./build.sh --web              the browser emulator: app/build/emu/
#   ./build.sh test               the DSP's tests (plain cc, no toolchain needed)
#   ./build.sh audio              the microphone path in the simulator (the tones and the checks)
#   ./build.sh shots              the screens in the simulator: app/build/shots/*.png
#   ./build.sh info               what the connected FM-1 is running
#   ./build.sh flash [FILE]       install over USB-MIDI (default app/build/anatomix.fwsc)
# JIELI_TOOLCHAIN, AC79_SDK and PYTHON override the defaults (~/.jieli/toolchain, ~/fw-AC79_AIoT_SDK, python3).
set -e
cd "$(dirname "$0")"
export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
PY="${PYTHON:-python3}"

case "$1" in
    test)
        exec sh tests/dsp/run.sh ;;
    audio)
        shift
        "$PY" base/tools/build.py app --host
        mkdir -p app/build/audio
        "$PY" tests/make_tone.py app/build/audio
        app/build/host/anatomix_host app/tests/audio.txt app/build/audio
        echo "build.sh: the microphone path is in order"
        exit ;;
    shots)
        shift
        "$PY" base/tools/build.py app --host
        mkdir -p app/build/shots
        app/build/host/anatomix_host app/tests/screens.txt app/build/shots
        echo "build.sh: the screens are in app/build/shots/"
        exit ;;
    info)
        exec "$PY" base/tools/fm1_install.py --info ;;
    flash)
        PKG="${2:-app/build/anatomix.fwsc}"
        [ -f "$PKG" ] || { echo "build.sh: no $PKG (./build.sh first)"; exit 1; }
        echo "Installing $PKG. Keep the FM-1 plugged in and charged until it restarts."
        exec "$PY" base/tools/fm1_install.py "$PKG" ;;
esac

[ -f base/tools/build.py ] || { echo "build.sh: no fm1-base in base/ (scripts/bootstrap.sh)"; exit 1; }
exec "$PY" base/tools/build.py app "$@"
