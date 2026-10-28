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
mkdir -p "$work/bin" "$work/sys/xemu" "$work/sysb/xemu" "$work/sysd/xemu"

cc -O2 -std=gnu99 -Wall -o "$work/bin/harness" "$here/harness.c" -ldl -lEGL -lGL
cc -O2 -std=c99 -Wall -o "$work/bin/sinecheck" "$here/sinecheck.c"
nasm -f bin -o "$work/sys/xemu/mcpx_1.0.bin" "$here/rom/bootrom.asm"
nasm -f bin -I"$here/rom/" -o "$work/sys/xemu/Complex_4627v1.03.bin" "$here/rom/flash.asm"
cp "$work/sys/xemu/mcpx_1.0.bin" "$work/sysb/xemu/"
nasm -f bin -DBUSY -I"$here/rom/" -o "$work/sysb/xemu/Complex_4627v1.03.bin" "$here/rom/flash.asm"
cp "$work/sys/xemu/mcpx_1.0.bin" "$work/sysd/xemu/"
nasm -f bin -DDSP -DGP_PASSES=16 -I"$here/rom/" -o "$work/sysd/xemu/Complex_4627v1.03.bin" "$here/rom/flash.asm"

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

# APU DSP Emulation on, the GP DSP running a program of 16 x 4096 MACs a
# 32-sample frame (rom/gp.a56) that hands the tone on unchanged: the tone
# comes out of the GP whole, on the interpreter and on the JIT. The time the
# emulator thread spends in the DSP is not the guest's: when it was, a frame
# that took longer than its 0.67 ms made more of them due than it finished,
# and the interpreter fell to a frame a second.
dsp="--opt xemu_use_dsp=enabled"
t "GP DSP, interpreter" "$h" "$core" "$work/sysd" --frames 120 --strict \
  --min-speed 0.15 $dsp --opt xemu_dsp_jit=disabled --wav "$work/dspi.wav"
"$work/bin/sinecheck" "$work/dspi.wav" --frames 96096 --peak 16385
t "GP DSP, JIT" "$h" "$core" "$work/sysd" --frames 600 --strict \
  $dsp --opt xemu_dsp_jit=enabled --wav "$work/dspj.wav"
"$work/bin/sinecheck" "$work/dspj.wav" --frames 480480 --peak 16385
t "GP DSP, JIT: save, load, reset" "$h" "$core" "$work/sysd" --frames 600 \
  --strict $dsp --opt xemu_dsp_jit=enabled --save 200 --load 350 \
  --reset 450

# Savestate, load and reset between frames.
t "save, load, reset" "$h" "$core" "$work/sys" --frames 900 --strict \
  --save 200 --load 500 --reset 700

echo "all passed"
