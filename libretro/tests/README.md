# The core's tests

On Linux, `python3 libretro/tests/test-libretro-jit.py CHECKOUT [CC]`
compiles the production Apple Silicon JIT adapter against a strict mock of
libretro-common's nested write scopes, with UBSan. It checks initial execute,
repeated write/execute requests, calls from separate translation units, and
four independent threads while the main thread remains writable. `run.sh`
runs it for Linux cores. Real Apple API compilation is checked by macOS CI.

`python libretro/tests/test-texture-reinterpret.py CHECKOUT [CC]` compiles
the production GPU-alias eligibility gate and checks format/layout exclusions,
overflow boundaries, transfer-buffer capacity, native scratch availability
and dirty active/parked overlaps under the undefined-behavior sanitizer.
The GPU texture-alias phase also redraws the source blue and checks both
RGB565 halfwords (`001cff` and `ffe300`) at frames 770/790, covering channel
order and packed bit fields beyond all-zero/all-one texels.

The GPU ROM's texture-alias phase (frames 680 through 790) samples a red
GPU-produced 32x16 ARGB surface directly and through an incompatible 64x16
RGB565 view with the same pitch. Red's little-endian words become black and
white texels in the narrow view. A later GPU redraw must refresh the cached
aliased texture. The source RAM starts blue, so stale-RAM sampling cannot pass.
This phase initializes its transform and window-clip state explicitly and
exercises both compatible GPU reuse and the incompatible readback/upload path.
Vulkan uses a device-buffer reinterpretation for the supported live ARGB-to-
RGB565 layout; the fallback remains covered when the source has aged out.
The suite also restores an earlier alias state after a differently colored
GPU redraw and checks both the restored picture and subsequent sampling.

Vulkan captures allocate mapped readback buffers lazily per sync slot and
grow them to the actual output dimensions. Requested captures fail explicitly
if the image, size or allocation is invalid. Timing-only runs allocate no
readback buffers. The harness reports `vulkan_readback_allocations` and
`max_bytes` at teardown so tests can verify both paths.

`test-surface-clear.c` checks the full-color-clear upload predicate shared by
OpenGL and Vulkan against
200,000 per-pixel/channel overwrite cases, including partial channels,
partial rectangles, inclusive edges and empty surfaces. `run.sh` runs it.

After building the core, `make s3tctest && libretro/tests/s3tctest` checks
5,374,016 decoded components against an independent normalized BC1/2/3
reference: every per-channel color endpoint pair, both BC1 palette modes,
four-color BC2/3 in either endpoint order, all explicit alpha nibbles and
every BC3 alpha endpoint pair and selector, plus parity with the 3D decoder.
The reference was cross-checked
against Microsoft's DirectXTex decoder (BC.cpp, commit
`ec66b78b37389e8373a8e14ccc3cd29be94146b3`) using its original decode functions
and DirectXMath. Only the final normalized-to-byte conversion is quantized.

`test-audio-volume.c` checks host volume rounding against a floating-point
reference over every S16 sample at 257 gains, plus boundary samples at every
Q16 gain. It checks zero DC bias for symmetric inputs, half-LSB error bounds,
mute and exact preservation at unity. Build and run it from the repository
root with `cc -O2 -I. libretro/tests/test-audio-volume.c -lm -o test-audio-volume`
and `./test-audio-volume`.

`run.sh` runs `test-scanout-regions.c`: 12,000 per-pixel raster-reference
cases for the exact background complement, clipping, overlap, empty/full cover,
adjacent-strip coalescing, the eight-rectangle fallback and a scale-aware
draw-cost guard (64K removed output pixels per additional background draw).

Vulkan timing-only harness runs submit a completion marker without copying
image pixels. Readbacks prefer host-cached coherent memory, falling back to
coherent memory where unavailable. They occur only for requested hashing/pixel captures; GPU
completion intervals measure whole frames, not isolated shader execution.

`run.sh` also runs `test-scanout-bytes.c`: 6,000 per-pixel reference cases
for mapping packed byte spans to at most three rectangles, including padding,
clipping, alignment and overflow. The extended Vulkan GPU fixture checks
nonuniform Morton data, 16-to-32 and 32-to-16/15 reinterpretation, RGB1555/565
changes, R8/RG8 color bytes and mismatched row strides whose padding must
leave the older GPU layer visible. These are byte reinterpretations, not color blits.

`run.sh` also runs `test-scanout-background.c`: 2,500 independent dirty-byte,
pitch-padding, partial-edge, coverage and deferred-exposure cases. Sparse CPU
writes at opposite ends of the GPU fixture's framebuffer check partial uploads.

`run.sh` also runs `test-scanout.c`: 12,896 clipped linear mappings checked
against per-pixel guest-byte addresses, plus range boundaries, alignment,
pitch mismatch and overflow. The GPU ROM tests CPU-written borders around
two offset render surfaces, a later border write, swizzled byte readback
overlapping an older parked surface, switching to a RAM-only front buffer,
retaining the composed picture past surface eviction ages, and switching
between 32-, 15- and 16-bit display formats, returning to warmed modes,
returning after mode-cache eviction, and save/load invalidation of a
composed picture after later mode changes. Exact metadata cache tests also cover
unchanged content, tile changes, CPU writes, coverage changes, state-load
invalidation, and selective readback dependencies. The GPU fixture additionally checks the
32/33-layer layouts (Vulkan now draws both directly), return to sparse coverage,
and Vulkan keyed/unkeyed PVIDEO over both a GPU tile and CPU border, including
an overlay input resize. Both OpenGL
and Vulkan are checked at native and 2x scales. The harness accepts
`--pixel FRAME:X:Y` to sample a top-left-based physical coordinate, including
on repeated frames; `--pixel FRAME` retains its centre-pixel behavior.

`test-glib.c` checks the core's GLib (`libretro/glib`) on its own, against
what GLib does: hash tables (removal while iterating included), balanced
trees, lists, queues, sequences, arrays, strings, base64 and SHA-1, paths,
files and directories, and a main context driven as QEMU's main loop drives
it (prepare, query, check, dispatch; idle and timeout sources). `run.sh`
runs it natively, and with the Windows core under Wine its Windows build
too.

`test-qcow2.c` checks the HDD's qcow2 code on its own: the bundled image
written out raw (`block/qcow2-raw.c`) against `qemu-img convert`'s, on
images it makes of every kind the reader takes; and, with `--workload`,
random writes, zeroed ranges and reads on a qcow2 read and written in place
(`block/libretro-qcow2.c`), each checked against a raw copy. `run.sh` makes
those images with `qemu-img` (versions 2 and 3, 64 KB and 4 KB clusters,
compressed, with a snapshot, the bundled HDD) and has `qemu-img check` and
`compare` judge them after, the snapshot read back unchanged, and a writer
killed partway leave no error; without `qemu-img` that part is skipped.

`test-audio-margin.c` works out the worst room the frontend's audio pull
finds without waiting for the DSPs' thread, over every phase of the APU's
engine frames against the video frames, with the engine frame in flight
not counted: the queue's preroll (`hw/xbox/mcpx/apu/monitor.h`) has to
leave a whole engine frame to spare.

`rom/rep.asm` and `rom/rep-stos.asm` check REP MOVS copies and STOS fills
in bare-metal guests with CPU Boost on and off. They share setup and handlers
in `rom/rep-common.inc`. They cover byte/word/dword operations, fill-value and
register preservation, copy overlap, direction, zero counts,
16-bit address/count handling and wrap, TF single-stepping, RF and execution
breakpoints, data watchpoints, a pending interrupt after STI, page-fault
restart, self-modifying code and a REP opcode spanning two code pages.
`run.sh` checks its completion marker, trap counts and remaining counts.

`python libretro/tests/test-io-recompile.py . gcc` executes production
I/O learning and block invalidation against controlled cache/page/jump
ownership. It prevents an unchanged old I/O block from being resurrected,
while checking ordinary block reuse, PC-relative hashes, page/jump cleanup,
duplicate invalidation and one-shot blocks in 513 cases with UB traps.

`python libretro/tests/test-vertex-snapshot-cache.py . gcc` executes the
production Vulkan snapshot lookup, insertion and invalidation functions. It
checks cached subranges, touching versus overlapping writes, both arena
generations, replacement beyond 512 entries, stale recent indices after cache
reset, and 80,000 randomized lookups under undefined-behavior sanitization.

`python libretro/tests/test-packed-uniforms.py . gcc` runs production packed
uniform comparison/copy and epoch invalidation functions. It checks padding,
unchanged and sparse writes, contiguous vector arrays, exact-bit values,
source generations, module recycling and 20,000 independent packing cases
with undefined-behavior traps enabled.

`python libretro/tests/test-uniform-stage-uploads.py . gcc` exercises the
production Vulkan uniform tracker, upload planning and descriptor-reuse
decision against bounded upload storage. It checks independent stages,
module switches (including descriptor reuse for matching ranges), changed
uniform ranges/backing buffers, alignment, resets and descriptor exhaustion
in 100,000 mixed draws, with GCC's undefined-behavior traps enabled. It also checks
A-to-B-to-A snapshot reuse without copies, append-on-mutation while old draw
bytes stay unchanged, stale offsets after external resets, and overflow-safe
snapshot bounds.

`python libretro/tests/test-tlb-dirty.py . gcc` checks the production TLB
dirty-range functions against a full-scan reference in 1,655,360 cases:
fast/slow flags, aliases, wrapped/empty ranges, active MMU modes and victim
entries. Another 10,000 states compare first-write dirty promotion against
an exhaustive scan, including primary/victim aliases and preservation of
other page flags. GCC's undefined-behavior traps are enabled. The tag index additionally
checks 20,000 larger-table mutation/reprotection states, victim restoration,
stale tags and 200,000 range/tag cases including unsigned wrap and host-width
shift boundaries, under undefined-behavior sanitization.

`python libretro/tests/test-page-collection.py . gcc` compiles the
production TB page collection (the pages a code write or an invalidated
range locks: the range's own and those of every TB on them) against a
reference over random pages and TBs, with other threads' locks simulated
by trylocks that fail at random. It checks the set, its ascending order,
that every page in it and no other is locked, the retries, the array's
move to the heap past 16 pages, and the unlock, in 160,000 cases with
GCC's undefined-behavior traps enabled.

`python libretro/tests/test-region-tbs.py . gcc` compiles the production
index of translated blocks by host code address (insert, remove, lookup,
walk, count, flush) and drives it as block translation does: new blocks
upwards, the newest dropped and its space taken again, old blocks
recycled in place, flushes. 25,000 checks compare every lookup, walk and
count with a reference, the array growing past its first size; GCC's
undefined-behavior traps are enabled.

`python libretro/tests/test-dma-map.py . gcc` compiles the production direct
DMA mapping function against bounded RAM and a pending GPU writeback. It
checks callback ordering for reads/writes, preservation of newly written
DMA data, translated and shortened mapping ranges, failed/empty maps and
the separate bounce path. GCC's undefined-behavior traps are enabled.

`python libretro/tests/test-voice-tail.py . gcc` runs the decoder-to-mix
regression without booting a complete core. It compiles the checkout's
actual PCM/ADPCM decoder, resampler, envelope/filter/routing functions and
voice-list active gate against bounded guest RAM and notification writes.
Worker execution and platform/debug glue are fixtures. The 944 finite-source
cases cover source lengths around frame/block boundaries, slow/fast pitch,
mono/stereo and DSP-off output; additional cases cover loops, SSL bank
completion, persistent underrun/refill, explicit stop, pause, restart and
VP reset/pre-load. Another 24,576 ADPCM cases cover every page offset for
mono/stereo buffers and streams, discontiguous SGE pages, address-space fallback, exact decoded
samples and the number of SGE translations per block. Pitch cases check
steady and changing fractional/extreme steps, reset and multipass-to-source
cache invalidation. Set `TAIL_TEST_CFLAGS` to
`-fsanitize=undefined -fsanitize-undefined-trap-on-error` for trapping under
the core's `-fwrapv` semantics. Python and GCC are required; no Xbox files
are needed. These are source-level tests, not a hardware timing comparison.

`run.sh <core.so>` runs the core in a headless frontend of its own
(`harness.c`: OpenGL core on a surfaceless EGL display, Mesa's llvmpipe
will do; with `--vulkan` a Vulkan context as RetroArch's vulkan driver
gives one, Mesa's lavapipe will do) with test ROMs in place of the Xbox's
boot ROM and BIOS, so no Microsoft file is needed. CI runs it after every
Linux build.

- `rom/bootrom.asm`: 512 bytes where the MCPX boot ROM goes; enters
  protected mode and jumps to the flash.
- `rom/flash.asm`: the flash. It starts one looping voice on the APU's voice
  processor, a 1 kHz sine at 48 kHz and 0 dB (`sine.inc`, 40 periods), and
  halts; built with `-DBUSY` it counts in RAM at 0x300000 instead, so the
  CPU never rests; built with `-DDSP` it also loads `gp.a56` into the APU's
  GP DSP and starts it (`-DGP_PASSES=n` sets its load, 1 by default);
  built with `-DSPIN` it spins on a word in RAM that nothing writes; built
  with `-DPAD` it runs `pad.c`; built with `-DGPU` it runs `gpu.c`; built
  with `-DDVD` it runs `dvd.c`; built with `-DHDD` it runs `hdd.c`; built
  with `-DACI` it runs `aci.c`; built with `-DEP` it loads `ep.a56` into
  the EP DSP and starts it.
- `rom/pad.c`: the pad through the USB controller, as the kernel reads it:
  the two hubs found by their status-change reports and configured, their
  ports reset, the pad's reports taken over its interrupt endpoint on the
  periodic list and copied to RAM. Freestanding 32-bit C; `pad.sh` builds
  it into `pad.inc` (committed, so the tests need no 32-bit compiler).
- `rom/gpu.c`: the NV2A from bare metal: the AGP bridge's windows and the
  GPU's BARs, a 640x480 32-bit display, one PFIFO channel in DMA mode with
  its objects in RAMIN, and a scenario the harness reads the picture of
  (`--pixel`): a surface cleared red and shown, left alone while another is
  drawn and flips are counted, read back by the CPU (the renderer
  downloads it for that), then written green by the CPU; later a surface
  drawn into with a partial clear between two draws, whose second draw must
  not take the clear's pipeline. `gpu.sh`
  builds it into `gpu.inc` (committed, like `pad.inc`).
- `rom/dvd.c`: DVD reads as the kernel sends them: READ(10) packets to the
  primary IDE channel's slave, by bus-master DMA through the PIIX3's PRD
  tables (one entry, three scattered, ten for 600 KB), every word read
  checked. `dvd.sh` builds it into `dvd.inc` (committed, like `pad.inc`);
  `mkdisc.c` makes its disc, whose every 32-bit word says which sector and
  word it is.
- `rom/hdd.c`: the hard disk, the primary channel's master: 40 sectors past
  its partitions written by WRITE DMA from two PRD entries, FLUSH CACHE,
  read back by READ DMA into three and one by PIO, every word checked, and
  a read past the disk's end that must fail. `hdd.sh` builds it into
  `hdd.inc` (committed, like `pad.inc`).
- `rom/aci.c`: the MCPX AC97 controller's PCM out DMA: 32 buffers of 50 ms,
  its position read 100 ms after the start and a second later by the TSC,
  for how many samples a second of guest time takes. `aci.sh` builds it
  into `aci.inc` (committed, like `pad.inc`).
- `rom/ep.a56`: the EP DSP's program. Each run (every 8th engine frame) it
  sends the next 512 values of a counter to its output FIFO 0 by DMA, 16-bit:
  the core sinks that FIFO to the frontend, so the audio is a ramp.
  `ep.inc` is it assembled with a56, as `gp.inc` is `gp.a56`.
- `rampcheck.c`: checks that the harness's audio is that ramp, every 16-bit
  sample the one before plus one, from its first one to the end.
- `rom/gp.a56`: the GP DSP's program. Every 32-sample frame it rebuilds
  mixbins 0 and 1 from 64 exact products a sample, n times over, so the tone
  comes out of the DSP unchanged after n x 4096 MACs. `gp.inc` is it
  assembled with a56 (a DSP56001 assembler; the 56300 runs that code), so
  the tests do not need a56.
- `sinecheck.c`: checks the audio the harness wrote: the tone unbroken, its
  level, the number of samples.
- `dspdiff.c`: the DSP JIT against the interpreter, built from the core's
  DSP sources alone (`make dspdiff`, for another host `make dspdiff
  CC=aarch64-linux-gnu-gcc`): a DSP program (`gp.inc`, or a dump of a
  game's DSP: `DSPD`, then P, X and Y as 32-bit words) run frame by frame
  on both, from the same state, with the same random memory around them,
  the whole DSP state compared after every frame.

What `run.sh` checks: every `retro_run` hands over one video frame and
exactly one frame's worth of audio (800 or 801 samples at 60000/1001 fps),
idle and busy; the tone has no gap; an idle guest runs far faster than real
time when the frontend does not pace it (fast-forward), and so does one
that spins waiting; the busy guest does the same work paced and unpaced;
the pad's buttons reach the guest over USB; DVD reads by DMA land in the
guest's memory byte for byte; the hard disk's writes and reads by DMA and
PIO come back byte for byte and are in the image file, on a raw image the
core makes and on a qcow2 used as it is (which `qemu-img check` then finds
clean); the idle guest runs the same
on a Vulkan context, where there is a Vulkan device, through version 2 of
the context negotiation interface and as a frontend that knows only
version 1; with APU DSP
Emulation on, the JIT leaves the DSP in the same state as the interpreter
after every frame of the GP program, the tone comes through the GP DSP
whole on the interpreter and on the JIT, the interpreter does not fall behind on a heavy program, and
the JIT's state survives a savestate; savestates, loads and resets between
frames, and right after a load the guest's RAM is byte for byte what it was
when the state was saved, in the same process and in another one.

The harness's options are listed at the top of `harness.c`.

The OpenGL byte reinterpretation oracle runs the production transfer and
native-size shrink functions in a surfaceless EGL context. It checks every
RGB565 word at 1x, 2x and 4x, repeated source updates, and pixel-store/PBO state
restoration. It also runs the production asynchronous download function and
checks native depth allocation, scaled color storage, buffer growth/reuse and
returned pixels at 1x/2x/4x. On Linux with EGL/OpenGL development libraries
installed, run:

```sh
python3 libretro/tests/test-gl-texture-reinterpret.py .
```

`test-texture-reinterpret.py` also checks both renderers' production eligibility
predicates, including format/layout rejection and overlapping dirty surfaces.

`test-audio-volume.c` also checks the VP host float-to-S16 output against a
long-double rounding oracle: all 65536 normalized PCM samples at unity,
attenuation consistency with the integer output path, every Q16 gain on
boundary values, adjacent floats around half steps, one million arbitrary
float encodings, clipping, mute and nonfinite inputs. The old conversion
loses one step on 65535 of 65536 normalized PCM round trips; the new one
preserves every value. The test runs as part of `run.sh`.
