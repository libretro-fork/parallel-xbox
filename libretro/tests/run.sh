#!/bin/sh
# The core's frame and audio tests: the headless harness runs the core with
# the test ROMs (rom/), no Microsoft files needed.
#
#   libretro/tests/run.sh <core.so | core.dll> [work dir]
#
# Needs nasm, a C compiler, and an OpenGL 4.5 core context: on Linux an EGL
# with a surfaceless display (Mesa's llvmpipe will do). The Vulkan test runs
# where there is a Vulkan device (Mesa's lavapipe will do) and is skipped
# where there is none. The Windows core runs in a Windows build of the
# harness: in MSYS2's MINGW64 shell as it is, on Linux under Wine (with the
# MinGW-w64 compiler, HARNESS_CC, and an X display for Wine's OpenGL).
set -e
core=$(readlink -f "$1")
here=$(cd "$(dirname "$0")" && pwd)
work=${2:-$(mktemp -d)}
mkdir -p "$work/bin" "$work/sys/xemu" "$work/sysb/xemu" "$work/sysd/xemu" \
    "$work/syss/xemu" "$work/sysp/xemu"

# The harness's paths, as the core's OS spells them
path() { echo "$1"; }
case $core in
*.dll)
    case $(uname -s) in
    MINGW*|MSYS*)
        cc=${CC:-gcc}
        ${cc} -O2 -std=gnu99 -Wall -I"$here/../../deps/vulkan-headers" -I"$here/../../libretro-common/include" \
            -o "$work/bin/harness.exe" "$here/harness.c" -lopengl32 -lgdi32 -static
        h="$work/bin/harness.exe"
        ;;
    *)
        cc=${CC:-cc}
        ${HARNESS_CC:-x86_64-w64-mingw32-gcc} -O2 -std=gnu99 -Wall \
            -I"$here/../../deps/vulkan-headers" -I"$here/../../libretro-common/include" \
            -o "$work/bin/harness.exe" "$here/harness.c" -lopengl32 -lgdi32 -static
        h="wine $work/bin/harness.exe"
        path() { winepath -w "$1"; }
        ;;
    esac
    ;;
*)
    cc=${CC:-cc}
    ${cc} -O2 -std=gnu99 -Wall -I"$here/../../deps/vulkan-headers" -I"$here/../../libretro-common/include" \
        -o "$work/bin/harness" "$here/harness.c" -ldl -lEGL -lGL -lpthread
    h="$work/bin/harness"
    ;;
esac
${cc} -O2 -std=c99 -Wall -o "$work/bin/sinecheck" "$here/sinecheck.c"
nasm -f bin -o "$work/sys/xemu/mcpx_1.0.bin" "$here/rom/bootrom.asm"
nasm -f bin -I"$here/rom/" -o "$work/sys/xemu/Complex_4627v1.03.bin" "$here/rom/flash.asm"
cp "$work/sys/xemu/mcpx_1.0.bin" "$work/sysb/xemu/"
nasm -f bin -DBUSY -I"$here/rom/" -o "$work/sysb/xemu/Complex_4627v1.03.bin" "$here/rom/flash.asm"
cp "$work/sys/xemu/mcpx_1.0.bin" "$work/syss/xemu/"
nasm -f bin -DSPIN -I"$here/rom/" -o "$work/syss/xemu/Complex_4627v1.03.bin" "$here/rom/flash.asm"
cp "$work/sys/xemu/mcpx_1.0.bin" "$work/sysp/xemu/"
nasm -f bin -DPAD -I"$here/rom/" -o "$work/sysp/xemu/Complex_4627v1.03.bin" "$here/rom/flash.asm"
cp "$work/sys/xemu/mcpx_1.0.bin" "$work/sysd/xemu/"
nasm -f bin -DDSP -DGP_PASSES=16 -I"$here/rom/" -o "$work/sysd/xemu/Complex_4627v1.03.bin" "$here/rom/flash.asm"

t() { echo "== $1"; shift; timeout 300 "$@"; }

# The plain test ROMs play their tone on the voice processor alone and never
# start the DSPs: their tests run without APU DSP Emulation, which is on by
# default where the core has the JIT (a game's DSP programs make its sound).
vp="--opt xemu_use_dsp=disabled"

# The guest idles: unpaced, the frontend's fast-forward. One frame's worth
# of audio and a video call per run, the tone unbroken, far faster than
# real time.
t "idle guest, unpaced" $h "$(path "$core")" "$(path "$work/sys")" $vp --frames 3000 --strict \
  --min-speed 4 --wav "$(path "$work/idle.wav")"
"$work/bin/sinecheck" "$work/idle.wav" --frames 2402400

# The guest's CPU never rests: still exactly a frame's worth a run.
t "busy guest, unpaced" $h "$(path "$core")" "$(path "$work/sysb")" $vp --frames 600 --strict \
  --wav "$(path "$work/busy.wav")"
"$work/bin/sinecheck" "$work/busy.wav" --frames 480480

# The guest spins on RAM that nothing writes, as the Xbox kernel's idle
# thread does: a wait, recognised and skipped, so it runs as fast as the
# halted guest and its tone is as whole.
t "spinning guest, unpaced" $h "$(path "$core")" "$(path "$work/syss")" $vp --frames 3000 --strict \
  --min-speed 4 --wav "$(path "$work/spin.wav")"
"$work/bin/sinecheck" "$work/spin.wav" --frames 2402400

# The same idle guest on a Vulkan context, as RetroArch's vulkan driver
# gives one (the device made by the core's negotiation interface, the frame
# an image handed over, read back under the queue lock): a frame's worth
# of audio and a video call a run, the machine stopped cleanly after. Once
# through version 2 of the negotiation interface, once as a frontend that
# knows only version 1.
for v in "" -v1; do
echo "== idle guest, Vulkan context$v"
set +e
timeout 300 $h "$(path "$core")" "$(path "$work/sys")" $vp --vulkan$v \
  --opt xemu_renderer=vulkan --frames 300 --strict --hash-video
r=$?
set -e
case $r in
0) ;;
3) echo "no Vulkan device: skipped" ;;
*) echo "FAIL: the Vulkan run exited $r"; exit 1 ;;
esac
done

# The pad, through the USB controller as the kernel reads it (rom/pad.c):
# the hubs found by their status-change reports, their ports reset, the
# pad polled on the periodic list. The harness holds Start and the left
# stick at (12000, -20000) from run 20: the setup is done (status 0),
# reports came, the last one has Start (XID wButtons bit 4) and nothing
# else, and the sticks: left (11460, 19100) with the 10% dead zone scaled
# out and Y the Xbox's way up, right its negative. The ROM sends the
# motors every 64th report (left ramping, right its complement), which
# reach the harness's rumble interface.
echo "== the pad over USB"
timeout 300 $h "$(path "$core")" "$(path "$work/sysp")" $vp --frames 300 --strict \
  --pad 20:3:1000 --stick 20:12000:-20000:1000 --peek 0x300008 --peek 0x30000c \
  --peek 0x300010 --peek 0x30001c --peek 0x300020 --peek 0x300028 >"$work/pad.txt"
grep -E "speed=|peek_|rumble_" "$work/pad.txt"
grep -E "peek_0x300008=0x00000000$" "$work/pad.txt" >/dev/null || {
    echo "FAIL: the pad's USB setup did not complete: $(grep peek_0x300008 "$work/pad.txt")"; exit 1; }
grep -E "peek_0x30000c=0x0000[0-9a-f]{4}$" "$work/pad.txt" | grep -v "=0x00000000$" >/dev/null || {
    echo "FAIL: no reports from the pad"; exit 1; }
grep -E "peek_0x300010=0x00101400$" "$work/pad.txt" >/dev/null || {
    echo "FAIL: the pad's report is not Start: $(grep peek_0x300010 "$work/pad.txt")"; exit 1; }
grep -E "peek_0x30001c=0x4a9c2cc4$" "$work/pad.txt" >/dev/null || {
    echo "FAIL: the left stick did not reach the pad: $(grep peek_0x30001c "$work/pad.txt")"; exit 1; }
grep -E "peek_0x300020=0xb564d33c$" "$work/pad.txt" >/dev/null || {
    echo "FAIL: the right stick did not reach the pad: $(grep peek_0x300020 "$work/pad.txt")"; exit 1; }
grep -E "rumble_calls=[1-9]" "$work/pad.txt" >/dev/null || {
    echo "FAIL: the pad's motors did not reach the frontend"; exit 1; }
# Both sticks pushed fully up, RetroArch's -32768: the Xbox's way up is
# 32767 on both, not -32768 (a negation that wrapped, full down: Jet Set
# Radio Future turned round instead of running forward)
echo "== the pad over USB, sticks fully up"
timeout 300 $h "$(path "$core")" "$(path "$work/sysp")" $vp --frames 300 --strict \
  --stick 20:0:-32768:1000 --peek 0x30001c --peek 0x300020 >"$work/pad2.txt"
grep -E "peek_" "$work/pad2.txt"
grep -E "peek_0x30001c=0x7fff0000$" "$work/pad2.txt" >/dev/null || {
    echo "FAIL: the left stick fully up is not 32767: $(grep peek_0x30001c "$work/pad2.txt")"; exit 1; }
grep -E "peek_0x300020=0x7fff0000$" "$work/pad2.txt" >/dev/null || {
    echo "FAIL: the right stick fully up is not 32767: $(grep peek_0x300020 "$work/pad2.txt")"; exit 1; }

# The guest's time is its instructions, never the host's: a frame is the
# same work however fast the host runs it. The busy guest's RAM (its
# counter) after 120 frames is the same flat out and at 59.94 Hz.
echo "== busy guest: the same work flat out and paced"
timeout 300 $h "$(path "$core")" "$(path "$work/sysb")" $vp --frames 120 --strict \
  >"$work/flat.txt"
timeout 300 $h "$(path "$core")" "$(path "$work/sysb")" $vp --frames 120 --strict \
  --realtime >"$work/paced.txt"
flat=$(grep -o 'counter_0x300000=.*' "$work/flat.txt")
paced=$(grep -o 'counter_0x300000=.*' "$work/paced.txt")
echo "flat out: $flat"
echo "paced:    $paced"
if [ -z "$flat" ] || [ "$flat" != "$paced" ]; then
    echo "FAIL: the guest did different work in the same frames"
    exit 1
fi

# The DSP JIT against the interpreter on the test ROM's GP program
# (dspdiff.c): the whole DSP state the same after every frame, from
# random memory around it. Built from the core's sources here; nothing
# to compare where the host has no JIT.
echo "== DSP JIT against the interpreter"
make -s -C "$here/../.." dspdiff
set +e
"$here/dspdiff" --frames 60 --x 0=16 --seed 1 "$here/rom/gp.inc" && \
"$here/dspdiff" --frames 60 --x 0=16 --seed 2 "$here/rom/gp.inc"
rc=$?
set -e
if [ $rc = 2 ]; then echo "no JIT on this host"; elif [ $rc != 0 ]; then exit 1; fi

# APU DSP Emulation on, the GP DSP running a program of 16 x 4096 MACs a
# 32-sample frame (rom/gp.a56) that hands the tone on unchanged: the tone
# comes out of the GP whole, on the interpreter and on the JIT. The time the
# emulator thread spends in the DSP is not the guest's: when it was, a frame
# that took longer than its 0.67 ms made more of them due than it finished,
# and the interpreter fell to a frame a second.
dsp="--opt xemu_use_dsp=enabled"
t "GP DSP, interpreter" $h "$(path "$core")" "$(path "$work/sysd")" --frames 120 --strict \
  --min-speed 0.15 $dsp --opt xemu_dsp_jit=disabled --wav "$(path "$work/dspi.wav")"
"$work/bin/sinecheck" "$work/dspi.wav" --frames 96096 --peak 16385
t "GP DSP, JIT" $h "$(path "$core")" "$(path "$work/sysd")" --frames 600 --strict \
  $dsp --opt xemu_dsp_jit=enabled --wav "$(path "$work/dspj.wav")"
"$work/bin/sinecheck" "$work/dspj.wav" --frames 480480 --peak 16385
t "GP DSP, JIT: save, load, reset" $h "$(path "$core")" "$(path "$work/sysd")" --frames 600 \
  --strict $dsp --opt xemu_dsp_jit=enabled --save 200 --load 350 \
  --verify-load --reset 450

# Savestates are the whole machine in the frontend's buffer: right after a
# load the guest's RAM is byte for byte what it was at the save, in the same
# process and in another one (the busy ROM's RAM changes every frame).
t "save, load: busy guest" $h "$(path "$core")" "$(path "$work/sysb")" $vp --frames 400 --strict \
  --save 200 --load 399 --verify-load --state-out "$(path "$work/busy.state")"
t "load in another process" $h "$(path "$core")" "$(path "$work/sysb")" $vp --frames 100 --strict \
  --load 50 --state-in "$(path "$work/busy.state")" --verify-load

# Savestate, load and reset between frames.
t "save, load, reset" $h "$(path "$core")" "$(path "$work/sys")" $vp --frames 900 --strict \
  --save 200 --load 500 --reset 700

echo "all passed"
