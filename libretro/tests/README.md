# The core's tests

The harness validates the declared 48 kHz / 60000:1001 timing and counts audio
with an integer phase, including phase save/restore. Its cadence oracle stays
exact when hard-FPU tests leave host floating-point rounding changed.

The qcow2 span test instruments the production layer's file calls while keeping
the real file backend. It replaces existing data through 130 adjacent spans
and checks one read/write call, then verifies separated buffers, the 64-run
limit, null empty spans and a cluster split inside an original span. Images
are disposable copies of the bundled HDD; qemu-img checks their metadata.

DVD DMA checks run the same word-pattern image through native I/O and the
harness's read-only `content://` and `saf://` VFS providers. The provider uses
the API's zero-on-success seek result and deliberately returns at most seven
bytes per read. Disc classification, partition probing and guest scatter-DMA
must still deliver every expected word. The standalone VFS test additionally
checks coalesced callback counts, untouched gaps, EOF and seek-error recovery.

Shader-dictionary tests reuse the core's object files and compiler platform.
For a Windows core tested from Linux, the helper is a static MinGW executable
run through Wine. Its exit status is checked before printing its summary.

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

The GPU ROM's texture-alias phase (frames 680 through 810) samples a red
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
engine frames against the video frames, with the engine frames in flight
(`MCPX_APU_MONITOR_FRAMES_IN_FLIGHT`) not counted: the queue's preroll
(`hw/xbox/mcpx/apu/monitor.h`) has to leave a whole engine frame to spare.

`rom/cf.asm` checks the carry flag across translation block boundaries:
every flag-setting instruction, on operands around the carry, then a jump,
then a block that reads CF before it sets the flags (INC, which keeps CF,
ADC, JC, and one block reached from blocks of every kind and left by an
indirect jump), each against what SETC saw right after the instruction.
Two more blocks are reached from every kind: one sets the flags before it
reads CF, so CPU Boost translates it once for all of them (`TB_CF_ANY`),
and one leaves by a direct jump with the flags untouched, so it must not be
(a translator that shared it fails this case).
`run.sh` runs it with CPU Boost on, where the next block knows from its TB
flags how to compute CF and does it inline, and off, where QEMU's helper
does.

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

`python libretro/tests/test-vp-threads.py . gcc` (run by `run.sh`, 3 s)
builds the voice processor's frame with its own dispatch: the unit schedule,
the claims, the worker threads (libretro-common's rthreads and eventcount).
It runs 400 random voice setups on four threads and on one and checks every
24-bit bin and VP monitor sample of each frame is the same. Half the setups
use DirectSound's layout, 3D voices into the multipass bin, so their frames
split into units; multipass voices mixing into the bin they read are among
them. `VP_THREADS_CFLAGS=-fsanitize=thread` runs it under ThreadSanitizer,
which finds no race.

`test-vpmath.c` checks the voice processor's integer control math
(`hw/xbox/mcpx/apu/vp/vpmath.h`) against the double-precision code it
replaced: the Lagrange kernel table and the cutoff curve over every input,
the pitch step and the envelopes' exponential decay and release at five
million points. `run.sh` runs that; with the argument `full` it checks
every steady pitch, 400 million ramped ones, every decay count and rate and
400 million releases (1.1 billion, about a minute): no difference.

`python libretro/tests/test-voice-tail.py . gcc` (run by `run.sh`) runs the
decoder-to-mix regression without booting a complete core. It compiles the checkout's
actual PCM/ADPCM decoder, resampler, envelope/filter/routing functions and
voice-list active gate against bounded guest RAM and notification writes.
Worker execution and platform glue are fixtures. The 944 finite-source
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
  GP DSP and starts it (`-DGP_PASSES=n` sets its load, 1 by default; with
  `-DGPWRITE` the CPU then writes and reads back the GP's memory while its
  frames run, and puts the GP in reset on a word that comes back wrong);
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

`test-audio-volume.c` runs with undefined-behavior sanitizer traps in the standard
driver. Its signed sample and half-step fixtures use exact multiplication,
including an `INT64_MIN` saturation case, so the C99 oracle does not rely on
left-shifting negative values. It also checks the VP monitor's integer mix-to-S16 output
(DSPs off) against a 128-bit rounding oracle: all 65536 PCM samples round-trip
at unity and match the integer volume path at 257 gains, every Q16 gain on
boundary mixes up to the int64 limits, four million arbitrary mixes, and both
sides of every half step. The test runs as part of `run.sh`.

`python libretro/tests/test-gl-volume-compression.py CHECKOUT [CC]` runs the
production DXT volume uploader and decoder on real OpenGL. It checks DXT1,
DXT3 and DXT5 slice/block order, alpha, depths 1/2/4/8, mip levels, RGBA8
fallback storage and native uploads without scratch allocation. Windows uses
WGL and the built `deps/libepoxy.a`; Linux uses surfaceless EGL. Every driver
checks the forced software fallback; drivers advertising
`GL_NV_texture_compression_vtc` also check native volume uploads. 2D S3TC
support alone does not imply volume support. The native oracle was confirmed
on NVIDIA and the S3TC-without-VTC fallback on Mesa llvmpipe.

The layout/capability contract follows the Khronos
[VTC specification](https://registry.khronos.org/OpenGL/extensions/NV/NV_texture_compression_vtc.txt)
and [S3TC specification](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_texture_compression_s3tc.txt).

`python libretro/tests/test-gl-upload-alignment.py CHECKOUT [CC]` runs the
production GL uploader on independently supplied post-unswizzle/conversion
bytes. On WGL (Windows, built libepoxy) or surfaceless EGL (Linux), it checks
46 cases spanning R8, RG8, RGB565, RGB8_SNORM and RGBA8; narrow 2D mips,
3D slices/mips, explicit linear pitch, and incoming unpack alignments
1/2/4/8, plus bordered RGB8_SNORM and palette-expanded RGBA8 cubemap crops.
Every uploaded byte must match, and unpack alignment must be
restored. Guest swizzling and format-conversion arithmetic are outside this
host pixel-transfer test.

`python libretro/tests/test-tb-code-hash.py . gcc` checks production code
hashing against byte-fetch oracles with the portable XXH3 implementation.
It covers page ends, nonadjacent/aliased mappings, device-read side effects,
sub-page fallback, mutations and executable-page faults, including 200,000
randomized comparisons per available scalar/SSE2/AVX2/AVX-512 path under
undefined-behavior traps. Each case also checks an already-resolved first-page
pointer: no extra probe for same-page RAM, with boundaries/devices retaining
their fallback. Cross-page device reads must precede resolving a later page
whose mapping they enable.

`python3 libretro/tests/test-vulkan-rgb-snorm.py CHECKOUT [CC]` requires Linux
Vulkan and Khronos validation layers. It runs the production RGB655 conversion,
staging and in-place RGBA expansion, checks every packed 16-bit input, verifies
that expanded host storage cannot alias a guest render target directly, and
compares actual GPU upload/readback bytes with synchronization validation on.
RGB bytes must remain identical; signed alpha 127 supplies the RGB format's
implicit alpha of one. Guest unswizzle output is provided linearly by the fixture.

`python libretro/tests/test-texture-layout.py CHECKOUT [CC]` extracts the
production texture-span and cubemap-face calculation, format table and shape
from the core. Fixed edge cases and 5000 independent row/block mip walks
check tiny compressed faces, borders, shrinking 3D mip depth, linear pitch,
face alignment and LOD-clamp independence under UBSan. It requires a C
compiler and no graphics driver. OpenGL and Vulkan use the same aligned face
span as the cache's guest-memory range calculation.

`python libretro/tests/test-retail-seed-name.py . gcc` checks production
shader-seed naming: immediate retail-disc names, stable normalization,
missing paths, Chihiro's 900-flip grace period and detected-title renames,
with undefined-behavior traps enabled.

`python3 libretro/tests/test-vulkan-cube-crop.py CHECKOUT [CC]` requires Linux
Vulkan, BC formats and Khronos validation layers. It extracts the production
cube crop and buffer-image region builder, then checks 168 actual GPU copy/
readback cases: R8/RG8/RGB565/RGBA8/SNORM and BC1/2/3, tiny faces and later
mips, cube layers 0/3/5 and all source-kind metadata values. It checks exact
bytes, mip extents, compressed row alignment and preservation of the original
allocation spans. Base crops follow OpenGL's existing four-texel convention;
higher mip source origins remain unchanged and are not a hardware oracle.

`test-vram-ranges.c` checks shared half-open VRAM overlap semantics with
82944 byte-by-byte cases and 500000 independent carry-preserving endpoint
cases, including empty, adjacent, one-byte and full-width address spans.
`run.sh` runs it. The GPU ROM at frame 800 samples a one-byte swizzled Y8
view from the first byte of a GPU-cleared blue ARGB surface while RAM stays
zero; frame 810 must be white on both renderers at 1x/2x/4x. This reproduces
the old OpenGL stale-black result with a real texture upload.

`python libretro/tests/test-vk-worker-modules.py . gcc` runs the production
worker memo and immutable-array reference functions. It checks exact keys,
collisions, SPIR-V versions, the byte cap, oversize bypass, live references
after eviction/clear, queue drops, adoption and per-state completion markers,
plus 10,000 FIFO/slot-reuse cases and 800,000 concurrent ref/unref pairs
with no leaked arrays (UB traps).

`python libretro/tests/test-spv-reflection-borrow.py . gcc` uses the real
GLSL compiler and SPIRV-Reflect to compare copy/borrowed metadata for vertex,
geometry, fragment and compute shaders under SPIR-V 1.5/1.6. Uniform layouts,
arrays, descriptors and push constants match; borrowed instances share the
original immutable words and can be destroyed independently. Production
layout construction/destruction is compared with a copy-mode control; layouts
must survive freeing all parser/source data, and all packed uniform storage
must be released.

`test-texture-dirty.py CHECKOUT [CC]` compiles the actual OpenGL and Vulkan cache-invalidation functions against a page-dirty RAM oracle. It covers the final VRAM byte/page, palettes sharing cleared pages, writes crossing a page boundary, and empty spans. The standard runner executes it alongside the VRAM range test.

The GPU alias sequence also tests the last VRAM page with `xemu_memory=64`: frame 830 samples a linear Y8 texture ending at 64 MiB (white), frames 850/870 sample two cached views after a shared-page CPU write (black), and frames 890/910 sample a 256-entry palette ending at 64 MiB before/after a CPU update (white/black). These checks run on OpenGL and Vulkan at scales 1, 2 and 4.

GPU alias frames 930/950 sample an indexed texture after GPU clears write red then blue into the palette surface. CPU palette RAM starts black and the indexed binding is already cached, so these checks detect missing GPU palette synchronization and stale cache revalidation. Both APIs run at scales 1, 2 and 4.

After the GPU palette updates, guest frames 960 through 999 rebind the same indexed texture and draw without modifying its palette. Captures at frontend frames 970, 990 and 1005 must remain blue. A separate temporary transfer-counter audit measured actual palette readbacks and indexed uploads across this sequence; those counters are not part of the shipped core or the standard visual regression assertions.

`python libretro/tests/test-vk-vertex-copies.py CHECKOUT [CC]` runs the
production vertex range merge, dirty/COW dispatch and remap copy against
independent page-union and byte-fetch oracles. It covers 100,000 interval
sets, 10,000 zero/nonzero sync-queue cases and 20,000 multi-attribute copies
with output guard bytes, packed/strided sources and zero vertex counts. It
checks that merged ranges stay in the input array and packed attributes use
one copy; undefined-behavior traps are enabled. No graphics driver is needed.

At guest frame 1020, two GPU palette clears (red then green) are each followed by an indexed draw before the next flip. Frontend frame 1030 must show green; the former Vulkan per-frame validation throttle left it red. The extracted dirty-page test also checks that GPU writeback invalidates all overlapping texture/palette validation stamps even when they are already marked dirty, leaving unrelated and empty views unchanged.

Guest frame 1040 clears a GPU ARGB source red then blue, sampling each update through a one-byte Y8 view before the flip. Frame 1060 repeats this using the GPU-produced byte as index 0 then 255, with green and white palette entries. Frontend captures 1050/1070 must both be white. The Vulkan build predating GPU-writeback validation-stamp invalidation instead produced black and green.

The fixture wrap handshake publishes PUT=0 immediately after writing the jump-to-zero command, then waits for GET=0; this makes zero a stable empty-ring state. Waiting with the old PUT could let the GPU replay old commands and miss zero. The retained-draw tests render two texture versions into separate targets before the same flip, then scan out each target: frames 1090/1110 must be black/white for texture byte updates, and 1130/1150 red/green for palette updates. This checks the first draw as well as the latest version.

`test-audio-volume-wav.py UNITY.wav SCALED.wav GAIN_Q16 [--all-s16]` compares actual stereo 48 kHz S16 captures sample for sample using Decimal half-away-from-zero quantization. The standard runner captures the EP ramp at mute and 20/40/60/80 percent on the DSP interpreter and JIT; every reference capture must contain all 65,536 S16 values. It also checks the separate GP output path at 40 percent on both engines. Curve gains are 0, 825, 5430, 16347 and 35731 in Q16; the GP gain is 5430.

`python libretro/tests/test-vertex-fetch-span.py CHECKOUT [CC]` checks the
shared fetch-span calculation against 200,000 per-byte walks, and executes
the production OpenGL/Vulkan range-queue blocks and buffer-update functions
for 2,005 cases. It covers padded and overlapping strides, zero counts,
uniform-only ranges, final-page/VRAM endpoints, 64-bit products and the GL
quick-cache sequence across an empty first attribute. UB traps are enabled.

The compressed-volume oracle adds 21 strict fallback cases against independently computed normalized colors: nontrivial endpoint interpolation, both BC1 palette modes, every BC2 alpha nibble, all BC3 alpha selectors with both endpoint modes and equal endpoints, odd 7x5x3 dimensions and mip tails down to 1x1x1. These compare 113,616 additional RGBA components after actual GPU upload/readback. Strict interpolation comparisons apply to software fallback; native VTC retains its separate endpoint/block-order checks because driver interpolation precision can differ.

The GL upload-alignment oracle now uses the production BC decoder for 36 additional forced 2D fallback cases: BC1/BC2/BC3 at unpack alignments 1/2/4/8, 7x5 odd images, 1x8 narrow rows and 16x8 mip chains down to 1x1. It verifies level dimensions, RGBA8 storage, 10,944 component values and restoration of unpack alignment. The 2D and volume oracles share `bc-reference.h` for the independent normalized-color reference.

`python3 libretro/tests/test-vulkan-volume-blocks.py CHECKOUT [CC]` compiles the production volume staging, VRAM selection and copy-region functions, then transfers compressed BC1/BC2/BC3 volume data through Vulkan 3D images. Its 324 cases cover one/multiple XY blocks, depth groups including a partial fifth slice, absent VRAM buffers, unaligned source offsets, and ranges ending exactly at VRAM end. It checks exact block bytes and staging allocation avoidance; Khronos synchronization validation must report zero errors. Requires Linux Vulkan with BC 3D image support and validation layers.

The volume-block oracle also records production copy operations. Direct VRAM levels must copy nothing; ordered layouts falling back because VRAM is absent or unaligned must use one bulk copy; layouts requiring rearrangement must retain their per-block copies. The 324-case matrix records 1,314 operations after coalescing versus 1,548 before, with identical GPU bytes. These are source-level copy-operation counts, not a game frame-time benchmark.

`python3 libretro/tests/test-vulkan-linear-span.py CHECKOUT [CC]` validates production linear staging and copy-region functions with 324 Vulkan image transfers. R8, RG8, RGBA8 and RGB565 cases cover narrow/padded rows, one/multiple rows, absent VRAM buffers, unaligned offsets, and valid VRAM-end ranges. It checks exact pixels, row pitch, unchanged direct spans and exclusion of trailing padding from staging writes/allocation, plus an empty-height underflow check. Requires Linux Vulkan and Khronos validation layers.

The `EXITWORK` flash scenario checks that register-only work crossing STI/CLI
block exits completes inside 20 runs, with CPU Boost enabled and disabled.
It guards against speculative idle detection skipping active game work, as
seen in Ninja Gaiden Black near a door.

`test-kernel-idle.py` tests the production opcode/empty-queue proof for the
short kernel idle fast path, including both loop forms, exit positions,
unreadable memory, pending work and self-modifying code. Active STI/CLI
work must fail the proof and continue running.

The texture-layout oracle also checks 3,570 hash-prefix cases against an independent active-byte walk. Only final-row padding of unconverted linear formats is excluded; packed, swizzled, compressed and converted formats retain their prior hash extent. GPU frames 1170/1190 both sample a white 1x1 linear Y8 texture before/after a CPU write to trailing padding. A separate temporary upload-count audit measured three uploads across the complete sequence after the change versus four before, on both APIs; the standard suite asserts the visible pixels.

GPU frames 1210/1230 and 1250/1270 test width-one YUY2 and UYVY textures. Neutral Y/Cb/Cr bytes of 128 produce 828282 with the existing conversion; changing only the shared Cr byte to 255 produces ff1b82. Cr lies at byte 3 for YUY2 and byte 2 for UYVY, beyond the first pixel's nominal two-byte span. The cache must invalidate both converted formats; these checks protect their full-hash exception and host upload precision, not a new Xbox color-matrix calibration.

`python libretro/tests/test-vk-cow-tail.py CHECKOUT [CC]` runs production
COW lookup/store/growth/snapshot functions with immutable-reader byte
oracles. It checks tail-only writes, contained reuse, dirty and GPU-writeback
invalidation, CPU writes during downloads, slot rotation, pending pre-bind
snapshots, arena exhaustion,
100,000 selector cases and 10,000 snapshot operations with epoch recycling.
Coherent and non-coherent atom boundaries are checked under UB traps.
The alignment gate follows the [Vulkan mapped-memory rules](https://docs.vulkan.org/spec/latest/chapters/memory.html):
non-coherent host writes synchronize at atom granularity.

At guest frames 1280/1300 the fixture samples two CPU-written texture/palette versions before a flip. A backend semaphore between the first draw and the second CPU write ensures that pushbuffer parsing alone cannot hide the old version. Frontend captures 1290/1310 must show white/green. The old Vulkan validation throttle showed stale black in the texture case; the palette case is a passing control. The dirty-page extraction test also requires CPU invalidation to reset stamps of already-dirty overlapping texture and palette views.

The CPU-write GPU cases at frames 1280/1300 now leave texture sampling enabled after the first draw. After the backend semaphore and CPU rewrite, the second draw resends no texture, sampler or combiner state. This directly exercises the unchanged-state Vulkan bind fast path. The extracted fast-path test verifies clean-page reuse, same-frame dirty detection and validation-stamp reset, and preservation of dirty bits when bindings are disabled.

`test-inline-index-bounds.py` compiles the actual 16/32-bit index packet
handlers, mixed DRAW_ARRAYS expansion, derived bounds and NV2A post-load
hook. An independent list walk checks order/bounds for 65,536 complementary packed
endpoint pairs and 100,000 mixed operations, including partial packet
consumption, tracing, capacity, wrapping ranges, reset and legacy loads.

At guest frame 1320 a retained one-byte Y8 view samples a red GPU surface, then the producer is cleared blue and sampled again without texture/sampler state changes. Capture 1330 must be white; Vulkan previously kept the old black private texture. Bound-view invalidation tests cover overlapping texture/palette byte ranges, nonoverlapping ranges in the same page, empty ranges and null bindings without RAM queries. The harness now accepts 64 pixel captures so the standard alias loop can include this case.

At guest frame 1340 a retained indexed texture samples a red GPU palette. After a backend semaphore, the GPU clears only the palette blue; index RAM and texture/sampler/combiner state stay unchanged before the second draw. Capture 1350 must be blue. Vulkan before bound-surface consumer tracking stayed red. The standard loop checks this on both APIs at scales 1, 2 and 4.

At guest frame 1360 a fresh ARGB GPU source is sampled directly, changed red to blue, and sampled again without texture-state changes. It then ages out before the CPU reads it at frame 1460. Captures 1370/1410 must stay blue and GPU_READ0 must contain ff0000ff. A Vulkan snapshot at frontend 1447, load at 1470, and later CPU read verify that deferred retired content survives state handling. Temporary readback counters measured expiry-time readback moving from guest frame counter 1364 to the real CPU reader at 1389; context destruction still materializes owed content.

The compatible source created at guest frame 1360 is now sampled on every frame through 1440 without texture-state changes, before the later CPU read. A separate per-binding sample clock keeps its borrowed surface from aging out during use. `test-vulkan-surface-age.py CHECKOUT [CC]` compiles production expiry/timestamp functions and checks 340 combinations, including copied-source and cubemap address/draw mismatches, plus idle expiry and unsigned frame wrap without a graphics driver. Temporary GPU counters showed an expiry-triggered readback/upload before the change; afterward no texture upload occurs and the source readback waits for the real CPU reader.

The shared compatible-source fixture now samples through frame 1440 to exceed OpenGL's 60-frame age limit, with additional blue captures at 1430/1470. OpenGL marks already-resolved flat or cubemap GPU source surfaces used on successful texture-copy reuse. Temporary counters measured readback and RAM upload at counter 1419 before the fix; afterward the sole readback waits for the CPU reader at 1459, with no RAM texture upload. Conversion arithmetic, copied pixel data and the 60-frame idle limit are unchanged.

The source-age unit oracle also extracts the OpenGL touch helper and verifies flat-source, all-six-cube-face and null-source stamp updates.

The copied-source case creates a fresh ARGB surface at frame 1660, samples its
RGB565 GPU copy through 1740, changes filtering at 1700 and reads the source on
the CPU at 1760. Captures at 1670/1710/1770 stay `0x001cff`; guest word
`0x00300044` becomes `0xff0000ff`. The Vulkan expiry scan recognizes a bound
byte-reinterpreted copy only when its source address and draw timestamp match.
It uses the existing sample clock, so an unused binding cannot pin the source.
Focused transfer counters showed a readback/upload at the sampler change before
the fix; afterward the readback waits for the explicit CPU read, with no upload.

The GPU cubemap fixture clears six 16x16 swizzled ARGB faces at guest frame 1800,
samples through 1880, changes filtering at 1840 and reads all six faces on
the CPU at 1900. The +X face is redrawn cyan at 1820 and red at 1860;
captures 1810/1830/1850/1870/1910 follow red/cyan/cyan/red/red. A -X capture
at 1842 stays blue, verifying an unchanged face; guest words 0x00300048 through
0x0030005c contain one red and five blue pixels. A fixture-only deferred-draw
flag lets the caller finish cube state before sampling any source.
Vulkan records the six source draw timestamps when copying the cube; its expiry
scan requires a matching face address, timestamp and recent sample clock. RAM
uploads, resource release and cache-node initialization clear that provenance.
The unit fixture checks each face, wrong timestamps, absent provenance, stale
sampling, non-cube bindings, misaligned/out-of-range addresses and idle expiry.
Focused counters showed six face readbacks plus one upload at the filter change
before the fix. Afterward readback waits for a real CPU reader or teardown;
there is no texture upload at the filter change. Native-depth copies remain a
separate audit target.

The kernel-idle oracle also compares borrowed ordinary RAM with the original
read path over 20,000 mutations, work-list changes and failed reads. It covers
page/region boundaries, RAM-device rejection, address-space attributes and RCU
lifetime, and asserts zero copied bytes for supported RAM.
`test-frame-completion.py` compiles the production frame-end handoff against a
model of the GPU's thread: one request, then waits until the thread has drawn
the display. 216 combinations cover pending work, racing/spurious wakeups,
stopping while waiting and no GPU thread. Guest time is not advanced by it.

there is no texture upload at the filter change.

The depth-source fixture creates linear D16 at frame 1960 and D24 at 2120,
samples their GPU copies, changes filtering at 2000/2160, and reads the sources
on the CPU at 2060/2220. A depth-greater-than-zero shadow comparison checks
nonzero sampled depth (white); the CPU words are `0x80008000` and `0x80000000`.
The standard test runs at scale 2 to exercise native-size depth copies.
Vulkan marks depth-copy provenance at the completed GPU conversion and clears
it on RAM uploads, cache initialization and resource release. Source retention
requires a matching VRAM address, draw timestamp and recent sample clock, just
like the byte reinterpretation path. Focused D16 counters at scale 2 showed a
readback/upload at 1999 before the fix; afterward only the requested CPU readback
at 2059 remains. No depth quantization or packing arithmetic changes here.

The lifetime fixtures explicitly select displayed surface D for their ongoing
draws: the loop's earlier background work selects B each frame. Their existing
transfer counters measured real sampling, but unchanged captures alone did not
prove that later draws reached the displayed surface. The new depth transition
case makes that distinction observable. It reuses a retired D16 GPU snapshot as
RAM data at 2280, writes zeros on the CPU at 2320, changes filtering at 2340,
then clears the depth source on the GPU at 2360. Display captures must go white
(2290), black (2330/2350), then white (2370/2410). CPU markers verify zero after
the CPU rewrite and `0x80008000` after the GPU rewrite. The standard test uses
scale 2. This adds regression coverage without changing renderer behavior.

The cubemap transition case reuses the retired GPU cube as RAM at 2480, writes
+X green at 2520, changes filtering at 2540, selects unchanged -X blue at 2560,
writes -X yellow at 2580, then switches back to +X green at 2600. Draws explicitly
select displayed D. The +X and -X edits resend no texture binding state; a backend
semaphore orders prior sampling before each CPU write. Captures verify red,
green, green, blue, yellow, green, green at 2490/2530/2550/2570/2590/2610/2650.
CPU words 0x00300070/74/78 verify green +X, yellow -X and untouched blue +Y.
The standard test covers OpenGL and Vulkan at 1x, exercising whole-cube hashing
and face strides after GPU-copy provenance has been replaced by RAM data.

Vulkan mixed cubemaps: native-resolution, single-level unbordered ARGB cubes can
combine compatible live GPU faces with RAM faces. Each selected surface must
cover exactly one face and use the matching host format; overlapping dirty
unselected surfaces or owed parked surfaces force the existing RAM fallback.
The staging/region builders skip GPU faces; RAM faces upload first, then the
GPU copies execute after prior source draws. Cache updates track per-face source
draw timestamps and a source bitmask; RAM edits still trigger content validation.
Lifetime tracking consults only the selected face bits. With Force Texture LOD0 enabled, eligible single-level ARGB mixed cubes also retain scaled GPU faces. RAM faces upload into a native-sized image and expand on the GPU with nearest blits; rendered faces copy directly at internal resolution. Temporary upload images retire through the existing image pool. Other layouts and unsupported blit formats retain the fallback.
The mixed fixture creates -X magenta on the GPU at 2720 with other faces in RAM,
selects +X green at 2740, edits it yellow on the CPU at 2745, redraws -X cyan on
the GPU at 2760, then checks +X yellow again at 2780. The GPU redraw uses a full-surface, all-channel clear of the same live
swizzled view. Six captures plus cyan/yellow/untouched-blue CPU words verify
layer selection, updates, copy-on-write preservation and delayed CPU reads.
Focused native-resolution counters removed the bind-time 1 KiB GPU-face readback;
only five RAM faces are uploaded, with the GPU face copied directly. Later
idle expiry or CPU reads still settle owed GPU content normally.

Vulkan swizzled clear reuse is restricted to an exactly matching color view
(format, dimensions, pitch, native-resolution mode, anti-aliasing and complete
clip bounds). Full clears use the existing attachment-clear path; partial
clears use the existing mask/scissor pipeline after the state-isolation fix.
Depth views and layout migration remain guarded. The mixed cube fixture now
re-clears its retained GPU face cyan at 2760; a separate case clears a 16x16
face blue then rebinds and clears it magenta at 2860. CPU verification at 2880
checks all 256 swizzled pixels, both endpoint words and an untouched adjacent
blue face. This validates storage/reuse behavior without changing clear color
arithmetic or asserting new hardware-accuracy results.

Partial-clear state isolation: Vulkan clear pipelines encode their own static
blend, write-mask and depth state. The normal-draw dynamic-state emitter now
skips clears and invalidates its shadow so subsequent normal draws re-emit state.
A fresh 16x16 swizzled target at 2960 and linear target at 3040 are cleared blue,
then red-only on the left half. The swizzled case first clears an alternate
linear target and restores its shape to force a real rebind; the linear case
keeps its original binding. CPU checks at 2980/3060 verify
all 256 pixels each: left magenta, right blue, alpha unchanged. The normal draw
after each clear must display the existing blue texture at 2990/3070. The
focused pre-fix swizzled run lost blue in 128 pixels; afterward all pixels match.
This fixes host pipeline-state interference without changing color arithmetic.
The exact-view swizzled rebind now also permits the verified partial clear;
depth targets, larger cached views and layout migration remain guarded.

The forced swizzled rebind regression at 2960 changes the shape generation by
clearing a disjoint linear target, then returns to the original swizzled view
before clearing red on its left half. This avoids the render-target memo fast
path. Before expanding the exact-view exception it triggers the clear assertion;
afterward all 256 swizzled pixels retain the expected blue/alpha channels. The
comparison linear case and following blue textured draws remain unchanged.

OpenGL now permits the same exact swizzled color-view clear reuse as Vulkan:
matching host/guest format, strict dimensions/pitch, anti-aliasing and complete
clip bounds. Depth and layout-migration assertions remain guarded. The standard
partial-clear regression now runs through both renderers, covering retained
cubemap face re-clears, a forced swizzled rebind, 256-pixel channel preservation
for each layout and correct following textured draws. A focused GL oracle also
checked all 15 nonempty channel masks with 16 color patterns in both layouts:
480 cases / 122,880 byte-exact ARGB pixels at each of 1x, 2x and 4x, zero errors.
No GL clear arithmetic, color-mask implementation or transfer policy changed.

OpenGL speculative readback freshness: the fixture explicitly issues FLIP_STALL,
which runs the prediction hook (FLIP_INCREMENT_WRITE alone does not). Frames
3120..3135 CPU-read a red 32x16 source then redraw it blue and call FLIP_STALL.
After source eviction, the CPU checks all 512 blue pixels at 3220. A second
prediction at 3280 is made stale by a green redraw at 3300; all 512 green pixels
must be correct at 3380. A counter requires all 17 red reads to complete, with
zero red-read mismatches. Distinct final color markers prevent incomplete runs
from passing with zero-initialized error counts.
The issue path retains an existing PBO/fence when its draw timestamp matches,
using the same freshness criterion as harvest. A changed timestamp still deletes
and reissues the snapshot. Focused instrumentation showed 16 predictions plus a
duplicate age-eviction issue before the fix; afterward the duplicate disappears.
The stale second prediction is still replaced correctly. No waits, ownership
fields, readback conversion arithmetic or eviction policy were added or changed.

`test-interrupt-shadow.py` compiles the production x86 exit generator, carry-class selection, generic page/link guard and pointer-lookup fallback. Its 2,688 cases cover CPU Boost, existing shadow, TF/RF, same/cross page, debugger restrictions and known/unknown carry class. STI probes only carry-independent destinations and retains the runtime fallback for other classes; it does not create direct links or require extra entry-class variants. The `STI_BOUNDARY` variant of `rom/cf.asm` repeats carry consumers immediately after STI, including shared destinations reached from different flag-setting instructions, with both CPU Boost modes. Existing REP MOVS/STOS tests still require pending IRQ delivery after the first REP iteration; EXITWORK protects active STI/CLI code from idle misclassification.

`test-idle-interrupts.py` compiles the production idle-wait ordering helper against the existing interrupt-handler interface. Its 64 cases require posted interrupts/stops to be serviced before the opcode proof, allow masked interrupts to retain idle handling, preserve stop exceptions and block-link invalidation, and reject active handler code as idle.

Mixed-cube GPU-only updates distinguish face-mask changes from GPU draw-timestamp
changes. An in-place GPU-only update leaves RAM layers intact, so it no longer
forces an auxiliary RAM upload or treats the open recording as an upload hazard.
The copy-on-write helper reports whether it allocated a replacement image; if
so, the RAM layers are restored before the GPU face copies. RAM edits and mask
changes retain their uploads. Existing mixed-case captures at 2750/2770/2790
verify yellow RAM data, cyan GPU data and preserved yellow RAM data afterward.
Focused counters showed uploads at 2719/2744/2759 before the change and only
2719/2744 afterward: the GPU-only redraw at 2760 no longer uploads five unchanged
RAM faces. Native mixed-path scope and scaled fallback behavior are unchanged.

`test-two-page-exit.py` compiles the production x86 relative-branch generator and page guard: 5,120 mode, PC-relative, page, segment, carry-class and exit-slot cases check the two-page cache fallback and retained ordinary direct-link exits. The cache generator is mocked in this oracle. The real guest ROM `two-page.asm`, with Boost on and off, repeatedly calls an instruction straddling two pages, then remaps its second page, modifies both physical code pages, unmaps the second page and verifies a page fault, and remaps it again with a full CR3 flush. Every phase must see the current immediate value. Existing REP/debug/interrupt/active-work tests continue to cover the original exit restrictions. Two-page targets are never permanently linked; single-page targets and misses return the original source/slot to the dispatcher.

Vulkan cube refreshes reuse unchanged GPU layers when the destination image is preserved. A fresh replacement image or missing GPU-face provenance still copies all required faces. Temporary transfer counters in the one-face redraw fixture measured six initial copies and one per redraw (eight total), compared with six on every update (eighteen total).

`test-sse-broadcast.py` compiles the actual SHUFPS emitter against independent lane-selection and helper-routing models: 98,304 bit-pattern/alias/encoding cases each for native and forced-big-endian fallback selection, with undefined-behavior traps. Whole backing storage is checked to protect upper vector-register bytes. This is an emitter/model test, not big-endian hardware validation. The `sse-shuffle.asm` guest ROM checks all 256 selectors for same-register, different-register and memory sources across XMM0/1/3/7 (3,072 cases), including signed zero, NaN payloads, infinities and subnormal bits, and verifies unchanged EFLAGS. Both CPU Boost modes must produce SHOK with no failure index. The new path uses the existing TCG 32-bit lane-duplicate operation only for legacy 128-bit same-source broadcasts on little-endian hosts; floating-point arithmetic and rounding policy are unchanged.

The SSE native-gate test compiles the production arithmetic helper macros with real SoftFloat. On x86-64 it compares 128/256-bit and scalar operations against the previous per-lane decision across rounding/flush modes, register aliases, first-use host mode repair and special values. It counts eligibility checks and compares register guards, software flags and host MXCSR. Native add/multiply with two NaNs retain the documented compiler-dependent input-payload selection; software results are exact. Other hosts skip this x86-64 fast-path test.

The VFS scatter-read test covers EOF zero-fill with a null, zero-length trailing span. The standard driver runs it with undefined-behavior sanitizer traps; empty vectors must not cause library calls on null pointers. Existing short-read, cursor-reuse, gap-preservation and error-recovery checks remain covered.

The SHUFPS emission oracle now covers every legacy register selector and both source-register aliases, with all loads checked before overlapping writes. Both memory-source positions, other encodings and the big-endian selection model retain the helper. The guest ROM exercises all 256 selectors with same-register, distinct-register and memory operands.

The EP DSP Run-Ahead regression compares 150-frame unity-output captures with and without one hidden frame. The captured ramp must contain at least 200,000 progressing channel samples with no breaks, and both WAV files must match byte-for-byte. This covers the EP DMA output queued eight engine frames at a time and its queued-ahead state across repeated serialization. Both runs retain strict audio cadence checks.

The state-variable filter regression (test-svf.c) runs without signed-wrap flags, with undefined-behavior sanitizer traps. It compares every internal filter state and all five output selections against an independent 128-bit formulation using division/floor rounding. Its 661,875 samples cover coefficient/state/input boundaries and changing-cutoff histories, including negative feedback and saturation. This validates the existing fixed-point equations, not a new Xbox hardware measurement.

The extracted voice regression now includes stereo ParaEQ at every Q setting and two cutoff targets with opposite-signed gains, plus negative spatial input at direct/room coefficient endpoints and midpoint. Two frames per configuration preserve histories. Mixed output and filter states use a canonical integer hash (2482e118ff7aabf8), captured from the pre-change production-wrap build. The complete voice suite runs with -fno-wrapv and undefined-behavior traps; its resampler reference uses defined scaling too. The hash protects existing behavior, not Xbox hardware accuracy. An optional filters argument runs only these focused cases.

The extracted voice suite now uses the real voice_lock/is_voice_locked helpers instead of a query stub. A boolean-array oracle and independent word reconstruction check all 256 voices across 1,024 transitions, including repeated lock/unlock operations and bit-63 word boundaries. A production frame check confirms that a locked voice retains its source position, unlocking advances it, and locking again stops it. The optional locks argument isolates these cases under the existing no-wrap/UB-sanitizer test configuration.

The x87 scratch ROM runs 96 scenarios across three precision and four rounding modes, with hard FPU and CPU Boost independently on/off. It covers memory/register and integer arithmetic, helper comparisons, block boundaries, unrelated SSE helpers, half-ULP rounding, stack tags and control/status state. FT0 remains available to helpers inside the current instruction, but no later instruction can depend on its stale value.

The file I/O fixture writes 1,300 adjacent spans and 1,300 separated spans, including empty endpoints, then verifies every span. This exceeds the native vector limit and exercises bulk fallback writes on POSIX as well as Windows; separated source gaps must not enter the disk image.
A three-color mip fixture at guest 3440 samples mip 1, edits the red base to yellow at 3500, then selects blue mip 2 at 3560. With Force Texture LOD0 enabled it displays red then yellow; disabled it displays green then blue. The driver saves at host 3520, loads at 3620, and checks restored green versus stale blue at 3625. All RAM bytes must match. Both GL/Vulkan also load enabled/disabled snapshots in fresh processes with the opposite frontend setting. These cases exercise restart-scoped policy and bound host-state revalidation; live option toggling is not supported.

The RCU startup regression extracts the production initializer, callback queue, worker claim and fork-child hook, plus the actual GLib once implementation. It forces the two-consumer stale-head schedule from the startup abort, checks concurrent initialization publication, 100,000 repeated initializations, 16 competing consumer entries and the fork-child owner reset. The queue invariant abort remains enabled. Fork coverage is a callback model, not a kernel fork on Windows.

The texture-quality fixture at 3800 renders a red rectangle starting at x=8.375 in a green 16x16 GPU cube face, with the other five faces in RAM. Samples at x=8.125 and x=8.625 hit the same native texel but straddle the rendered edge at 2x/4x. With Force LOD0 enabled Vulkan must display green/red/green; disabled and the GL fallback display red/red/red. A later linear-filter phase exercises cube sampling with the AF override excluded; CPU reads after sampling still see the normal native red texel. The driver checks both options together at 1x/2x/4x on Vulkan and 2x on GL, plus the two independent combinations at 2x in both renderers. Earlier mixed-cube phases cover RAM edits, GPU redraws and replacement images.

Force Quality Filtering is a separate restart-scoped option, disabled by default and independent of Force Texture LOD0. Its AF override requires linear minification and magnification, wrapping/mirrored U and V modes, an unbordered non-cube 2D color texture, and backend support. It requests 16x capped by device capability; point filters, depth, cubemaps, clamp/border modes and unsupported Vulkan linear-filter formats retain their guest settings. This avoids widening footprints across known seam-sensitive layouts and the vendor-dependent nearest-plus-anisotropy behavior described by the Vulkan sampling specification. Wrapping atlases with internal tile boundaries cannot be recognized generically.

The scaled cube save/load case verifies full RAM restoration and checks that a pre-load host binding does not survive. States contain native guest RAM; sub-native rendered detail is recreated only by subsequent game draws. The standalone GL transfer oracle also advances the source draw generation on each host rewrite and checks that a second unchanged issue reuses the pending fence.

The 4020 phase binds the same striped RAM texture to a point-sampled T0 and a linear-filtered T1, toggling T1 off and on. T0 must stay solid red with Force Quality Filtering enabled. OpenGL keeps forced-AF cache entries separate because its sampler parameters live on texture objects; Vulkan already separates sampler state from images. Disabled OpenGL retains the existing cache behavior.

The 4180 phase samples x=-0.25 at the edge of the striped texture using linear filtering. Clamp-to-edge, border, emulated OGL clamp, repeat and mirror modes must retain their normal edge colors with quality filtering enabled or disabled. The independent-option cases also check that quality filtering alone preserves the guest mip clamps, while LOD0 alone still selects the red/yellow base mip and preserves scaled cube detail. The two-unit fixture uses repeat addressing so that its linear unit qualifies for forced AF.

`x87-status.asm` checks all 65,536 loaded status words and control words,
AX upper-half preservation, exact 16-bit memory writes and live stack/TOP
transitions with hard FPU and CPU Boost independently enabled and disabled.

Native SSE arithmetic tests cover 128/256/512-bit helpers against the prior
scalar-lane reference, including destination/source aliases, NaN payloads,
signed zeros, subnormals, host MXCSR and software exception status. Packed
CPU Boost arithmetic uses 16-byte host SIMD groups without temporary buffers.

The qcow2 span fixture also replaces complete clusters directly from 256 source spans. Instrumented production file calls require no old-data reads and the original source pointer for full replacements, while partial replacements preserve both sides. Optional qemu-img fixtures cover compressed, v2 and snapshot-shared clusters, including a primed decompression cache and unchanged snapshot contents.

Large partial qcow2 zeroing uses one counted range-zero operation for an exclusive cluster, including an unaligned range in a 2-MiB cluster. Tests preserve both edges and cover cluster crossings, compressed originals and v2/v3 snapshots on independent disposable images. Shared entries retain COW; native range-zero fallback remains in the file backend.

Tiny partial zero ranges (up to 4 KiB) retain one bulk buffer write, avoiding a file-size query and sparse-operation overhead. The crossing fixture checks both paths in one request.
The GLib compatibility regressions include 16 simultaneous callers interning
strings and sharing pointer arrays, hash tables, trees and date/time references
(800,000 reference/unreference pairs per type), shared-array free/empty/reuse,
and concurrent default-context publication. The same tests run against system
GLib as an independent behavior oracle. Container mutations still require an
owner; atomic references do not make mutation thread-safe.

`test-win32-tls.c` exercises the production coroutine TLS macros with 32 native
Windows threads and delayed key allocation. Exactly two keys must be allocated;
all threads must retain their own cell and value across concurrent first access.
The RCU startup test also enters two independent nested once locations that
collided in the old 64-slot table; the nested initializer must complete.

The Vulkan harness reload path destroys the old frontend device after core
context destruction and negotiates a new device before context reset. It clears
old command/fence/readback/query handles and balances loader/queue-lock lifetime.

The qcow2 fixture injects two consecutive sync failures after a metadata change, requires both errors to propagate, then performs a real successful host sync. A following clean flush must skip host I/O. This guards retry behavior without replacing the real file backend.

GLib callback regressions replace a hash entry whose key destructor clears and
resizes the table, insert a surviving entry from a clear callback, replace the
same referenced key/value pointers, and clear a datalist from its replacement
destructor. The same cases pass against system GLib. The concurrency test also
sets, reads and removes 16 independent datalist keys on the shared list.

The autoclear fixture arms synthetic unknown feature bits in a disposable v3 header, injects repeated header failures (including a partially written header), and exercises in-place writes, allocating writes and zeroing. No guest bytes may change before the header clear succeeds. On-disk flags, retry counts, no-op requests and the absence of redundant later header writes are checked.

Pending-source lifetime tests check a ready source once and twice without
dispatching it, destroy the source, and release the context. Its finalizer must
run exactly once in each case, matching system GLib. Repeated checks must release
the previous pending references rather than silently dropping their pointers.

The file-I/O fixture checks 128-span native vectors and 1,300-span fallback requests with EOF inside a destination span, exactly at EOF and beyond it. Empty NULL spans and destination gaps are preserved; bytes after EOF must be zero. Native EOF now pads the remaining destinations after a confirmed end instead of issuing a read for each gap.

`logic-parity.asm` tests every low byte after TEST/AND/OR/XOR at 8/16/32
bits, including dirty high bits, SETP/SETNP, CMOVP, JP/JNP and full flag
readback after each consumer or branch. Both CPU Boost settings must pass
3,072 cases against a fixed parity table and independent full-flag oracle.

Main-context readiness regressions let sources destroy themselves from prepare
and check, or remove the next source during prepare. Removed sources must not
run again, later sources must still be visited, and every removed source must
finalize once. The same cases run against system GLib. The shim retains each
walk's source references in a reusable snapshot; nested walks use separate storage.

The L2 publication fixture injects repeated failures while zeroing a private cluster, including a four-byte prefix written before EIO. Cached reads must retain old data until the slot update succeeds. Raw L1/L2 lookup checks the on-disk word, retries must issue the missing writes, a later repeated zero must skip them, and reopening must match. v2/v3 snapshot copies retain their original contents and pass independent image checks. This tests publication ordering, not general transactional recovery from arbitrary metadata failures.

The child-source lifetime regression retains a child, releases its unattached
parent, and attaches/readies the child independently. It must remain usable and
must not propagate readiness through the finalized parent. The same sequence
passes against system GLib; the old shim produces an ASan use-after-free.
