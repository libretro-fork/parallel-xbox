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
# A work dir of its own (about 500 MB) is removed when the script ends, passed
# or failed; one given as the second argument is kept to look into.
if [ -n "$2" ]; then
    work=$2
else
    work=$(mktemp -d)
    trap 'rm -rf "$work"' EXIT
    trap 'exit 1' INT TERM HUP
fi
mkdir -p "$work/bin" "$work/sys/xemu" "$work/sysb/xemu" "$work/sysd/xemu" \
    "$work/syss/xemu" "$work/sysp/xemu" "$work/sysg/xemu" "$work/sysv/xemu" \
    "$work/sysr/xemu" "$work/sysf/xemu" "$work/syswork/xemu" "$work/sysa/xemu" "$work/syse/xemu" \
    "$work/sysh/xemu" "$work/sysc/xemu"

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
case $core in
*.so) python3 "$here/test-libretro-jit.py" "$here/../.." "$cc" ;;
esac
python3 "$here/test-kernel-idle.py" "$here/../.." "$cc"
python3 "$here/test-frame-completion.py" "$here/../.." "$cc"
python3 "$here/test-vk-cow-tail.py" "$here/../.." "$cc"
python3 "$here/test-inline-index-bounds.py" "$here/../.." "$cc"
${cc} -O2 -std=c11 -Wall -o "$work/bin/scanoutcheck" "$here/test-scanout.c"
${cc} -O2 -std=c11 -Wall -I"$here/../.." -o "$work/bin/mcpxcheck" "$here/test-mcpx-boot.c"
"$work/bin/mcpxcheck"
"$work/bin/scanoutcheck"
${cc} -O2 -std=c11 -Wall -o "$work/bin/scanoutbgcheck" "$here/test-scanout-background.c"
"$work/bin/scanoutbgcheck"
${cc} -O2 -std=c11 -Wall -o "$work/bin/scanoutregions" "$here/test-scanout-regions.c"
"$work/bin/scanoutregions"
${cc} -O2 -std=c11 -Wall -o "$work/bin/scanoutbytes" "$here/test-scanout-bytes.c"
"$work/bin/scanoutbytes"
# The audio pull's margin without waiting for the DSPs' thread (monitor.h)
${cc} -O2 -std=c99 -Wall -o "$work/bin/audiomargin" "$here/test-audio-margin.c"
"$work/bin/audiomargin"
${cc} -O2 -std=c99 -Wall -I"$here/../.." -o "$work/bin/audiovolume" "$here/test-audio-volume.c" -lm
"$work/bin/audiovolume"
${cc} -O2 -std=c99 -Wall -I"$here/../.." -o "$work/bin/surfaceclear" "$here/test-surface-clear.c"
"$work/bin/surfaceclear"
${cc} -O2 -std=c99 -Wall -I"$here/../.." -o "$work/bin/vramranges" "$here/test-vram-ranges.c"
"$work/bin/vramranges"
python3 "$here/test-texture-dirty.py" "$here/../.." "$cc"
python3 "$here/test-vulkan-surface-age.py" "$here/../.." "$cc"
# The voice processor's own functions, lifted from vp.c: decoding, the
# resampler, filters, gains, voice ends and the frame's voice list
python3 "$here/test-voice-tail.py" "$here/../.." "$cc"
# The voice processor's frame on four threads the same as on one
python3 "$here/test-vp-threads.py" "$here/../.." "$cc"
# The VP's integer pitch, cutoff and envelope curves and kernel table
# against the double-precision code they replaced, sampled ("full": all)
${cc} -O2 -std=c99 -Wall -I"$here/../.." -o "$work/bin/vpmath" "$here/test-vpmath.c" -lm
"$work/bin/vpmath"
${cc} -O2 -std=c99 -Wall -o "$work/bin/sinecheck" "$here/sinecheck.c"
${cc} -O2 -std=c99 -Wall -o "$work/bin/rampcheck" "$here/rampcheck.c"
${cc} -O2 -std=c99 -Wall -o "$work/bin/mkdisc" "$here/mkdisc.c"
"$work/bin/mkdisc" "$work/disc.iso"
# The HDD's qcow2 images (block/qcow2-raw.c, block/libretro-qcow2.c), on
# their own: the bundled image written out raw; then, where there is a
# qemu-img to make the images and judge them (the Linux CI installs it),
# random writes, zeroed ranges and reads on qcow2 images of every kind
# xemu has (versions 2 and 3, 64 KB and 4 KB clusters, compressed, with a
# snapshot, the bundled HDD), each checked against a raw copy. qemu-img
# check must find no errors and no leaks, qemu-img compare no difference,
# the snapshot must read as it was taken; and an image whose writer is
# killed partway must still have no errors (leaks at most).
lrc="$here/../../libretro-common"
# The core's GLib (libretro/glib) on its own: containers, strings, paths,
# files, checksums and the main context as QEMU drives it. With the
# Windows core under Wine, its Windows build as well.
glibsrc="$here/test-glib.c $here/../glib/gcontainers.c $here/../glib/gcore.c \
    $here/../glib/gfile.c $here/../glib/gmain.c $here/../glib/gunix.c \
    $lrc/compat/compat_strl.c $lrc/compat/compat_posix_string.c \
    $lrc/compat/fopen_utf8.c $lrc/encodings/encoding_utf.c \
    $lrc/encodings/encoding_base64.c $lrc/hash/lrc_hash.c \
    $lrc/features/features_cpu.c $lrc/file/file_path.c \
    $lrc/file/file_path_io.c $lrc/file/retro_dirent.c \
    $lrc/streams/file_stream.c $lrc/string/stdstring.c \
    $lrc/string/rstrtod.c $lrc/time/rtime.c $lrc/vfs/vfs_implementation.c"
glibflags="-O2 -std=gnu11 -w -I$here/../glib -I$lrc/include -Dpath_is_absolute=lrc_path_is_absolute"
mkdir -p "$work/glib"
# shellcheck disable=SC2086
${cc} $glibflags -o "$work/bin/glibtest" $glibsrc -lpthread
"$work/bin/glibtest" "$work/glib"
case $h in
wine*)
    # shellcheck disable=SC2086
    ${HARNESS_CC:-x86_64-w64-mingw32-gcc} $glibflags -o "$work/bin/glibtest.exe" \
        $glibsrc -static
    wine "$work/bin/glibtest.exe" "$(path "$work/glib")"
    ;;
esac
${cc} -O2 -std=gnu99 -Wall -I"$lrc/include" -o "$work/bin/qcow2test" "$here/test-qcow2.c" \
    "$here/../../block/qcow2-raw.c" "$here/../../block/libretro-qcow2.c" \
    "$here/../../block/libretro-file.c" \
    "$lrc/encodings/encoding_deflate.c" "$lrc/encodings/encoding_crc32.c" \
    "$lrc/features/features_cpu.c" "$lrc/time/rtime.c" "$lrc/compat/compat_strl.c" -lpthread
"$work/bin/qcow2test" "$here/../../data/xbox_hdd.qcow2" "$work"
if command -v qemu-img >/dev/null 2>&1; then
    q="$work/qcow2"
    mkdir -p "$q"
    head -c 40000000 /dev/urandom > "$q/base.raw"
    truncate -s 128M "$q/base.raw"
    seed=1
    for kind in v3 v2 v3-4k compressed snapshot bundled; do
        rm -f "$q/img.qcow2"
        case $kind in
        v3) qemu-img convert -O qcow2 -o compat=1.1 "$q/base.raw" "$q/img.qcow2" ;;
        v2) qemu-img convert -O qcow2 -o compat=0.10 "$q/base.raw" "$q/img.qcow2" ;;
        v3-4k) qemu-img convert -O qcow2 -o cluster_size=4096 "$q/base.raw" "$q/img.qcow2" ;;
        compressed) qemu-img convert -c -O qcow2 "$q/base.raw" "$q/img.qcow2" ;;
        snapshot) qemu-img convert -O qcow2 "$q/base.raw" "$q/img.qcow2"
                  qemu-img snapshot -c s1 "$q/img.qcow2" ;;
        bundled) cp "$here/../../data/xbox_hdd.qcow2" "$q/img.qcow2" ;;
        esac
        rm -f "$q/ref.raw"
        qemu-img convert -O raw "$q/img.qcow2" "$q/ref.raw"
        "$work/bin/qcow2test" --workload "$q/img.qcow2" "$q/ref.raw" $seed 600
        seed=$((seed + 1))
        qemu-img check "$q/img.qcow2" >"$q/check.txt" 2>&1 || {
            cat "$q/check.txt"; echo "FAIL: qemu-img check, $kind image"; exit 1; }
        qemu-img compare "$q/img.qcow2" "$q/ref.raw" >/dev/null || {
            echo "FAIL: the $kind image differs from its reference"; exit 1; }
        if [ $kind = snapshot ]; then
            rm -f "$q/s1.raw"
            qemu-img convert -l snapshot.name=s1 -O raw "$q/img.qcow2" "$q/s1.raw"
            cmp "$q/s1.raw" "$q/base.raw" || {
                echo "FAIL: the snapshot changed under the writes"; exit 1; }
        fi
        echo "qcow2, $kind: qemu-img check clean, the same as its reference"
    done
    case $(uname -s) in
    Linux)
        for i in 1 2 3 4 5 6; do
            set +e
            timeout -s KILL 0.0$i "$work/bin/qcow2test" --workload "$q/img.qcow2" \
              "$q/ref.raw" $((100 + i)) 1000000 >/dev/null 2>&1
            qemu-img check "$q/img.qcow2" >"$q/check.txt" 2>&1
            rc=$?
            set -e
            # 0: clean, 3: leaked clusters only; 2 would be corruption
            [ $rc -eq 0 ] || [ $rc -eq 3 ] || {
                cat "$q/check.txt"; echo "FAIL: a killed writer left the image corrupt"; exit 1; }
        done
        echo "qcow2: a writer killed six times left no errors"
        ;;
    esac
else
    echo "qcow2 writes: skipped, no qemu-img to make and check the images"
fi
nasm -f bin -o "$work/sys/xemu/mcpx_1.0.bin" "$here/rom/bootrom.asm"
cp "$work/sys/xemu/mcpx_1.0.bin" "$work/sysr/xemu/"
nasm -f bin -I"$here/rom/" -o "$work/sysr/xemu/Complex_4627v1.03.bin" "$here/rom/rep.asm"
cp "$work/sys/xemu/mcpx_1.0.bin" "$work/sysc/xemu/"
nasm -f bin -I"$here/rom/" -o "$work/sysc/xemu/Complex_4627v1.03.bin" "$here/rom/cf.asm"
cp "$work/sys/xemu/mcpx_1.0.bin" "$work/sysf/xemu/"
nasm -f bin -I"$here/rom/" -o "$work/sysf/xemu/Complex_4627v1.03.bin" "$here/rom/rep-stos.asm"
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
cp "$work/sys/xemu/mcpx_1.0.bin" "$work/sysh/xemu/"
nasm -f bin -DHDD -I"$here/rom/" -o "$work/sysh/xemu/Complex_4627v1.03.bin" "$here/rom/flash.asm"
cp "$work/sys/xemu/mcpx_1.0.bin" "$work/sysa/xemu/"
nasm -f bin -DACI -I"$here/rom/" -o "$work/sysa/xemu/Complex_4627v1.03.bin" "$here/rom/flash.asm"
cp "$work/sys/xemu/mcpx_1.0.bin" "$work/syse/xemu/"
nasm -f bin -DEP -I"$here/rom/" -o "$work/syse/xemu/Complex_4627v1.03.bin" "$here/rom/flash.asm"
cp "$work/sys/xemu/mcpx_1.0.bin" "$work/sysd/xemu/"
nasm -f bin -DDSP -DGP_PASSES=16 -I"$here/rom/" -o "$work/sysd/xemu/Complex_4627v1.03.bin" "$here/rom/flash.asm"

cp "$work/sys/xemu/mcpx_1.0.bin" "$work/syswork/xemu/"
nasm -f bin -DEXITWORK -I"$here/rom/" -o "$work/syswork/xemu/Complex_4627v1.03.bin" "$here/rom/flash.asm"

t() { echo "== $1"; shift; timeout 300 "$@"; }

# The plain test ROMs play their tone on the voice processor alone and never
# start the DSPs: their tests run without APU DSP Emulation, which is on by
# default where the core has the JIT (a game's DSP programs make its sound).
vp="--opt xemu_use_dsp=disabled"

# REP MOVS/STOS: per-element memory semantics, debug/RF behavior, pending IRQ
# after STI, a fault/restart and self-modifying/two-page code. The same
# architectural results must hold with and without CPU Boost chaining.
for op in movs stos; do
  case $op in movs) repsys="$work/sysr";; stos) repsys="$work/sysf";; esac
  for boost in enabled disabled; do
    t "REP $op, CPU Boost $boost" $h "$(path "$core")" "$(path "$repsys")" \
      $vp --opt "xemu_cpu_boost=$boost" --frames 20 --strict \
      --peek 0x300000 --peek 0x300004 --peek 0x300008 --peek 0x30000c \
      --peek 0x300014 --peek 0x300018 --peek 0x30001c \
      --peek 0x300024 >"$work/rep-$op-$boost.txt"
    for want in "peek_0x300000=0x5245504f" "peek_0x300004=0x00000000" \
                "peek_0x300008=0x00000005" "peek_0x30000c=0x00000001" \
                "peek_0x300014=0x000fffff" "peek_0x300018=0x00000001" \
                "peek_0x30001c=0x00000008" \
                "peek_0x300024=0x00000005"; do
        if ! grep -q "$want" "$work/rep-$op-$boost.txt"; then
            cat "$work/rep-$op-$boost.txt"
            echo "FAIL: REP $op ($boost): expected $want"
            exit 1
        fi
    done
  done
done

# CF across block boundaries: every flag-setting instruction, then a jump,
# then a block that reads CF before setting the flags (rom/cf.asm). With CPU
# Boost the next block knows the class of CC_OP it starts with (its TB
# flags) and computes CF inline; without, helper_cc_compute_c does. Both
# must see the CF the instruction left.
for boost in enabled disabled; do
  t "CF across blocks, CPU Boost $boost" $h "$(path "$core")" "$(path "$work/sysc")" \
    $vp --opt "xemu_cpu_boost=$boost" --frames 30 --strict \
    --peek 0x300000 --peek 0x300004 >"$work/cf-$boost.txt"
  if ! grep -q "peek_0x300000=0x4b4f4643" "$work/cf-$boost.txt"; then
    cat "$work/cf-$boost.txt"
    echo "FAIL: CF across blocks ($boost): case $(grep -o 'peek_0x300004=.*' "$work/cf-$boost.txt")"
    exit 1
  fi
done

# Active register-only work crossing STI/CLI exits must not be parked.
for boost in enabled disabled; do
  t "active STI/CLI work, CPU Boost $boost" $h "$(path "$core")" "$(path "$work/syswork")" \
    $vp --opt "xemu_cpu_boost=$boost" --frames 20 --strict \
    --peek 0x300000 --peek 0x300004 >"$work/exitwork-$boost.txt"
  for want in "peek_0x300000=0x574f524b" "peek_0x300004=0x00010000"; do
    if ! grep -q "$want" "$work/exitwork-$boost.txt"; then
      cat "$work/exitwork-$boost.txt"
      echo "FAIL: active STI/CLI work ($boost): expected $want"; exit 1
    fi
  done
done

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
# access, rewinding its block). Each turn of that poll also stores the value
# to the stack, as compiled game code does; taken for activity, that ran
# the ROM at 0.08x.
# At frame 60 a swizzled 32x16 surface is cleared red on its left half and
# blue on its right, its pitch set to half a row, and at 61 the CPU checks
# every texel where the NV2A puts it: hardware ignores a swizzled surface's
# pitch (nxdk_pgraph_tests surface_pitch). Taken as the row, it folded the
# rows onto each other in the read-back (0x300024: wrong texels, 0). At 62
# the depth and stencil are cleared, and at 63 the CPU reads their first
# and last words back: the clear value, 0x12345678. At 70 the CPU writes
# the depth buffer and at 71 only the stencil is cleared: at 72 the CPU
# reads the CPU's depth with the new stencil, 0xaabbcc55 (0x300034; on
# Vulkan through the unpack and pack shaders). A second swizzled surface,
# drawn at frame 5 and left alone, is evicted at 60 frames while dirty and
# read back without waiting (OpenGL: a PBO the binding parks with); at 75
# the CPU checks all its texels, which takes that read-back (0x300038: 0).
# At 64 the depth and stencil are cleared with their write masks off: the
# NV2A clears regardless (nxdk_pgraph_tests Clear on hardware), so the CPU
# reads the clear value at 65 (0x30003c: 0x33333344, not the old value).
# On Vulkan the front buffer was dropped as an old surface and a CPU write
# to it was not a new picture: the display stopped after the first frame
# (Jet Set Radio Future black after the Xbox logo).
# From frame 105 surface D is shown, each frame a black clear, a triangle
# (black), a clear of a corner's red, green and blue (drawn by the clear's
# own pipeline) and the same triangle: its centre stays black at 115. On
# Vulkan the second triangle reused the clear's pipeline and painted the
# triangle in the clear colour (white on lavapipe; Jet Set Radio Future
# crashed on it).
gpu_check() {
    for want in "pixel_10=0xff0000" "pixel_25=0xff0000" "pixel_45=0x00ff00" \
                "pixel_85=0x00ff00" "peek_0x300014=0xffff0000" \
                "peek_0x300018=0xffff0000" "peek_0x30001c=0x00000004" \
                "peek_0x300024=0x00000000" "peek_0x30002c=0x12345678" \
                "peek_0x300030=0x12345678" "peek_0x300034=0xaabbcc55" \
                "peek_0x300038=0x00000000" "peek_0x30003c=0x33333344" \
                "pixel_115=0x000000" "peek_0x300040=0x0000007f" \
                "pixel_130_4_20=0x0000ff" "pixel_130_320_100=0xff0000" \
                "pixel_130_320_350=0x00ff00" "pixel_130_4_100=0x0000ff" \
                "pixel_145_4_20=0xffff00" "pixel_165_320_120=0x00ffff" \
                "pixel_180=0x00ff00" "pixel_240_320_120=0x00ffff" "pixel_270_320_100=0xff0000" "pixel_270_4_20=0x0000ff" "pixel_300_320_100=0xff0000" "pixel_300_4_20=0x0000ff" "pixel_325_320_100=0xff0000" "pixel_345_320_100=0xff0000" "pixel_375_320_120=0x00ffff" "pixel_390_320_316=0x00ff00" "pixel_420_320_324=0xff0000" "pixel_436_320_120=0x800080" "pixel_460_320_120=0xff0000" "pixel_460_4_20=0x800080" "pixel_150_4_20=0xffffff" "pixel_150_639_479=0x00ffff"; do
        grep -E "^$want$" "$1" >/dev/null || {
            echo "FAIL: $2: $want expected, $(grep "^${want%%=*}=" "$1")"; exit 1; }
    done
}
echo "== the display, OpenGL"
timeout 300 $h "$(path "$core")" "$(path "$work/sysg")" $vp --frames 485 --strict \
  --min-speed 0.2 --pixel 10 --pixel 25 --pixel 45 --pixel 85 --pixel 115 --pixel 130:4:20 --pixel 130:320:100 --pixel 130:320:350 \
  --pixel 130:4:100 --pixel 145:4:20 --pixel 165:320:120 --pixel 180 \
  --pixel 240:320:120 --pixel 270:320:100 --pixel 270:4:20 --pixel 300:320:100 --pixel 300:4:20 --pixel 325:320:100 --pixel 345:320:100 --pixel 375:320:120 --pixel 390:320:316 --pixel 420:320:324 --pixel 436:320:120 --pixel 460:320:120 --pixel 460:4:20 --pixel 470:32:120 --pixel 470:10:120 --pixel 480:10:120 --pixel 484:10:120 --pixel 150:4:20 --pixel 150:639:479 --peek 0x300040 --peek 0x300014 --peek 0x300018 \
  --peek 0x30001c --peek 0x300024 --peek 0x300028 --peek 0x30002c \
  --peek 0x300030 --peek 0x300034 --peek 0x300038 --peek 0x30003c \
  >"$work/gpu_gl.txt"
grep -E "pixel_|peek_" "$work/gpu_gl.txt"
gpu_check "$work/gpu_gl.txt" "OpenGL"
echo "== the display, Vulkan"
set +e
timeout 300 $h "$(path "$core")" "$(path "$work/sysg")" $vp --vulkan \
  --opt xemu_renderer=vulkan --frames 485 --strict \
  --pixel 10 --pixel 25 --pixel 45 --pixel 85 --pixel 115 --pixel 130:4:20 --pixel 130:320:100 --pixel 130:320:350 \
  --pixel 130:4:100 --pixel 145:4:20 --pixel 165:320:120 --pixel 180 \
  --pixel 240:320:120 --pixel 270:320:100 --pixel 270:4:20 --pixel 300:320:100 --pixel 300:4:20 --pixel 325:320:100 --pixel 345:320:100 --pixel 375:320:120 --pixel 390:320:316 --pixel 420:320:324 --pixel 436:320:120 --pixel 460:320:120 --pixel 460:4:20 --pixel 470:32:120 --pixel 470:10:120 --pixel 480:10:120 --pixel 484:10:120 --pixel 150:4:20 --pixel 150:639:479 --peek 0x300040 --peek 0x300014 --peek 0x300018 \
  --peek 0x30001c --peek 0x300024 --peek 0x300028 --peek 0x30002c \
  --peek 0x300030 --peek 0x300034 --peek 0x300038 --peek 0x30003c \
  >"$work/gpu_vk.txt"
r=$?
set -e
case $r in
0) grep -E "pixel_|peek_" "$work/gpu_vk.txt"; gpu_check "$work/gpu_vk.txt" "Vulkan"
   for want in "pixel_470_32_120=0xffffff" "pixel_470_10_120=0x800080" \
               "pixel_480_10_120=0xffffff" "pixel_484_10_120=0xffffff"; do
     grep -q "^$want$" "$work/gpu_vk.txt" || { echo "FAIL: Vulkan overlay: $want"; exit 1; }
   done
   echo "== GPU byte reinterpretation, Vulkan"
   timeout 300 $h "$(path "$core")" "$(path "$work/sysg")" $vp --vulkan \
     --opt xemu_renderer=vulkan --frames 655 --strict \
     --pixel 510:32:120 --pixel 510:320:120 --pixel 530:100:210 \
     --pixel 550:32:210 --pixel 550:33:210 --pixel 570:32:210 --pixel 570:33:210 \
     --pixel 590:100:210 --pixel 610:100:210 --pixel 630:100:210 \
     --pixel 650:200:150 --pixel 650:100:151 --pixel 650:400:151 >"$work/gpu_bytes.txt"
   for want in "pixel_510_32_120=0x39a5c7" "pixel_510_320_120=0xda630b" \
               "pixel_530_100_210=0xa539a5" "pixel_550_32_210=0x393429" \
               "pixel_550_33_210=0x7b8200" "pixel_570_32_210=0x736b29" \
               "pixel_570_33_210=0xff0000" "pixel_590_100_210=0xc6ae39" \
               "pixel_610_100_210=0xa5c7a5" "pixel_630_100_210=0x5a5a5a" \
               "pixel_650_200_150=0xa539a5" "pixel_650_100_151=0xa539a5" \
               "pixel_650_400_151=0xff0000"; do
     grep -q "^$want$" "$work/gpu_bytes.txt" || { echo "FAIL: byte reinterpretation: $want"; exit 1; }
   done ;;
3) echo "no Vulkan device: skipped" ;;
*) echo "FAIL: the Vulkan run exited $r"; exit 1 ;;
esac

# The display test's read-backs at twice the internal resolution, where
# each is shrunk to the Xbox's size on the way back (OpenGL: a swizzled
# surface is shrunk and put in its swizzled order in one GPU pass).
for r in OpenGL Vulkan; do
    o=; [ $r = Vulkan ] && o="--vulkan --opt xemu_renderer=vulkan"
    echo "== the display at 2x, $r"
    set +e
    timeout 300 $h "$(path "$core")" "$(path "$work/sysg")" $vp $o \
      --opt xemu_surface_scale=2 --frames 485 --strict \
      --pixel 130:8:40 --pixel 130:640:200 --pixel 130:640:700 \
      --pixel 165:640:240 --pixel 240:640:240 --pixel 270:640:200 --pixel 270:8:40 --pixel 300:640:200 --pixel 300:8:40 --pixel 325:640:200 --pixel 345:640:200 --pixel 375:640:240 --pixel 390:640:632 --pixel 420:640:648 --pixel 436:640:240 --pixel 460:640:240 --pixel 460:8:40 --pixel 470:64:240 --pixel 470:20:240 --pixel 480:20:240 --pixel 484:20:240 --pixel 150:8:40 --pixel 150:1278:958 --peek 0x300014 \
      --peek 0x300018 --peek 0x300024 --peek 0x30002c --peek 0x300034 \
      --peek 0x300038 >"$work/gpu_2x.txt"
    a=$?
    set -e
    if [ $a = 3 ]; then echo "no Vulkan device: skipped"; continue; fi
    [ $a = 0 ] || { echo "FAIL: $r at 2x: the run exited $a"; exit 1; }
    if [ $a = 0 ]; then
      for want in "pixel_130_8_40=0x0000ff" "pixel_130_640_200=0xff0000" \
                  "pixel_130_640_700=0x00ff00" "pixel_165_640_240=0x00ffff" \
                  "pixel_240_640_240=0x00ffff" "pixel_270_640_200=0xff0000" "pixel_270_8_40=0x0000ff" "pixel_300_640_200=0xff0000" "pixel_300_8_40=0x0000ff" "pixel_325_640_200=0xff0000" "pixel_345_640_200=0xff0000" "pixel_375_640_240=0x00ffff" "pixel_390_640_632=0x00ff00" "pixel_420_640_648=0xff0000" "pixel_436_640_240=0x800080" "pixel_460_640_240=0xff0000" "pixel_460_8_40=0x800080" "pixel_150_8_40=0xffffff" "pixel_150_1278_958=0x00ffff"; do
        grep -q "^$want$" "$work/gpu_2x.txt" || {
          cat "$work/gpu_2x.txt"; echo "FAIL: scaled scanout: $want"; exit 1; }
      done
    fi
    if [ "$r" = Vulkan ]; then
      for want in "pixel_470_64_240=0xffffff" "pixel_470_20_240=0x800080" \
                  "pixel_480_20_240=0xffffff" "pixel_484_20_240=0xffffff"; do
        grep -q "^$want$" "$work/gpu_2x.txt" || { echo "FAIL: Vulkan 2x overlay: $want"; exit 1; }
      done
    fi
    grep -E "peek_" "$work/gpu_2x.txt"
    for want in "peek_0x300014=0xffff0000" "peek_0x300018=0xffff0000" \
                "peek_0x300024=0x00000000" "peek_0x30002c=0x12345678" \
                "peek_0x300034=0xaabbcc55" "peek_0x300038=0x00000000"; do
        grep -E "^$want$" "$work/gpu_2x.txt" >/dev/null || {
            echo "FAIL: $r at 2x: $want expected, $(grep "^${want%%=*}=" "$work/gpu_2x.txt")"; exit 1; }
    done
done

# Compatible surface reuse and incompatible byte-layout texture aliases.
for api in OpenGL Vulkan; do
  o=; [ "$api" = Vulkan ] && o="--vulkan --opt xemu_renderer=vulkan"
  for scale in 1 2 4; do
    set +e
    timeout 300 $h "$(path "$core")" "$(path "$work/sysg")" $vp $o \
      --opt xemu_memory=64 --opt xemu_surface_scale=$scale --frames 1480 --strict \
      --pixel 690 --pixel 710 --pixel 730 --pixel 750 \
      --pixel 770 --pixel 790 --pixel 810 \
      --pixel 830 --pixel 850 --pixel 870 --pixel 890 --pixel 910 \
      --pixel 930 --pixel 950 --pixel 970 --pixel 990 --pixel 1005 --pixel 1030 --pixel 1050 --pixel 1070 \
      --pixel 1090 --pixel 1110 --pixel 1130 --pixel 1150 --pixel 1170 --pixel 1190 \
      --pixel 1210 --pixel 1230 --pixel 1250 --pixel 1270 --pixel 1290 --pixel 1310 --pixel 1330 --pixel 1350 --pixel 1370 --pixel 1410 --pixel 1430 --pixel 1470 \
      --peek 0x300008 --peek 0x300014 >"$work/gpu_texture_alias.txt"
    r=$?
    set -e
    [ "$r" = 3 ] && [ "$api" = Vulkan ] && continue
    [ "$r" = 0 ] || { echo "FAIL: $api texture alias ${scale}x exited $r"; exit 1; }
    for want in "pixel_690=0xff0000" "pixel_710=0x000000" \
                "pixel_730=0xffffff" "pixel_750=0xffffff" \
                "pixel_770=0x001cff" "pixel_790=0xffe300" \
                  "pixel_810=0xffffff" "pixel_830=0xffffff" \
                "pixel_850=0x000000" "pixel_870=0x000000" \
                "pixel_890=0xffffff" "pixel_910=0x000000" \
                "pixel_930=0xff0000" "pixel_950=0x0000ff" \
                "pixel_970=0x0000ff" "pixel_990=0x0000ff" \
                "pixel_1005=0x0000ff" "pixel_1030=0x00ff00" \
                "pixel_1050=0xffffff" "pixel_1070=0xffffff" \
                "pixel_1090=0x000000" "pixel_1110=0xffffff" \
                "pixel_1130=0xff0000" "pixel_1150=0x00ff00" \
                "pixel_1170=0xffffff" "pixel_1190=0xffffff" \
                "pixel_1210=0x828282" "pixel_1230=0xff1b82" \
                "pixel_1250=0x828282" "pixel_1270=0xff1b82" \
                "pixel_1290=0xffffff" "pixel_1310=0x00ff00" "pixel_1330=0xffffff" \
                "pixel_1350=0x0000ff" "pixel_1370=0x0000ff" "pixel_1410=0x0000ff" \
                "pixel_1430=0x0000ff" "pixel_1470=0x0000ff" \
                "peek_0x300014=0xff0000ff" \
                "peek_0x300008=0x00000000"; do
      grep -q "^$want$" "$work/gpu_texture_alias.txt" || {
        echo "FAIL: $api texture alias ${scale}x: $want"; exit 1; }
    done
  done
done

# A retired GPU-only source must survive a snapshot before its CPU reader.
set +e
timeout 300 $h "$(path "$core")" "$(path "$work/sysg")" $vp \
  --vulkan --opt xemu_renderer=vulkan --opt xemu_memory=64 \
  --opt xemu_surface_scale=1 --frames 1540 --strict --save 1447 --load 1470 \
  --verify-load --pixel 1530 --peek 0x300014 >"$work/gpu_expired_state.txt"
r=$?
set -e
if [ "$r" = 0 ]; then
  grep -q '^pixel_1530=0x0000ff$' "$work/gpu_expired_state.txt"
  grep -q '^peek_0x300014=0xff0000ff$' "$work/gpu_expired_state.txt"
elif [ "$r" != 3 ]; then
  echo "FAIL: retired Vulkan surface snapshot exited $r"; exit 1
fi

# Restore an earlier alias snapshot after a different GPU source redraw.
for api in OpenGL Vulkan; do
  o=; [ "$api" = Vulkan ] && o="--vulkan --opt xemu_renderer=vulkan"
set +e
timeout 300 $h "$(path "$core")" "$(path "$work/sysg")" $vp $o \
  --frames 820 --strict --save 705 --load 785 \
  --verify-load --pixel 710 --pixel 770 --pixel 790 --pixel 810 \
  >"$work/gpu_texture_alias_state.txt"
r=$?
set -e
if [ "$r" = 0 ]; then
  for want in "pixel_710=0x000000" "pixel_770=0x001cff" \
              "pixel_790=0x000000" "pixel_810=0xffffff"; do
    grep -q "^$want$" "$work/gpu_texture_alias_state.txt" || {
      echo "FAIL: $api texture alias state: $want"; exit 1; }
  done
elif [ "$r" != 3 ] || [ "$api" != Vulkan ]; then
  echo "FAIL: $api texture alias state exited $r"; exit 1
fi
done

# A 4x capture exceeds the old fixed 1920x1080 readback allocation. A
# timing-only run at the same size must not allocate capture buffers at all.
set +e
timeout 300 $h "$(path "$core")" "$(path "$work/sysg")" $vp --vulkan \
  --opt xemu_renderer=vulkan --opt xemu_surface_scale=4 --frames 150 --strict \
  --pixel 130:1280:400 >"$work/gpu_4x_capture.txt"
r=$?
set -e
if [ "$r" = 0 ]; then
  grep -q '^pixel_130_1280_400=0xff0000$' "$work/gpu_4x_capture.txt"
  grep -q '^vulkan_readback_allocations=3 max_bytes=19660800$' "$work/gpu_4x_capture.txt"
  timeout 300 $h "$(path "$core")" "$(path "$work/sysg")" $vp --vulkan \
    --opt xemu_renderer=vulkan --opt xemu_surface_scale=4 --frames 40 --strict \
    --stutter >"$work/gpu_4x_timing.txt"
  grep -q '^vulkan_readback_allocations=0 max_bytes=0$' "$work/gpu_4x_timing.txt"
elif [ "$r" != 3 ]; then
  echo "FAIL: Vulkan 4x capture exited $r"; exit 1
fi

# A copied RGB565 texture outlives five-frame source expiry, then changes sampler.
set +e
timeout 300 $h "$core" "$work/sysg" $vp --vulkan --opt xemu_renderer=vulkan \
  --opt xemu_memory=64 --opt xemu_surface_scale=1 --frames 1780 --strict \
  --pixel 1670 --pixel 1710 --pixel 1770 --peek 0x300044 >"$work/gpu_copied_source.txt"
r=$?
set -e
if [ "$r" = 0 ]; then
  for want in "pixel_1670=0x001cff" "pixel_1710=0x001cff" \
              "pixel_1770=0x001cff" "peek_0x300044=0xff0000ff"; do
    grep -q "^$want$" "$work/gpu_copied_source.txt" || {
      echo "FAIL: copied source: $want"; exit 1;
    }
  done
elif [ "$r" != 3 ]; then
  echo "FAIL: copied source exited $r"; exit 1
fi

# All six GPU-produced cube faces survive a sampler change and later CPU reads.
set +e
timeout 300 $h "$core" "$work/sysg" $vp --vulkan --opt xemu_renderer=vulkan \
  --opt xemu_memory=64 --opt xemu_surface_scale=1 --frames 1920 --strict \
  --pixel 1810 --pixel 1850 --pixel 1910 --peek 0x300048 --peek 0x30004c \
  --peek 0x300050 --peek 0x300054 --peek 0x300058 --peek 0x30005c \
  >"$work/gpu_cube_source.txt"
r=$?
set -e
if [ "$r" = 0 ]; then
  for want in "pixel_1810=0xff0000" "pixel_1850=0xff0000" \
              "pixel_1910=0xff0000" "peek_0x300048=0xffff0000" \
              "peek_0x30004c=0xff0000ff" "peek_0x300050=0xff0000ff" \
              "peek_0x300054=0xff0000ff" "peek_0x300058=0xff0000ff" \
              "peek_0x30005c=0xff0000ff"; do
    grep -q "^$want$" "$work/gpu_cube_source.txt" || {
      echo "FAIL: cube source: $want"; exit 1;
    }
  done
elif [ "$r" != 3 ]; then
  echo "FAIL: cube source exited $r"; exit 1
fi

# D16 and D24 GPU depth copies survive filter changes and later CPU reads.
set +e
timeout 300 $h "$core" "$work/sysg" $vp --vulkan --opt xemu_renderer=vulkan \
  --opt xemu_memory=64 --opt xemu_surface_scale=2 --frames 2240 --strict \
  --pixel 1970 --pixel 2010 --pixel 2070 --pixel 2130 --pixel 2170 --pixel 2230 \
  --peek 0x300060 --peek 0x300064 >"$work/gpu_depth_source.txt"
r=$?
set -e
if [ "$r" = 0 ]; then
  for want in "pixel_1970=0xffffff" "pixel_2010=0xffffff" \
              "pixel_2070=0xffffff" "pixel_2130=0xffffff" \
              "pixel_2170=0xffffff" "pixel_2230=0xffffff" \
              "peek_0x300060=0x80008000" "peek_0x300064=0x80000000"; do
    grep -q "^$want$" "$work/gpu_depth_source.txt" || {
      echo "FAIL: depth source: $want"; exit 1;
    }
  done
elif [ "$r" != 3 ]; then
  echo "FAIL: depth source exited $r"; exit 1
fi

# A retired GPU depth snapshot becomes RAM data, then CPU/GPU rewrite it.
set +e
timeout 300 $h "$core" "$work/sysg" $vp --vulkan --opt xemu_renderer=vulkan \
  --opt xemu_memory=64 --opt xemu_surface_scale=2 --frames 2420 --strict \
  --pixel 2290 --pixel 2330 --pixel 2350 --pixel 2370 --pixel 2410 \
  --peek 0x300068 --peek 0x30006c >"$work/gpu_depth_transition.txt"
r=$?
set -e
if [ "$r" = 0 ]; then
  for want in "pixel_2290=0xffffff" "pixel_2330=0x000000" \
              "pixel_2350=0x000000" "pixel_2370=0xffffff" \
              "pixel_2410=0xffffff" "peek_0x300068=0x80008000" \
              "peek_0x30006c=0x00000000"; do
    grep -q "^$want$" "$work/gpu_depth_transition.txt" || {
      echo "FAIL: depth transition: $want"; exit 1;
    }
  done
elif [ "$r" != 3 ]; then
  echo "FAIL: depth transition exited $r"; exit 1
fi

# Retired GPU cube -> RAM: independent CPU face edits with unchanged bindings.
for cube_api in gl vk; do
  cube_api_args="--opt xemu_renderer=opengl"
  if [ "$cube_api" = vk ]; then
    cube_api_args="--vulkan --opt xemu_renderer=vulkan"
  fi
  set +e
  timeout 300 $h "$core" "$work/sysg" $vp $cube_api_args \
    --opt xemu_memory=64 --opt xemu_surface_scale=1 --frames 2660 --strict \
    --pixel 2490 --pixel 2530 --pixel 2550 --pixel 2570 --pixel 2590 \
    --pixel 2610 --pixel 2650 --peek 0x300070 --peek 0x300074 --peek 0x300078 \
    >"$work/gpu_cube_transition_$cube_api.txt"
  r=$?
  set -e
  if [ "$r" = 0 ]; then
    for want in "pixel_2490=0xff0000" "pixel_2530=0x00ff00" \
                "pixel_2550=0x00ff00" "pixel_2570=0x0000ff" \
                "pixel_2590=0xffff00" "pixel_2610=0x00ff00" \
                "pixel_2650=0x00ff00" "peek_0x300070=0xff00ff00" \
                "peek_0x300074=0xffffff00" "peek_0x300078=0xff0000ff"; do
      grep -q "^$want$" "$work/gpu_cube_transition_$cube_api.txt" || {
        echo "FAIL: $cube_api cube transition: $want"; exit 1;
      }
    done
  elif [ "$r" != 3 ]; then
    echo "FAIL: $cube_api cube transition exited $r"; exit 1
  fi
done

# One GPU cube face combined with RAM faces; subsequent CPU and GPU edits.
set +e
timeout 300 $h "$core" "$work/sysg" $vp --vulkan --opt xemu_renderer=vulkan \
  --opt xemu_memory=64 --opt xemu_surface_scale=1 --frames 2840 --strict \
  --pixel 2730 --pixel 2742 --pixel 2750 --pixel 2770 --pixel 2790 --pixel 2830 \
  --peek 0x30007c --peek 0x300080 --peek 0x300084 >"$work/gpu_mixed_cube.txt"
r=$?
set -e
if [ "$r" = 0 ]; then
  for want in "pixel_2730=0xff00ff" "pixel_2742=0x00ff00" \
              "pixel_2750=0xffff00" "pixel_2770=0x00ffff" \
              "pixel_2790=0xffff00" "pixel_2830=0xffff00" \
              "peek_0x30007c=0xff00ffff" "peek_0x300080=0xffffff00" \
              "peek_0x300084=0xff0000ff"; do
    grep -q "^$want$" "$work/gpu_mixed_cube.txt" || {
      echo "FAIL: mixed cube: $want"; exit 1;
    }
  done
elif [ "$r" != 3 ]; then
  echo "FAIL: mixed cube exited $r"; exit 1
fi

# A savestate taken while surfaces live only on the GPU (rom/gpu.c: before
# run 64, B drawn and its depth cleared at frame 62, nothing read back), then
# loaded in a new process, which has no GPU surfaces: the depth the guest
# reads at frame 63 is the clear value only if the save wrote every dirty
# surface into RAM. On Vulkan those read-backs go to the GPU together, one
# submission and one wait.
for r in OpenGL Vulkan; do
    o=; [ $r = Vulkan ] && o="--vulkan --opt xemu_renderer=vulkan"
    echo "== surfaces saved from the GPU, $r"
    set +e
    timeout 300 $h "$(path "$core")" "$(path "$work/sysg")" $vp $o --frames 66 \
      --save 64 --state-out "$(path "$work/gpu.state")" >/dev/null
    a=$?
    timeout 300 $h "$(path "$core")" "$(path "$work/sysg")" $vp $o --frames 20 \
      --load 1 --state-in "$(path "$work/gpu.state")" --peek 0x30002c \
      --peek 0x300030 >"$work/gpu_state.txt"
    b=$?
    set -e
    if [ $a = 3 ] || [ $b = 3 ]; then echo "no Vulkan device: skipped"; continue; fi
    [ $a = 0 ] && [ $b = 0 ] || { echo "FAIL: $r: the runs exited $a, $b"; exit 1; }
    grep -E "peek_" "$work/gpu_state.txt"
    for want in "peek_0x30002c=0x12345678" "peek_0x300030=0x12345678"; do
        grep -E "^$want$" "$work/gpu_state.txt" >/dev/null || {
            echo "FAIL: $r: $want expected after the load, $(grep "^${want%%=*}=" "$work/gpu_state.txt")"; exit 1; }
    done
done

# Save the composed (cyan/green) frame, switch modes and evict its images,
# then load it back in the same process: no cached pre-load pixels/keys survive.
for r in OpenGL Vulkan; do
    o=; [ $r = Vulkan ] && o="--vulkan --opt xemu_renderer=vulkan"
    echo "== offset composition after a state load, $r"
    set +e
    timeout 300 $h "$(path "$core")" "$(path "$work/sysg")" $vp $o --frames 400 \
      --save 240 --load 390 --verify-load --state-out "$(path "$work/scanout.state")" \
      --pixel 395:320:120 --pixel 395:320:350 >"$work/scanout_state.txt"
    a=$?
    set -e
    if [ $a = 3 ]; then echo "no Vulkan device: skipped"; continue; fi
    [ $a = 0 ] || { echo "FAIL: $r: composed state load exited $a"; exit 1; }
    for want in "pixel_395_320_120=0x00ffff" "pixel_395_320_350=0x00ff00"; do
        grep -q "^$want$" "$work/scanout_state.txt" || {
            echo "FAIL: $r: $want after the composed state load"; exit 1; }
    done
done

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

# The AC97 controller's PCM out (rom/aci.c): its samples are dropped, but
# how fast it takes them is the guest's to see. A second of guest time
# takes 96000 16-bit samples (48 kHz stereo), within one 10 ms audio tick
# (960), the buffer-complete status is set and the DMA has not halted.
echo "== the AC97 controller's DMA in guest time"
timeout 300 $h "$(path "$core")" "$(path "$work/sysa")" $vp --frames 200 --strict \
  --peek 0x300008 --peek 0x30000c --peek 0x300010 --peek 0x300014 >"$work/aci.txt"
grep -E "speed=|peek_" "$work/aci.txt"
grep -E "peek_0x300008=0x00000000$" "$work/aci.txt" >/dev/null || {
    echo "FAIL: the AC97 test did not finish: $(grep peek_0x300008 "$work/aci.txt")"; exit 1; }
n=$(( $(sed -n 's/^peek_0x30000c=//p' "$work/aci.txt") ))
[ "$n" -ge 95040 ] && [ "$n" -le 96960 ] || {
    echo "FAIL: AC97 took $n samples in a second of guest time, not 96000"; exit 1; }
sr=$(( $(sed -n 's/^peek_0x300010=//p' "$work/aci.txt") ))
[ $((sr & 8)) -ne 0 ] && [ $((sr & 1)) -eq 0 ] || {
    echo "FAIL: AC97 PCM out status $sr: no buffer completed, or halted"; exit 1; }

# The same across a state saved and loaded while it runs (save at 30,
# load at 60): the controller's position and pacing come back with the
# state. They were not saved, and after the load the DMA was half a second
# ahead of the guest: 143936 samples in its second, run off its last buffer.
echo "== the AC97 controller's DMA across save and load"
timeout 300 $h "$(path "$core")" "$(path "$work/sysa")" $vp --frames 200 --strict \
  --save 30 --load 60 --verify-load --peek 0x300008 --peek 0x30000c \
  --peek 0x300010 >"$work/aci-load.txt"
grep -E "load at|peek_" "$work/aci-load.txt"
grep -E "peek_0x300008=0x00000000$" "$work/aci-load.txt" >/dev/null || {
    echo "FAIL: the AC97 test did not finish after the load"; exit 1; }
n=$(( $(sed -n 's/^peek_0x30000c=//p' "$work/aci-load.txt") ))
[ "$n" -ge 95040 ] && [ "$n" -le 96960 ] || {
    echo "FAIL: AC97 took $n samples in a second across a load, not 96000"; exit 1; }
sr=$(( $(sed -n 's/^peek_0x300010=//p' "$work/aci-load.txt") ))
[ $((sr & 8)) -ne 0 ] && [ $((sr & 1)) -eq 0 ] || {
    echo "FAIL: AC97 PCM out status $sr after the load: halted"; exit 1; }

# The hard disk (rom/hdd.c), both kinds of image: the raw xbox_hdd.img the
# core makes from the HDD it carries when there is none (8 GB, sparse),
# and an xbox_hdd.qcow2 (as standalone xemu and earlier builds have it),
# used as it is, without a copy. The guest writes 40 sectors past the
# partitions by DMA from two PRD entries, flushes, reads them back by DMA
# into three and one by PIO, and reads past the disk's end, which must
# fail: status 0, all 0x1480 words right, and the words are in the image
# where the sectors are (the qcow2's read by qemu-img, which must also find
# it clean). Each image twice: written, then used as it was left.
hdd_test() {
    sysdir=$1
    for pass in 1 2; do
        timeout 300 $h "$(path "$core")" "$(path "$sysdir")" $vp --frames 120 --strict \
          --peek 0x300008 --peek 0x30000c --peek 0x300010 >"$sysdir/hdd-rw$pass.txt" 2>&1
        grep -E "peek_|bundled" "$sysdir/hdd-rw$pass.txt"
        grep -E "peek_0x300008=0x00000000$" "$sysdir/hdd-rw$pass.txt" >/dev/null &&
        grep -E "peek_0x30000c=0x00001480$" "$sysdir/hdd-rw$pass.txt" >/dev/null || {
            echo "FAIL: the HDD's writes and reads: step $(grep peek_0x300008 "$sysdir/hdd-rw$pass.txt"), first wrong word at $(grep peek_0x300010 "$sysdir/hdd-rw$pass.txt")"; exit 1; }
    done
}
echo "== the HDD, a raw image the core makes: written and read by the guest"
img="$work/sysh/xemu/xbox_hdd.img"
rm -f "$work/sysh/xemu/xbox_hdd."*
hdd_test "$work/sysh"
grep "wrote the bundled one" "$work/sysh/hdd-rw1.txt" >/dev/null || {
    echo "FAIL: no HDD was made"; exit 1; }
if grep "wrote the bundled one" "$work/sysh/hdd-rw2.txt"; then
    echo "FAIL: the HDD was made again"; exit 1
fi
[ -f "$img" ] || { echo "FAIL: the new HDD is not a raw xbox_hdd.img"; exit 1; }
[ "$(wc -c < "$img")" -eq 8589934592 ] || {
    echo "FAIL: the raw HDD is $(wc -c < "$img") bytes, not 8 GB"; exit 1; }
words=$(od -An -tx4 -j 8588886016 -N 8 "$img" | tr -s ' ')
[ "$words" = " c8400000 c8400001" ] || {
    echo "FAIL: the guest's sectors are not in the raw image ($words)"; exit 1; }
case $core in
*.so)
    kb=$(du -k "$img" | cut -f1)
    echo "the raw HDD takes $kb KB on the disk"
    [ "$kb" -lt 65536 ] || { echo "FAIL: the raw HDD is not sparse ($kb KB)"; exit 1; }
    ;;
esac
echo "== the HDD, a qcow2 image: written and read by the guest in place"
mkdir -p "$work/sysq/xemu"
cp "$work/sysh/xemu/mcpx_1.0.bin" "$work/sysh/xemu/Complex_4627v1.03.bin" "$work/sysq/xemu/"
rm -f "$work/sysq/xemu/xbox_hdd."*
cp "$here/../../data/xbox_hdd.qcow2" "$work/sysq/xemu/xbox_hdd.qcow2"
hdd_test "$work/sysq"
[ ! -e "$work/sysq/xemu/xbox_hdd.img" ] || {
    echo "FAIL: the qcow2 HDD was copied to a raw one"; exit 1; }
if command -v qemu-img >/dev/null 2>&1; then
    qemu-img check "$work/sysq/xemu/xbox_hdd.qcow2" || {
        echo "FAIL: qemu-img check of the qcow2 HDD the guest wrote"; exit 1; }
    rm -f "$work/sysq/hdd.raw"
    qemu-img convert -O raw "$work/sysq/xemu/xbox_hdd.qcow2" "$work/sysq/hdd.raw"
    words=$(od -An -tx4 -j 8588886016 -N 8 "$work/sysq/hdd.raw" | tr -s ' ')
    rm -f "$work/sysq/hdd.raw"
    [ "$words" = " c8400000 c8400001" ] || {
        echo "FAIL: the guest's sectors are not in the qcow2 image ($words)"; exit 1; }
fi

# A session killed with the game running (a crash, the frontend killed):
# the game may have been cut off partway through writing its cache in the
# HDD's cache partitions, and read it back broken at every boot after (Jet
# Set Radio Future and Ninja Gaiden Black black past the Xbox logo, with
# the qcow2 images of earlier builds). The mark beside
# the image (<image>.open) outlives the killed session, the next load
# empties X, Y and Z and says so, and a clean close takes the mark away.
# Linux only: a killed Wine launcher may leave the process behind it.
case $core in
*.so)
    echo "== a killed session, then the next load"
    # the image the earlier runs used: the raw one the core made, or a
    # qcow2 a work dir from before kept
    hdd="$work/sys/xemu/xbox_hdd.img"
    [ -f "$hdd" ] || hdd="$work/sys/xemu/xbox_hdd.qcow2"
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
# where the core was built here. Reuse the core's platform and run a
# cross-compiled Windows helper under Wine, just like the harness.
if [ -f "$here/../../hw/xbox/nv2a/pgraph/spv/glslc.o" ]; then
    echo "== seed dictionary shaders"
    seedshaders_run() {
        case $core in
        *.dll)
            case $(uname -s) in
            MINGW*|MSYS*)
                make -s -C "$here/../.." platform=win seedshaders
                "$here/seedshaders.exe" "$(path "$here/../../data/shader_seeds.bin")"
                ;;
            *)
                make -s -C "$here/../.." platform=win \
                    CROSS_PREFIX="${CROSS_PREFIX:-x86_64-w64-mingw32-}" seedshaders
                wine "$here/seedshaders.exe" "$(path "$here/../../data/shader_seeds.bin")"
                ;;
            esac
            ;;
        *)
            make -s -C "$here/../.." seedshaders
            "$here/seedshaders" "$here/../../data/shader_seeds.bin"
            ;;
        esac
    }
    seedshaders_run >"$work/seedshaders.txt"
    tail -1 "$work/seedshaders.txt"
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

# The EP DSP's output (rom/ep.a56), which the core sinks to the frontend:
# its 256 samples a run go into the frontend's queue as the EP's DMA
# writes them. The audio is one unbroken ramp, every 16-bit sample the one
# before plus one, on the interpreter and the JIT, and through a reset
# (the EP's eight frames start again mid-run: the queue used to repeat 385
# samples there).
t "EP DSP, interpreter" $h "$(path "$core")" "$(path "$work/syse")" --frames 300 --strict \
  $dsp --opt xemu_dsp_jit=disabled --wav "$(path "$work/epi.wav")"
"$work/bin/rampcheck" "$work/epi.wav" --min 400000
t "EP DSP, JIT, through a reset" $h "$(path "$core")" "$(path "$work/syse")" --frames 300 --strict \
  $dsp --opt xemu_dsp_jit=enabled --reset 150 --wav "$(path "$work/epj.wav")"
"$work/bin/rampcheck" "$work/epj.wav" --min 400000

# Host volume integration: the EP ramp covers every S16 value. Compare
# captured output against exact decimal attenuation, including both signs,
# mute, and the actual option curve's Q16 gains (20/40/60/80 percent).
for jit in disabled enabled; do
  reference="$work/ep-volume-$jit-100.wav"
  t "EP volume reference, $jit" $h "$(path "$core")" "$(path "$work/syse")" \
    --frames 120 --strict $dsp --opt xemu_dsp_jit=$jit \
    --opt xemu_audio_volume=100 --wav "$(path "$reference")"
  for setting in 0:0 20:825 40:5430 60:16347 80:35731; do
    volume=${setting%:*}; gain=${setting#*:}
    capture="$work/ep-volume-$jit-$volume.wav"
    t "EP volume $volume percent, $jit" $h "$(path "$core")" "$(path "$work/syse")" \
      --frames 120 --strict $dsp --opt xemu_dsp_jit=$jit \
      --opt xemu_audio_volume=$volume --wav "$(path "$capture")"
    python3 "$here/test-audio-volume-wav.py" "$reference" "$capture" "$gain" --all-s16
  done
  # GP output applies gain directly; EP applies it while committing DMA.
  reference="$work/dspi.wav"
  if [ "$jit" = enabled ]; then
    reference="$work/gp-volume-enabled-100.wav"
    t "GP volume reference, JIT" $h "$(path "$core")" "$(path "$work/sysd")" \
      --frames 120 --strict $dsp --opt xemu_dsp_jit=enabled \
      --opt xemu_audio_volume=100 --wav "$(path "$reference")"
  fi
  capture="$work/gp-volume-$jit-40.wav"
  t "GP volume 40 percent, $jit" $h "$(path "$core")" "$(path "$work/sysd")" \
    --frames 120 --strict $dsp --opt xemu_dsp_jit=$jit \
    --opt xemu_audio_volume=40 --wav "$(path "$capture")"
  python3 "$here/test-audio-volume-wav.py" "$reference" "$capture" 5430
done

# Run-Ahead: after every run a save, hidden runs and a load, as RetroArch
# does. The sound has to be exactly the sound without it: the state carries
# the samples queued, the APU's engine phase, the voices' resamplers and
# the frame loop's grid. Before, every load cleared the queue to its
# preroll and restarted the voices, and the run after it played two
# frames' worth of the guest's time: the tone broke 24278 times.
echo "== Run-Ahead: the sound the same as without it"
timeout 300 $h "$(path "$core")" "$(path "$work/sys")" $vp --frames 200 --strict \
  --wav "$(path "$work/ra0.wav")" >/dev/null
for n in 1 2; do
  t "Run-Ahead $n" $h "$(path "$core")" "$(path "$work/sys")" $vp --frames 200 --strict \
    --runahead $n --wav "$(path "$work/ra$n.wav")"
  "$work/bin/sinecheck" "$work/ra$n.wav" --frames 160160
  cmp "$work/ra0.wav" "$work/ra$n.wav" || {
    echo "FAIL: the sound with Run-Ahead $n is not the sound without it"; exit 1; }
done
timeout 300 $h "$(path "$core")" "$(path "$work/sysd")" $dsp --opt xemu_dsp_jit=enabled \
  --frames 150 --strict --wav "$(path "$work/rad0.wav")" >/dev/null
t "Run-Ahead 1, GP DSP" $h "$(path "$core")" "$(path "$work/sysd")" $dsp --opt xemu_dsp_jit=enabled \
  --frames 150 --strict --runahead 1 --wav "$(path "$work/rad1.wav")"
cmp "$work/rad0.wav" "$work/rad1.wav" || {
  echo "FAIL: the GP DSP's sound with Run-Ahead is not the sound without it"; exit 1; }

# The main loop's step (util/main-loop.c, XBOX) runs the bottom halves and
# timers straight: no GLib main context and no poll. Before, every pass of
# the frame loop polled, about 30 ppoll a frame (8651 in 300 frames of the
# busy ROM). Over 300 frames of the busy and the display ROM the core polls
# at its start only. Linux, the Linux core: with strace (the Windows core
# under wine would count wine's own polls).
if command -v strace >/dev/null 2>&1 && [ "$(uname -s)" = Linux ] &&
   [ "${core##*.}" = so ]; then
  for rom in sysb sysg; do
    echo "== the main loop, $rom: no poll a frame"
    timeout 300 strace -f -c -e trace=poll,ppoll,select,pselect6,epoll_wait,epoll_pwait \
      -o "$work/polls-$rom.txt" $h "$(path "$core")" "$(path "$work/$rom")" $vp --frames 300 \
      >/dev/null
    n=$(awk '$NF ~ /^(poll|ppoll|select|pselect6|epoll_wait|epoll_pwait)$/ {s += $4} END {print s+0}' "$work/polls-$rom.txt")
    echo "polls: $n"
    [ "$n" -le 30 ] || { echo "FAIL: the core polled $n times in 300 frames"; exit 1; }
  done
fi

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
