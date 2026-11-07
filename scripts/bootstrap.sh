#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
# Get ANATOMIX VOXINATOR ready to build: the platform (base/ and its own submodule), and the tools.
#   scripts/bootstrap.sh            the platform only
#   scripts/bootstrap.sh --tools    the platform and the flashing tools (needs the JieLi SDK)
set -e
cd "$(dirname "$0")/.."

echo "bootstrap: the platform (base/ and felucca-bsp)"
git submodule update --init --recursive

if [ ! -f base/tools/build.py ]; then
    echo "bootstrap: base/tools/build.py is missing: the submodule did not come in" >&2
    exit 1
fi
if [ ! -f base/bsp/chip/usb.c ]; then
    echo "bootstrap: base/bsp is empty: felucca-bsp did not come in" >&2
    exit 1
fi

echo "bootstrap: the Python the build needs"
PY="${PYTHON:-python3}"
"$PY" -c 'import PIL' 2>/dev/null || {
    echo "bootstrap: $PY has no Pillow (pip install Pillow)" >&2
    exit 1
}

if [ "$1" = "--tools" ]; then
    echo "bootstrap: the flashing tools"
    [ -d "$HOME/fw-AC79_AIoT_SDK" ] || {
        echo "bootstrap: the JieLi SDK is missing: ~/fw-AC79_AIoT_SDK" >&2
        echo "bootstrap: (it is only needed to build the firmware package and to flash it)" >&2
        exit 1
    }
    "$PY" -m pip install --user -r base/tools/requirements.txt 2>/dev/null || true
fi

echo "bootstrap: ready."
echo "bootstrap:   ./build.sh test      the DSP's tests (no toolchain needed)"
echo "bootstrap:   ./build.sh --host    the host simulator"
echo "bootstrap:   ./build.sh shots     the screens, as PNGs"
