#!/bin/sh
# The core's frame and audio tests: the headless harness runs the core with
# the test ROMs (rom/), no Microsoft files needed.
#
#   libretro/tests/run.sh <core.so> [work dir]
#
# Needs nasm, a C compiler, and an EGL with a surfaceless OpenGL 4.5 core
# context (Mesa's llvmpipe will do).
set -e
core=$(readlink -f "$1")
here=$(cd "$(dirname "$0")" && pwd)
work=${2:-$(mktemp -d)}
mkdir -p "$work/bin" "$work/sys/xemu" "$work/sysb/xemu"

cc -O2 -std=gnu99 -Wall -o "$work/bin/harness" "$here/harness.c" -ldl -lEGL -lGL
cc -O2 -std=c99 -Wall -o "$work/bin/sinecheck" "$here/sinecheck.c"
nasm -f bin -o "$work/sys/xemu/mcpx_1.0.bin" "$here/rom/bootrom.asm"
nasm -f bin -I"$here/rom/" -o "$work/sys/xemu/Complex_4627v1.03.bin" "$here/rom/flash.asm"
cp "$work/sys/xemu/mcpx_1.0.bin" "$work/sysb/xemu/"
nasm -f bin -DBUSY -I"$here/rom/" -o "$work/sysb/xemu/Complex_4627v1.03.bin" "$here/rom/flash.asm"

h="$work/bin/harness"
t() { echo "== $1"; shift; timeout 300 "$@"; }

# The guest idles: unpaced, the frontend's fast-forward. One frame's worth
# of audio and a video call per run, the tone unbroken, far faster than
# real time.
t "idle guest, unpaced" "$h" "$core" "$work/sys" --frames 3000 --strict \
  --min-speed 4 --wav "$work/idle.wav"
"$work/bin/sinecheck" "$work/idle.wav" --frames 2402400

# The guest's CPU never rests: still exactly a frame's worth a run.
t "busy guest, unpaced" "$h" "$core" "$work/sysb" --frames 600 --strict \
  --wav "$work/busy.wav"
"$work/bin/sinecheck" "$work/busy.wav" --frames 480480

# Savestate, load and reset between frames.
t "save, load, reset" "$h" "$core" "$work/sys" --frames 900 --strict \
  --save 200 --load 500 --reset 700

echo "all passed"
