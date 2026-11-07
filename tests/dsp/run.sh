#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
# ANATOMIX VOXINATOR's DSP tests: app/dsp/ built with the computer's own C compiler (no FM-1 toolchain,
# no fm1-base, no SDK). The engine stays platform-free, so it compiles on its own.
#   tests/dsp/run.sh [OUTDIR]
#
# The device's rules are enforced on the engine's own files (float only, no double promotion, no float
# conversion). The test programs run on the computer, where double is allowed, so they are built with the
# ordinary warnings.
set -e
cd "$(dirname "$0")/../.."
OUT="${1:-build/dsp}"
mkdir -p "$OUT"
CC="${CC:-cc}"

# the device build rejects double precision: warn here on any float that gets promoted
CFLAGS="-std=c99 -O2 -Wall -Wextra -Werror -Wdouble-promotion -Wfloat-conversion -Wno-unused-parameter -Iapp/dsp"
# the tests are the computer's own: they may use double (they measure against the reference)
TFLAGS="-std=c99 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -Iapp/dsp"

for f in vox_pitch_detect vox_pitch_correct vox_glide vox_harmonizer vox_formant \
         vox_input vox_pitch_shift vox_voice vox_dynamics vox_fx vox_mixer vox_limiter vox_engine; do
    [ -f "app/dsp/$f.c" ] || { echo "run.sh: app/dsp/$f.c is missing"; exit 1; }
    # shellcheck disable=SC2086
    "$CC" $CFLAGS -c "app/dsp/$f.c" -o "$OUT/$f.o"
done

# the tests
# shellcheck disable=SC2086
"$CC" $TFLAGS tests/dsp/test_pitch.c "$OUT"/*.o -lm -o "$OUT/test_pitch"
# shellcheck disable=SC2086
"$CC" $TFLAGS tests/dsp/test_chain.c "$OUT"/*.o -lm -o "$OUT/test_chain"

"$OUT/test_pitch"
"$OUT/test_chain"
echo "run.sh: the DSP's tests passed"
