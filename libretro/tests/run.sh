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
    "$work/syss/xemu" "$work/sysp/xemu" "$work/sysg/xemu" "$work/sysv/xemu"

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
${cc} -O2 -std=c99 -Wall -o "$work/bin/mkdisc" "$here/mkdisc.c"
"$work/bin/mkdisc" "$work/disc.iso"
nasm -f bin -o "$work/sys/xemu/mcpx_1.0.bin" "$here/rom/bootrom.asm"
nasm -f bin -I"$here/rom/" -o "$work/sys/xemu/Complex_4627v1.03.bin" "$here/rom/flash.asm"
cp "$work/sys/xemu/mcpx_1.0.bin" "$work/sysb/xemu/"
nasm -f bin -DBUSY -I"$here/rom/" -o "$work/sysb/xemu/Complex_4627v1.03.bin" "$here/rom/flash.asm"
cp "$work/sys/xemu/mcpx_1.0.bin" "$work/syss/xemu/"
nasm -f bin -DSPIN -I"$here/rom/" -o "$work/syss/xemu/Complex_4627v1.03.bin" "$here/rom/flash.asm"
cp "$work/sys/xemu/mcpx_1.0.bin" "$work/sysp/xemu/"
nasm -f bin -DPAD -I"$here/rom/" -o "$work/sysp/xemu/Complex_4627v1.03.bin" "$here/rom/flash.asm"
cp "$work/sys/xemu/mcpx_1.0.bin" "$work/sysg/xemu/"
nasm -f bin -DGPU -I"$here/rom/" -o "$work/sysg/xemu/Complex_4627v1.03.bin" "$here/rom/flash.asm"
cp "$work/sys/xemu/mcpx_1.0.bin" "$work/sysv/xemu/"
nasm -f bin -DDVD -I"$here/rom/" -o "$work/sysv/xemu/Complex_4627v1.03.bin" "$here/rom/flash.asm"
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

# The display (rom/gpu.c): the NV2A programmed from bare metal, a surface
# cleared red and shown, then left alone - another surface drawn and a
# flip counted every frame - and at frame 30 written green by the CPU, and
# from frame 50 triangles drawn and cleared elsewhere. The picture is red
# at frames 10 and 25 and green at 45 and 85, on both renderers. At frame
# 20 the CPU reads the surface's first and last pixels: the renderer
# downloads it to RAM for that, and both are red (0xffff0000). From frame
# 100 the guest waits on the GPU, a semaphore after a run of no-ops, up to
# 4 times a frame: it must manage all 4 in every frame. A guest waiting on
# the GPU had its time skip on to its next deadline while the GPU worked,
# and managed 1 (Ninja Gaiden Black in slow motion). The ROM waits for each
# vblank polling the GPU's interrupt register: a wait, so the OpenGL run
# goes at least 0.2x real time (it went 0.03x when each poll was a device
# access, rewinding its block).
# On Vulkan the front buffer was dropped as an old surface and a CPU write
# to it was not a new picture: the display stopped after the first frame
# (Jet Set Radio Future black after the Xbox logo).
gpu_check() {
    for want in "pixel_10=0xff0000" "pixel_25=0xff0000" "pixel_45=0x00ff00" \
                "pixel_85=0x00ff00" "peek_0x300014=0xffff0000" \
                "peek_0x300018=0xffff0000" "peek_0x30001c=0x00000004"; do
        grep -E "^$want$" "$1" >/dev/null || {
            echo "FAIL: $2: $want expected, $(grep "^${want%%=*}=" "$1")"; exit 1; }
    done
}
echo "== the display, OpenGL"
timeout 300 $h "$(path "$core")" "$(path "$work/sysg")" $vp --frames 110 --strict \
  --min-speed 0.2 --pixel 10 --pixel 25 --pixel 45 --pixel 85 --peek 0x300014 --peek 0x300018 \
  --peek 0x30001c >"$work/gpu_gl.txt"
grep -E "pixel_|peek_" "$work/gpu_gl.txt"
gpu_check "$work/gpu_gl.txt" "OpenGL"
echo "== the display, Vulkan"
set +e
timeout 300 $h "$(path "$core")" "$(path "$work/sysg")" $vp --vulkan \
  --opt xemu_renderer=vulkan --frames 110 --strict \
  --pixel 10 --pixel 25 --pixel 45 --pixel 85 --peek 0x300014 --peek 0x300018 \
  --peek 0x30001c >"$work/gpu_vk.txt"
r=$?
set -e
case $r in
0) grep -E "pixel_|peek_" "$work/gpu_vk.txt"; gpu_check "$work/gpu_vk.txt" "Vulkan" ;;
3) echo "no Vulkan device: skipped" ;;
*) echo "FAIL: the Vulkan run exited $r"; exit 1 ;;
esac

# DVD reads (rom/dvd.c): READ(10) packets by bus-master DMA from the
# primary slave, as the kernel sends them, into one PRD entry, three
# scattered ones, and ten for 600 KB, from the disc mkdisc.c makes, whose
# every word says which sector and word it is. All 0x2c800 words read are
# checked: status 0, none wrong. The reads go from the image straight into
# the guest's memory; this is what says no byte got lost on the way.
echo "== DVD reads by DMA"
timeout 300 $h "$(path "$core")" "$(path "$work/sysv")" "$(path "$work/disc.iso")" $vp \
  --frames 120 --strict --peek 0x300008 --peek 0x30000c --peek 0x300010 >"$work/dvd.txt"
grep -E "peek_" "$work/dvd.txt"
grep -E "peek_0x300008=0x00000000$" "$work/dvd.txt" >/dev/null &&
grep -E "peek_0x30000c=0x0002c800$" "$work/dvd.txt" >/dev/null || {
    echo "FAIL: the DVD reads: step $(grep peek_0x300008 "$work/dvd.txt"), first wrong word at $(grep peek_0x300010 "$work/dvd.txt")"; exit 1; }

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

# A session killed with the game running (a crash, the frontend killed):
# the HDD image's cache partitions may name data that never reached it,
# and a game read its broken cache back at every boot after (Jet Set Radio
# Future and Ninja Gaiden Black black past the Xbox logo). The mark beside
# the image (<image>.open) outlives the killed session, the next load
# empties X, Y and Z and says so, and a clean close takes the mark away.
# Linux only: a killed Wine launcher may leave the process behind it.
case $core in
*.so)
    echo "== a killed session, then the next load"
    hdd="$work/sys/xemu/xbox_hdd.qcow2"
    set +e
    timeout -s KILL 15 $h "$(path "$core")" "$(path "$work/sys")" $vp --frames 1000000 \
      >/dev/null 2>&1
    set -e
    [ -f "$hdd.open" ] || { echo "FAIL: no session mark beside the HDD image"; exit 1; }
    timeout 300 $h "$(path "$core")" "$(path "$work/sys")" $vp --frames 60 >"$work/hdd.txt" 2>&1
    grep "cache partitions X, Y and Z were emptied" "$work/hdd.txt" || {
        echo "FAIL: the load after a killed session did not empty the cache partitions"; exit 1; }
    [ ! -f "$hdd.open" ] || { echo "FAIL: the session mark is left after a clean close"; exit 1; }
    timeout 300 $h "$(path "$core")" "$(path "$work/sys")" $vp --frames 60 >"$work/hdd2.txt" 2>&1
    if grep "cache partitions X, Y and Z were emptied" "$work/hdd2.txt"; then
        echo "FAIL: the caches were emptied after a clean close"; exit 1
    fi
    ;;
esac

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

# Every shader of the seed dictionary through the GLSL to SPIR-V compiler
# (seedshaders.c): what a generator writes for any state a game is known
# to meet, the compiler takes. Built from the core's own objects, so only
# where the core was built here (a Linux build directory).
if [ -f "$here/../../hw/xbox/nv2a/pgraph/spv/glslc.o" ]; then
    echo "== seed dictionary shaders"
    make -s -C "$here/../.." seedshaders
    "$here/seedshaders" "$here/../../data/shader_seeds.bin" | tail -1
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
