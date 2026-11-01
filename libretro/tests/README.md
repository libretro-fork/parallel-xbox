# The core's tests

`make dspnative` compares 1,950,129 DSP instruction boundaries against the interpreter and asserts zero interpreter calls, including generated fallback calls and dispatcher/repeat fallback. It covers subroutine addressing, long-interrupt vectors, unsigned compares across scaling modes, peripheral moves, bit-test jumps at loop boundaries, and all X/Y-to-A/B add/subtract-with-carry forms. Carry tests include signed limits, both carry inputs and parallel X/Y stores. Magnitude compares and rotates cover edge operands and all four X/Y load/store combinations; a sweep checks all 252 legal parallel ALU encodings. MAX has an independent 512-case transfer/flags matrix, including wrapped subtraction and distinct accumulator fields. Independent expected-result cases check MACR X1,Y0,B rounding and rotate-through-carry results and flags in both engines (DSP56300 Family Manual, sections 13-165 and 13-166). Immediate and effective-address JMP/Jcc/JScc tests cover all conditions and status bytes, all 58 accepted EA encodings, untaken address updates, DO boundaries, stack-error interrupts and long-interrupt calls. It also checks lazy scaling-mode allocation, code-cache growth and bounds. Test instrumentation is compiled out of the normal core. `make dspmax` compares 100-frame MAX/REP programs against the interpreter for two memory seeds using production-size blocks, including arithmetic flags deferred immediately before MAX; `make dspjumps` checks lazy flags, conditional calls, returns and jumps inside nested loops for two 100-frame memory seeds. `make dsptcc` checks conditional accumulator and paired address-register transfers in REP and nested loops, deferred flags consumed by Tcc and MOVEC SR reads, and modifier-register transfers over two 100-frame memory seeds. The native boundary sweep covers all condition codes and status bytes, all legal Tcc accumulator selectors, paired Rn aliasing, all scaling modes and DO boundaries; MOVEC coverage includes ordinary registers and status reads. `make dsplogical` checks zero-count and wide logical shifts, long immediate OR and short immediate AND in production-size blocks and REP/nested loops for two 100-frame memory seeds. A separate expected-result matrix covers every logical-left-shift count, both accumulators, both incoming carry values, all scaling modes and signed/zero edge words, preserving the untouched accumulator fields and status bits. Repeated-entry tests mutate LA and LF without rewriting P, including a completed DO loop, to guard against stale loop state when compiled entries are reused. `make dsprelative` runs a focused 48,768-case relative-control matrix, followed by two 100-frame programs against both the interpreter and chunked JIT with budget carry. It covers short/long Bcc, BRA and BSR, signed offsets, DO target/fallthrough boundaries, long interrupts and stack errors, and register/X-peripheral bit-test branches. Native boundaries compare peripheral-read counts as well as state, preventing duplicated reads. `dspnative --relative` selects just that matrix; the default includes it in the full sweep. `make dsploops` adds 7,056 REP/DOR setup boundaries covering zero and maximum immediate counts, overlapping DO setup, 16-bit endpoint wrapping, stack errors and long interrupts. `dspnative --loops` selects just this matrix. A production-block program completes a zero-count REP (65,536 additions), a DOR that ends during setup, and nested DO/REP bodies, checked for two 100-frame seeds against both the interpreter and chunked JIT with budget carry. `make dspprogram` adds 70,288 program-transfer and invalidation boundaries: all ordinary data/accumulator/R/N/M registers, 58 EA forms, all 64 short addresses, scaling modes and source/address aliases. Three production-block programs replace a cached next opcode, a REP body and its extension; unchanged writes must preserve the cache. Each runs two 100-frame seeds against both the interpreter and chunked JIT with budget carry. `dspnative --program` selects the focused matrix. Special control-register MOVEM transfers still use the interpreter. `make dspregisterloops` checks 651,024 immediate/register setup boundaries and three production-block programs for register-count DOR/DO/REP. Sources include every named ordinary, status and stack register, all scaling modes, count truncation and accumulator limiting, nested/coincident DO boundaries, endpoint wrapping, stack errors and long interrupts. Zero-count REP executes 65,536 iterations; a nested DO LC reads the outer loop count; deferred ADD results feed accumulator-count loops. The new register matrix adds 643,968 boundaries to the full suite. Each program runs two 100-frame seeds against the interpreter and chunked JIT with budget carry. `make dspcompare` adds 8,960 compare boundaries, including an independent 56-bit subtraction/CCR oracle for signed long and unsigned short immediates, both accumulators, four scaling modes, sticky L, overflow, borrow and internal upper status bits. Negative long immediates must sign-extend (DSP56300 Family Manual, section 13-45); the accumulator stays unchanged. A two-word compare also lands on active DO endpoints. Production-block programs check deferred ADD flags followed by CMP/Bcc, REP/DOR compares and rewriting a cached extension, for two 100-frame seeds against both interpreter and chunked JIT with budget carry. `dspnative --compare` selects this focused matrix. `make dspstartup` adds 45,632 startup-operation boundaries for immediate SUB, accumulator-bit INC/DEC, OMR bit operations, immediate/control MOVEC and X:qq peripheral moves. Reserved control writes retain masking and stack side effects; control writes retire once and redispatch after scale, loop or REP-count changes. Peripheral comparisons include write addresses and values. Two production-block fixtures run 100 frames with two seeds against both interpreter and chunked JIT with budget carry. `dspnative --startup` selects the focused matrix. All these targets run in CI, including regression-only dispatches.

`test-quiet-progress.py CHECKOUT [CC]` runs the production TB-exit path against useful store-free work, verified kernel waits, device polling, interrupt release and null-TB exits. It prevents computation from acquiring synthetic timer waits.

## Frame ownership of guest time

`test-gpu-wait-deadline.py CHECKOUT [CC]` checks 345 cases against the production host dispatch and GPU-wait functions. A halted CPU, a GPU completion wait, a descriptor poll and a NAT poll must not advance guest time independently. It covers timed-wait rounding, prepare/notify races, deterministic completion and the frame owner's bounded elapsed-time advancement. Its regression model rejects the old idle-warp callback, which moved a halted guest from 1000 to 1500 ns inside host dispatch.

`test-icount-publication.py CHECKOUT [CC]` also executes the production fixed-clock initializer. It preserves raw instruction count and bias, selects the fixed Xbox rate, initializes legacy compatibility fields and creates no host-paced timers. Existing owner-accounting, I/O-boundary, restored-state and six-reader publication tests remain. These tests run in regression-only CI. The standalone QEMU CPU timing-option parser, adaptive clock governor and host/guest clock-alignment sleeps are no longer part of core initialization or CPU execution; saved-state fields retain their previous wire layout.

## DSP and guest RAM ordering

`test-dsp-guest-ram.py CHECKOUT [CC]` compiles the production APU tick and
DSP dispatcher, mix-buffer accessor and scatter/gather walker with an
independent DMA callback. Each tick must finish before guest execution resumes.
It reproduces OutRun 2's scratch growth from maximum page `0xc` to `0x6c`,
with a new 64-byte transfer at `0x12520` published before the register write.
It checks reads, writes, dirty tracking, frame cadence, scratch-page reuse and
20,000 synchronous ticks without a mix-buffer copy. An asynchronous dispatcher
would observe modified guest RAM or tables after the tick has returned.

`test-core-timers.py CHECKOUT [CC]` compiles the production clock queues and
checks 20,000 operations against an independent ordered-array oracle:
four clocks, filtered deadlines, equal-expiry ordering, pause/resume,
icount deadline selection, rearm/cancel inside callbacks and self-free.
`CORE_TIMERS_BASELINE=REV` runs the same checks on the prior timer hierarchy,
including two unused AIO timer groups. `CORE_TIMERS_CFLAGS`,
`CORE_TIMERS_BINARY` and `CORE_TIMERS_SKIP_RUN` select sanitizers or native
cross-builds.

`test-vmstop-mailbox.py CHECKOUT [CC]` compiles production VM-stop request
publication/consumption with real libretro-common threads and atomics. It
checks every stop reason (including enum value zero), latest-value coalescing,
publication before notification, exactly one consuming reader, payload
visibility and 80,000 concurrent publications. `VMSTOP_CFLAGS`, `VMSTOP_BINARY`
and `VMSTOP_SKIP_RUN` select sanitizers or cross-built binaries.

`test-nvnet-tx-borrow.py CHECKOUT [CC]` compiles the production NVNet TX loop
and network queue. It checks borrowed pointers and zero staging bytes for
complete RAM packets, queue-owned copies under backpressure/reentrancy,
queue drops, descriptor/payload aliasing, DMA fallback and fragmented
snapshots across calls. `NVNET_BORROW_CFLAGS`, `NVNET_BORROW_BINARY` and
`NVNET_BORROW_SKIP_RUN` select sanitizers or cross-built binaries.
`test-dma-map.py` also checks direct-only mapping, MMIO rejection without
reads or allocation, complete-span requirements and failed-map references;
its equivalent overrides are `DMA_MAP_CFLAGS`, `DMA_MAP_BINARY` and
`DMA_MAP_SKIP_RUN`.

`test-qemu-parking.py CHECKOUT [CC]` compiles production manual-reset events
against libretro-common parking. It forces notifications
at the prepare/commit boundaries, checks wake-all and predicate visibility,
checks event retirement before the signaler returns and shared-channel
collisions. It also reports uncontended and round-trip
timings. `QEMU_PARKING_CFLAGS`, `QEMU_PARKING_BINARY` and
`QEMU_PARKING_SKIP_RUN` select sanitizers or cross-built binaries.
`QEMU_PARKING_BASELINE=REV` compares the old Linux/Windows futex code; its
Windows test executable requires the old synchronization import library.
On POSIX, a multithreaded fork test resets inherited channel waiters, starts
a child waiter, and verifies that the parent's parked waiter remains intact.
ThreadSanitizer skips that fork case because it cannot start child threads
after a multithreaded fork; the RCU startup test also checks reset ordering.

`test-init-publication.py CHECKOUT [CC]` compiles production TCG layout and
Windows thread-name API publication. It pauses one initializer while another
finishes, stresses canonical layout publication and losing-candidate cleanup,
and checks cached API presence/absence. `INIT_PUBLICATION_CFLAGS`,
`INIT_PUBLICATION_BINARY` and `INIT_PUBLICATION_SKIP_RUN` select sanitizers or
retain a cross-built executable. The layout test stubs ABI computation; full
core instruction tests cover the actual backend layout computation.

`test-chihiro-dimm-stream.py CHECKOUT [CC]` compiles the production DIMM
serializer and bounded memory cursor. It checks legacy v1 byte encoding,
image tails, 64-bit image sizes, one-copy output, borrowed input, round trips,
truncation, malformed headers/pages and image-identity mismatches using
synthetic data. `DIMM_STREAM_CFLAGS`, `DIMM_STREAM_BINARY` and
`DIMM_STREAM_SKIP_RUN` select sanitizers or retain a cross-built executable.

`test-rcu-owner.py CHECKOUT [CC]` runs production RCU reader, request,
registry, grace-period and initialization code against deterministic
overlapping periods, new-epoch publication before the scan barrier,
notifications before waiter registration, nested
readers, lifecycle requests during a blocked
period, concurrent reclamation, TLS churn and POSIX fork reinitialization.
`RCU_OWNER_CFLAGS`, `RCU_OWNER_BINARY` and `RCU_OWNER_SKIP_RUN` select
sanitizers or retain cross-built executables. GCC TSan needs
`-fsanitize=thread -Wno-error=tsan` because it does not instrument fences;
the release/acquire publication remains instrumented. The TSan run skips
fork-child thread creation, which its runtime does not support.

`test-cpu-work.py CHECKOUT [CC]` exercises production CPU work dispatch:
eight publishers, stack-backed synchronous requests, asynchronous reclamation,
exclusive callbacks, nested dispatch, and FIFO ordering. `CPU_WORK_CFLAGS`
adds sanitizer flags. A MinGW cross-build can set `CPU_WORK_BINARY` and
`CPU_WORK_SKIP_RUN=1` to retain an executable for native Windows testing.
`test-mpsc.c` stresses eight producers and immediate node reclamation over
800000 items; link it with libretro-common's rthreads implementation.

`test-glslgen-queue.py CHECKOUT [CC]` runs the production GLSL generation
worker and submitter over 100000 jobs, checking duplicate suppression,
all shader stages, declined sources, slot reuse, flags and idle wakeups.
`GLSLGEN_CFLAGS`, `GLSLGEN_BINARY` and `GLSLGEN_SKIP_RUN` select sanitizers
or retain a cross-compiled executable for native Windows testing.

`test-shader-cache-handoff.py CHECKOUT [CC]` checks production disk-result
adoption into the real LRU: 18000 concurrent publications, hash collisions,
duplicate/live-binding protection, limited draining, and immutable disk-save
snapshots, including repeated disabled-cache shutdown joins.
File-owner checks cover borrowed interior views, the 64-mapping cap and copy
fallback, non-mapping VFS buffers, slot reuse, duplicate disposal and writer
ownership. Concurrent publication verifies that all 18000 file owners close.
`SHADER_CACHE_CFLAGS`, `SHADER_CACHE_BINARY` and
`SHADER_CACHE_SKIP_RUN` select sanitizers or retain cross-built binaries.

`test-eventcount-spin.c` checks Windows eventcount notifications before waiter
publication, concurrent publication during list detachment, and detachment
while an unlinker owns the list. It includes the production implementation.
`run.sh` runs it through the alert, keyed-event and event tiers with Wine.

`test-signed-g8b8.py CHECKOUT CORE HARNESS SCRATCH` builds isolated guest bump
and color probes and checks 75264 cases on each of OpenGL and Vulkan, including
all channel-sign combinations for the color probe, with quality overrides off
and on. Mixed LOD0 filters additionally cover magnification/minification and
sampler LOD biases of -4, 0 and +4. Three-level mipmapped probes cover all four
nearest/linear mip-filter modes, signed channels, positive/negative wrap
coordinates, upper clamps, biases -4/0/+0.25/+4 and Force LOD0, with matching
and mixed min/mag reconstruction.
Clamp-to-edge matrices add independent X/Y data and samples inside, below,
above and at opposite U/V edges, with matching and mixed filters. New
independent-axis matrices are covered at bias +0.25; `--extended-address-biases` repeats
these at -4/0/+0.25/+4 (193536 total cases per renderer). The probe builder
accepts `--mip-clamp-axis u` or `v` for these independent-axis cases.
Mirrored U/V addressing is covered against wrap, edge and mirror partners,
with matching/mixed filters and bias +0.25 (all four biases in extended mode).
Use `--mip-address-u mirror --mip-address-v wrap` for an isolated matrix;
each explicit address accepts `wrap`, `edge` or `mirror`.
Lower-clamp matrices add 12288 cases per renderer at bias +0.25: level one
across all nine address pairs with trilinear filtering, other mip filters
with wrap addressing, and level two with all four mip filters. These cover
matching and mixed min/mag filters. Positive lower clamps select the
minification filter; the GL sampler retains the original base image and
applies a minimum LOD instead of rebasing it. Mixed-filter matrices repeat
all four biases in extended mode. Level-three matrices add 6912 cases per
renderer, including all nine address pairs, matching/mixed filters, and
negative-bias mixed point/trilinear controls. These use a 16x16 five-level
chain so level three remains 2x2 and level four exercises cross-mip blending.
The 16x16 fixture coordinates move 1/64 in normalized space to avoid exact mip-zero
point-sampling boundaries; their footprint LOD increases by two. Clamps above
level fifteen retain their previous path. Use `--min-lod 1` or `2` with the
probe builder, or `--min-lod 3 --mip-base-size 16` for level three. Force LOD0 overrides the guest lower clamp and
retains mixed min/mag selection. Nonzero lower-clamp reconstruction uses a
new absolute-LOD tag in the existing stage byte, keeping legacy GL seed
programs distinct. Zero-lower-clamp shader keys remain unchanged.
Extended lower-clamp matrices add 6912 cases with a 32x32 six-level chain,
a 2x2 mip four and 1x1 mip five. Use `--min-lod 4 --mip-base-size 32`.
The runtime supports effective minima through fifteen. An otherwise unused
combination of per-axis edge flags marks a seven-bit dense code for minima
four through fifteen and all nine address pairs, preserving existing state
layout, keys and shader seeds. Only extended keys use this encoding.
The 32x32 fixture shifts normalized coordinates by 1/128, avoiding both
point and gather boundaries after Force LOD0; its footprint adds three to LOD.
Use `--extended-mip-minima` to add 13824 cases at minima five and six:
64x64/seven-level and 128x128/eight-level textures keep the selected lower
mip at 2x2 and the next at 1x1. They cover all nine address pairs, all four
mip-filter modes, matching/mixed filters, bias +0.25, negative-bias controls
and both override settings. The optional total is 89088 cases per renderer,
or 242688 with `--extended-address-biases` too. Default totals stay unchanged.
The builder accepts `--min-lod 5 --mip-base-size 64` or
`--min-lod 6 --mip-base-size 128`. Larger fixtures use independent texture
slots below 48 MiB, separated from the target, scanout and gradient buffers;
compile-time bounds enforce the 64 MiB limit. Their normalized coordinate
shifts are 1/256 and 1/512, away from mip-zero point/gather boundaries.
These guest probes add no normal-emulator code or logging.
Non-neutral bump-luminance profiles add scale/bias (0.5, 0.25), negative
scale (-0.5, 0.75) and saturation (3, -0.5). Each profile covers all 16 channel
sign masks, point/linear filtering, three coordinates and linear/swizzled
single-level G8B8 layouts. Luminance uses the original unsigned R sample while
displacement uses the selected signed B/G components. Use the builder's
`--luminance-profile scale_bias`, `negative_scale` or `overbright` to isolate
a matrix. The three profiles add 1152 cases per renderer including override
off/on runs; they leave normal emulator execution unchanged.
Cross-mip comparisons allow two color LSBs for native LOD precision; original
single-level cases still require exact backend agreement. `run.sh` runs it on Linux
when Vulkan is available. See `docs/bump-filter-probe.md` for the model and
hardware-validation limits. The test code is absent from normal emulation.

`test-g8b8-sampler-views.py CHECKOUT [CC]` checks production GL sampler-view
classification, 3840 filter-mode transitions, 64 reuse gates, GL parameter
selection and unchanged cache-key/binding layouts. Another 294912
Vulkan cases check the quarter-LOD ceiling, nearest mip selection, and
exclusions by address, filter, minimum/maximum, format, cube/border and dimension. Mip and nonmip views of
the same image must remain separate when their base/minimum settings differ.

`test-g8b8-nonmip-transitions.py CHECKOUT PARENT CANDIDATE HARNESS SCRATCH`
compares 3072 nonmip pixels per renderer against the parent after priming the
same image with mip filtering. It checks each backend's existing behavior;
it does not require unsupported nonmip-chain sampling to agree across APIs.
Use `--assembler yasm` when nasm is unavailable.

`test-linear-texture-shape.py CHECKOUT [CC]` compiles the production shape
builder and format table: 403,200 linear cases cover every supported linear
format, all raw mip counts, clamp extremes, dimensions, pitch, border source,
texture stage and Force LOD0. Another 15,360 swizzled cases retain their mip
counts and clamps. Another 3,840 cube cases verify that Force LOD0 changes
only sampling clamps, retaining the complete stored-chain metadata.
`test-g8b8-linear-count.py CHECKOUT PARENT CANDIDATE HARNESS SCRATCH` checks
7,296 scalar cases, 2,304 nonmip aliases against the parent's count-one images,
and 1,920 unchanged count-one/swizzled pixels per backend. Raw counts 0, 2 and
15 cover mixed/matching filters, bias, border addressing and Force LOD0.
`build-bump-probe.py --linear-mip-count N` changes the raw register field while
keeping `--linear-level` storage fixed at one image. Linear texture shapes
ignore raw mip counts and clamps; signed G8B8 shader selection follows that rule.

`test-native-color-sampler.py CHECKOUT [CC]` checks 381,024 production Vulkan
sampler policies across supported formats, image levels, min/mag modes, clamp
values, dimensions, cubes, embedded borders and format filtering support.
`test-native-color-cubes.py CHECKOUT PARENT CANDIDATE HARNESS SCRATCH`
checks 10,368 scalar cases and 3,648 exact parent pixels per backend.
RGBA8/RGB565 cubes have distinct face colors, two palette bands and contrasting
lower mips. Controlled face-interior footprints cover all six directions,
including Force LOD0 with intact chains. All four mip modes, guest clamps,
zero/positive LOD boundaries and matching filters are included. Full chains,
positive lower clamps, 2D color and G8B8 remain parent controls.
`--native-cube-face 0..5` selects +X/-X/+Y/-Y/+Z/-Z with the native mixed
geometry; `--native-matching` selects matching native min/mag filters.
The cube reference carries explicit projected LODs and permits one color LSB
of interpolation rounding between APIs; parent comparisons stay bit exact.
The production sampler change covers swizzled color cubes, with no shader
changes. Depth, linear-layout cubes, embedded borders, convolution, unsupported
linear filtering and G8B8 keep their prior policies. Force LOD0 uses the same
shared clamp as 2D textures and preserves the stored chain and cube-face layout.

`test-native-color-base-clamps.py CHECKOUT PARENT CANDIDATE HARNESS SCRATCH`
checks 7,680 scalar cases and 3,264 exact parent pixels per backend.
RGBA8/RGB565 cover all four mip modes and both magnification filters, single
images, guest maximum zero, Force LOD0 with an intact chain, LOD boundaries
and extreme biases. Full mip chains, positive lower clamps, existing nonmip
sampling and G8B8 are parent controls. Quality filtering is disabled to isolate
the clamp from anisotropic filtering of matching linear filters.
`--native-mip-filter 3|4|5|6` programs the native probe's min mode and alternates
matching/mixed mag modes; `--native-base-only` programs guest maximum zero.
Native minimum one (`--min-lod 1`) requires an intact native mip chain.
Unclamped native mip controls use per-backend parent comparison rather than
the base-level scalar oracle. Both effective sampler clamps must be zero
for the new mip-filtered color path; positive lower clamps retain their policy.

`test-native-color-lod0.py CHECKOUT PARENT CANDIDATE HARNESS SCRATCH` checks
5,376 native RGBA8/RGB565 scalar cases and 1,344 exact matching-filter/G8B8
parent pixels per backend. Linear and swizzled layouts, six biases, Force
LOD0 and contrasting lower mip data expose both filter-choice and accidental
mip-blending failures. `build-bump-probe.py --native-color [rgba8|rgb565]`
uses 16 independent palettes with `--mixed` or `--color`;
`--native-mip-chain` retains nonmip filters with a contrasting second image level.
The Vulkan policy follows the Khronos nonmip emulation rule: a 0.25 LOD ceiling
preserves the min/mag choice, and nearest mip selection keeps reads at zero.
Depth, linear-layout cubes, embedded borders, convolution and existing G8B8 policies remain excluded.

`test-g8b8-single-level.py CHECKOUT PARENT CANDIDATE HARNESS SCRATCH` checks
one-level swizzled G8B8 images requesting mip-filter modes 3 through 6.
It covers mixed min/mag, all wrap/edge/mirror/border pairs, LOD bias,
Force LOD0, border colors and address-only transitions from an excluded mode.
With `--custom-border-colors`, each backend checks 17,280 scalar cases
and 3,072 exact parent pixels for matching filters and existing mip chains.
The custom-color flag requires Vulkan custom-border-color support.
Add `--linear-layout` to run the same model matrix on row-major G8B8 images;
multi-level controls stay swizzled and two existing nonmip fixtures bring
the parent comparisons to 3,456 pixels per renderer. The builder option
`--linear-level` requires a 4x4 single-level probe with explicit addresses.
Linear mip-filter requests reuse the existing nonmip reconstruction keys.
Vulkan retains a positive quarter-LOD ceiling and nearest mip selection
for their mixed min/mag filters, so the choice remains observable while
all texels come from level zero.

`build-bump-probe.py --single-level` declares one mip without changing the
underlying texels; `--prime-address` first draws with legacy clamp so that
a later address-only write must invalidate the shader key.

`test-g8b8-base-border.py CHECKOUT PARENT CANDIDATE HARNESS SCRATCH` checks
G8B8 constant borders when mip chains are clamped to zero, with guest
`--base-only` clamps and Force Texture LOD0. `--mode matching`, `mixed` or
`all` (default) selects the matrix. Black-border mixed cases cover all seven
border-containing address pairs and all four mip modes. Bias -4/0/+0.25/+4
checks min/mag selection while reads stay at mip zero. Matching cases cover
both linear mip modes across all pairs and border/border point controls.
Opaque white adds standard nonzero borders; `--custom-border-colors` adds two
independent R/G profiles and requires Vulkan custom-border-color support.
Matching mode checks 6912 scalar cases (8448 custom), mixed mode 13824
(15360 custom), and all mode 20736 (23808 custom), per renderer. All modes
compare 1920 full-mip pixels with the parent. Mixed mode adds 1536 exact
matching-filter controls, for 3456 parent-comparison pixels per renderer.
The scalar reference includes both axes' border coverage and the native G8B8
R/G swizzle. `--border-color 0x40c02080` isolates a colored builder case.
Mixed borders reuse the single-level mixed keys; previously supported mixed
nonborder mip keys remain unchanged. Vulkan's 0.25 sampler ceiling preserves
min/mag selection, with nearest mip selection keeping all reads at level zero.
Mixed borders with a positive minimum, full-mip borders, color keys, embedded
borders and signed alpha-kill retain their exclusions. No uniforms or state
fields are added. This manual parent/candidate test is separate from the
default general scalar runner.

Lower-clamp scalar matrices first prime a nonmip view, then draw with mip
filtering. The builder's `--sampler-transition lod0-to-mip` selects this
sequence. The reverse `mip-to-lod0` sequence needs the parent-comparison
runner above rather than `check-bump-probe.py`.

`test-signed-g8b8-state.py CHECKOUT [CC]` checks the production shader address
invalidation predicate over 262144 register combinations and verifies that
signed-channel, min/mag, mip-count and mip-size changes invalidate shader state.
It also checks 768 mip-address eligibility combinations, including minimum
clamps and both matching/mixed filters. Reuse of the reserved universal
byte preserves the
serialized pixel-shader state size and field offsets.
Another 8192 cases verify that matching, base-only wrap/edge/mirror/border filters reuse the
single-level point/gather shader keys without clearing other stages' flags. Another
3584 cases check mixed border reuse of single-level mixed-filter keys.
An additional 9216 cases verify independent-axis flags and preserve other stages;
3072 more check lower-mip bitplanes, matching/mixed canonicalization and
other-stage preservation without altering the serialized layout. Another
110592 cases check all extended minima, address pairs, signs and filter
combinations, including preservation of other stages.

`test-g8b8-higher-lod.py CHECKOUT [CC]` compiles the production mip shader
generator and checks 2592 minimum/address/filter/API combinations. It verifies
that every extended code emits its absolute lower clamp, correct address
handling and fixed minification reconstruction. It uses the core's GLib
development dependency through `pkg-config`.

`run.sh` runs the automatic suite. Parent/candidate GPU comparisons are
invoked separately with explicit core binaries. The extracted-code tests (`test-*.py`, which
compile production functions into a fixture) run for either core; the ones
that need Mesa's GL on EGL, and `test-mcpx-hle.py`, run with the Linux core;
the Vulkan copy, depth-clear and vertex-fetch tests run with the Linux core
where there is a Vulkan device. Each can also be run on its own, as below.
Two more are built by
the Makefile and run on their own: `make s3tctest` (the S3TC decoder against
its oracle) and `make fileiotest` (the disk file layer; it takes a scratch
file's path).

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
finds over every phase of the APU's engine frames against the video frames,
retaining a conservative eight-frame production-delay allowance
(`MCPX_APU_MONITOR_FRAMES_IN_FLIGHT`): the queue's preroll
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

`python libretro/tests/test-spv-depth.py . gcc --gpu` validates the immutable
depth/stencil kernels, three-buffer/16-byte push layout and allocation-free
four-pipeline setup/teardown. It exercises 400,000 lookups without driver calls,
100,000 bounded grids and exhaustive small-grid coverage. GPU checks compare
802 original/direct/CPU cases, including 128/256 workgroups, integer/float depth,
pack/unpack, 1/2/3/9x scaling, partial workgroups and guarded source/output
padding. A 5760x4320 case caps the X count at Vulkan's minimum limit to exercise
2D dispatch on hardware with higher limits too. The oracle uses a CPU snapshot,
not scalar reads from uncached mapped VRAM. Alternating timestamp batches
report one-million-pixel conversion cost without asserting a performance
threshold; these do not measure whole-core latency. Static modules pass
spirv-val for Vulkan 1.0; GPU readbacks and reference compilation are test-only.

`python libretro/tests/test-spv-clear.py . gcc --gpu` checks the immutable
fullscreen vertex and solid-clear fragment modules against the pre-port GLSL
reference. The production initialization/finalization functions pass the exact
static pointers to Vulkan, allocate no core-side objects and destroy each handle
once. Reflection independently verifies that no descriptor or push-constant
layouts are needed. The optional GPU test checks 88 combinations of RGBA8/float
raster output, viewport/scissor cases and indexed/non-indexed vertex indices
against a CPU oracle and the original shaders in both SPIR-V versions. The
static modules additionally pass spirv-val under Vulkan 1.0. Tests use bundled
Volk and report validation-layer availability; GPU readbacks are test-only.

`python libretro/tests/test-spv-geom.py . gcc --gpu` compares the direct
line, triangle and quad emitters with the production GLSL generator/compiler.
It checks both selectors, deterministic words and one payload allocation;
SPIR-V 1.5/1.6, flat/smooth colours, both depth modes, all winding pairs and
first/last/strip/fan provoking rules cover 8 line, 288 filled-triangle and 16 filled-quad variants, plus 576 triangle
and 32 quad wireframe/point variants. Those check synthetic edge positions,
face-to-edge depth slopes and repeated point depth positions. Quad checks cover both independent
depth planes, the fourth-input provoking colour, and untouched capture records
for skipped odd quad-strip invocations.
The optional GPU run captures every geometry output before EmitVertex and
compares old/new values bit for bit (NaN payloads may differ), then checks an
independent CPU oracle. Only the CPU slope oracle tolerates GPU division
rounding. Test-only capture writes/readbacks never enter the renderer.
Installed spirv-val checks original, direct and instrumented modules. The
Vulkan test uses bundled Volk and reports whether validation layers exist.
Compile microbenchmarks exclude GLSL generation and driver PSO compilation.

`python libretro/tests/test-shader-layout.py . gcc` checks compiler-provided
uniform and push constant layouts against independent expected offsets, sizes,
array and matrix strides under SPIR-V 1.5/1.6. It checks that requesting layouts
leaves shader words unchanged and that modules share immutable descriptors
while owning separate upload buffers. `--reflect-dir /path/to/SPIRV-Reflect`
optionally compares every case with an external reference checkout; reflection
is not needed to build the core or run the standard tests. `--shader-dir DIR`
also checks every GLSL module exported by `seedshaders` under both SPIR-V
versions. The seed tool itself requests and releases compiler layouts.

`test-vk-worker-modules.py` exercises source/layout reference counts, cache
collisions, byte limits (including metadata), queue drops and 800000 concurrent
reference pairs. It also passes the exact original word pointer to module
creation and verifies shared descriptors survive freeing words, eviction and
independent module destruction. Fixed shader ownership tests verify direct
compiler-buffer borrowing without a byte-array wrapper.

The fixed clear/depth/swizzle tests inspect their known SPIR-V resource
bindings and push offsets directly with `spv-test-layout.h`; installed
`spirv-val` independently validates their saved modules.

`test-texture-dirty.py CHECKOUT [CC]` compiles the actual OpenGL and Vulkan cache-invalidation functions against a page-dirty RAM oracle. It covers the final VRAM byte/page, palettes sharing cleared pages, writes crossing a page boundary, and empty spans. The standard runner executes it alongside the VRAM range test.

The GPU alias sequence also tests the last VRAM page with `xemu_memory=64`: frame 830 samples a linear Y8 texture ending at 64 MiB (white), frames 850/870 sample two cached views after a shared-page CPU write (black), and frames 890/910 sample a 256-entry palette ending at 64 MiB before/after a CPU update (white/black). These checks run on OpenGL and Vulkan at scales 1, 2 and 4.

GPU alias frames 930/950 sample an indexed texture after GPU clears write red then blue into the palette surface. CPU palette RAM starts black and the indexed binding is already cached, so these checks detect missing GPU palette synchronization and stale cache revalidation. Both APIs run at scales 1, 2 and 4.

After the GPU palette updates, guest frames 960 through 999 rebind the same indexed texture and draw without modifying its palette. Captures at frontend frames 970, 990 and 1005 must remain blue. A separate temporary transfer-counter audit measured actual palette readbacks and indexed uploads across this sequence; those counters are not part of the shipped core or the standard visual regression assertions.

`python libretro/tests/test-vk-vertex-copies.py CHECKOUT [CC]` runs the
production vertex range merge, dirty/COW dispatch and remap copy against
independent page-union and byte-fetch oracles. It covers 100,000 interval
sets, 10,000 zero/nonzero sync-queue cases and 20,000 multi-attribute copies
with output guard bytes, packed/strided sources, nonzero first vertices and
empty ranges. Skipped prefixes remain untouched; remap metadata and binding
indices stay intact. It checks that merged ranges stay in the input array
and packed attributes use one copy; undefined-behavior traps are enabled.
No graphics driver is needed.

`python libretro/tests/test-vk-vertex-formats.py CHECKOUT [CC]` exercises
production capability initialization, array remapping and inline conversion
with forced unsupported formats. It checks startup-only device queries,
guest dword padding, staging rotation, provoking values and all 65,536 signed
short values. The renderer's remap descriptor remains 400 bytes.
`test-vulkan-vertex-formats.py` additionally fetches converted attributes in
real vertex/indexed draws under synchronization validation, checking signed,
normalized and default components. Its probe shader is embedded; no external
shader compiler is needed to run the test.

`test-vk-query-scope.py CHECKOUT [CC]` executes production query/pass control
against a delayed GPU model, including recycled available slots, report
boundaries, clears, disabled counting, pass breaks and cost-model queries.
`test-vulkan-query-scope.py` runs the same workloads with real Vulkan draws
and synchronization validation. Host-reset devices keep 237 consecutive query
windows in one render pass with one pipeline bind; device-reset behavior is
also checked. Embedded shaders need no external compiler. Test fence waits
are confined to the standalone fixture; no wait is added to the core.

`test-tb-lookup-state.py CHECKOUT [CC]` runs production indirect-branch lookup
with Xbox direct dispatch and generic target dispatch. It checks boost on/off,
32/64-bit PCs, flag combinations, single-step/one-instruction/no-chain modes,
lookup hits/misses, logging and breakpoint exits against independent results.
The Xbox path must bypass the target function pointer in both boost modes.
The dispatcher is built with the single x86 target and shares an inline state
calculation with the generic target hook. Release Win64 disassembly confirms
that both dispatcher and indirect-branch paths avoid the out-of-line state
return call; the two callers grow by 288 bytes in the inspected build.
This code-generation result does not establish a game FPS improvement.

`test-rdpmc.py CHECKOUT [CC]` executes the production RDPMC helper and
translator against privilege, counter-index and intercept-order oracles over
100,144 cases. It checks that only EAX/EDX change on success, fault/intercept
paths preserve registers, no diagnostic is logged, and TCG models a returning
helper that writes guest state. The Xbox's unmodelled counters 0/1 read zero;
unsupported indices raise #GP(0), matching the P6 index rules in Intel's
[Optimization Reference Manual, B-14](https://download.intel.com/design/PentiumII/manuals/24512701.pdf).
This does not emulate event counting or test the FastCPU sample end to end.

`test-x87-load-precision.py CHECKOUT [CC]` checks real SoftFloat load helpers
and executes the production hard-FPU TCG generators with a native arithmetic
evaluator. It compares loads against exact native widening and arithmetic
against SoftFloat across defined PC modes, all four rounding directions and
boost on/off. It covers 36,060 soft-load cases, 136,000 hard-cache cases,
integer conversion, moves/exchanges, comparisons, square root, flushes and
the boost scratch-load bypass. It checks exception/precision restoration and
special soft inputs too. The evaluator does not execute a translated guest
block; `rom/x87-load-precision.asm` executes the load/move/conversion and
arithmetic checks in translated guest code, including a helper boundary, under
all four hard-FPU/CPU-boost combinations in `run.sh`. The Windows core also
passes the existing x87 scratch and status ROMs in all four combinations with
strict frame/audio cadence. Hard-FPU retains its existing binary64
range/precision approximation.

`test-win32-raw-socket.py CHECKOUT [CC] [--build-only EXE]` exercises production
Windows AIO registration for raw Winsock handles and CRT-wrapped descriptors,
callback readiness masks, replacement/removal and failed-registration rollback.
Non-Windows builds use a single-thread model. Native Windows builds also run
real nonblocking loopback UDP through the production prepare/pending/dispatch
functions, and reject a real invalid socket handle. Descriptor classification
and CRT-handle translation are modelled in this fixture. The standard runner
also builds/runs the Windows probe under Wine for Windows-core tests; the
one-second event wait belongs only to the standalone probe.

`test-surface-blend.py CHECKOUT [CC] [--gl]` checks 16,384 target/factor/enable
combinations through production blend mapping, Vulkan pipeline keys/dirty
checks and dynamic state, and the OpenGL state-cache/blend block. It covers
format-only changes between XRGB and ARGB sharing a host image format, and
equivalent factors reusing a static PSO key. `--gl` additionally runs 96 real
draws through the GL state block with stored destination alpha 0, 64 and 255,
comparing RGB against an independent blend equation. The GL probe needs EGL
and OpenGL development packages and a surfaceless EGL driver.

`test-conexant-counter.py CHECKOUT [CC]` executes production encoder access,
device callbacks, VMState fields, NV2A vblank and the frame-loop dispatch block.
It checks all 256 register values, nibble wrap, upper-bit preservation, pending
interrupts, paused/before-vblank/duplicate dispatches and SMBus read cursors.
The descriptor-based state round trips cover the parent SMBus state, all encoder
registers and the command cursor; detach/recreate checks cover the cached device
pointer. This fixture does not run a commercial game's startup or the full QEMU
migration stream.

`test-barycentric-overflow.py CHECKOUT [CC]` executes the production depth
GLSL on a surfaceless OpenGL compute context against a double-precision oracle
and the original arithmetic path. It checks 4,096 cases through float32's
largest finite exponents, perspective/linear interpolation, 1x/2x/4x scale,
ordinary fragment positions and degenerates. Non-overflowing results must be
bit-identical to the old path. It also compiles the relevant fragment code as
GLSL 4.00, and runs the core's compiler for SPIR-V 1.5 and 1.6. Installed
`spirv-val` and `glslc` additionally validate those modules and the probe source.
This requires EGL/OpenGL development files and an OpenGL 4.5 context. It tests
depth arithmetic rather than rasterization of extreme coordinates on real GPUs.

`test-gl-shader-version.py CHECKOUT [CC]` exercises the production GL cache
version writer/reader and module-key header parser. Current files round-trip;
legacy or changed generator/upstream versions and truncated prefixes fail.
Both stage-module and monolithic GL caches use the generator revision, so a
fork-only shader fix cannot silently load an old binary at the same upstream
version. Existing seed dictionaries remain usable for rebuilding the binaries.

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
model of the GPU's thread: one request, with immediate return for shared-device
Vulkan/GL and acknowledgement for legacy Vulkan interop. 864 combinations cover pending
work, racing/spurious wakeups, stopping, no GPU thread and both Vulkan flags.
Guest time is not advanced. It also extracts the production request publisher
for 40,000 requests, including INT_MAX wrap, missing/stopped owners and one kick
per accepted request.

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

The RCU startup regression extracts the production initializer, callback queue, worker claim and fork-child hook, plus the actual GLib once implementation. It pauses the sole consumer after batch detachment, checks concurrent initialization publication, 100,000 repeated initializations, 16 competing consumer entries and the fork-child owner reset. Fork coverage is a callback model, not a kernel fork on Windows.

`test-rcu-callbacks.py CHECKOUT [CC]` executes the production callback worker and publication code with libretro-common MPSC/eventcount primitives. A callback queued after detachment must wait for a distinct grace period; eight producers then publish 200,000 callbacks that free their intrusive nodes immediately. It checks per-producer FIFO, payload publication and a single consumer despite competing worker entries. `RCU_CALLBACK_CFLAGS`, `RCU_CALLBACK_BINARY` and `RCU_CALLBACK_SKIP_RUN` select sanitizers and cross-built native executables.

`test-win32-thread-exit.py CHECKOUT [CC]` compiles the production Windows notifier registration and cleanup. Sixteen foreign threads register TLS-backed notifiers simultaneously, drain their own callbacks before returning, and reuse the same TLS cells for a second session. It also checks removal, repeated drains and QemuThread-owned lists. `WIN32_EXIT_BINARY` and `WIN32_EXIT_SKIP_RUN` support cross-builds followed by native execution.

`test-qemu-file-memory.py CHECKOUT [CC]` compiles the complete production state cursor against golden byte sequences and bounds/error oracles. It checks big-endian scalars, every truncated scalar width, output canaries, empty buffers, overflow offsets, borrowed views larger than the old 32 KiB window, counted strings and Error ownership. A 4 MiB output payload must incur exactly its final copy, and borrowed input views must copy nothing. `QEMU_FILE_CFLAGS`, `QEMU_FILE_BINARY` and `QEMU_FILE_SKIP_RUN` support sanitizers and native cross-built tests. The cursor is caller-owned: it allocates no channel or scratch storage.

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

`test-win32-tls.c` exercises the production ordinary TLS macros with 32 native
Windows threads and delayed key allocation. Exactly two canonical keys remain; losing private candidates must be freed;
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
FP flag-capture tests execute emitted float/double comparison snapshots for
524,288 special/random operand, MXCSR and result-register cases per target
width, comparing native flags/status with PUSHF/POP. They cover 64-bit RAX
preservation, high-byte encoding without REX, simulated CPUID leaf bounds
and cached feature selection, plus 32-bit/old-host fallback. The x87 scratch
ROM also keeps six memory-loaded guest GPRs live across ordered/unordered
comparisons in both precision modes. No fixed RAX allocation is imposed.

Writable file-I/O checks zero an unaligned range across EOF, a range entirely beyond EOF and an INT64_MAX-length range, confirming unchanged size and preserved bytes before the range. Empty, negative-offset and maximum-offset requests cover the no-op/validation paths. Sparse operations enforce non-extension natively; fallback writes still clip using the current file size.


Windows UTF conversion tests cover bounded ASCII/non-BMP prefixes, explicit
lengths on non-terminated buffers, empty input, consumed/written counts and the
normal NUL-terminated path. The same vectors run against system GLib on Linux;
the Windows-only shim wrappers are exercised natively on Windows.
`mmx-tags.asm` checks all eight TOP positions at 24/64-bit precision,
compares full FXSAVE snapshots across EMMS except for the emptied tag byte,
and checks EFLAGS, repeated exits, new x87 pushes, pending arithmetic and
MMX entry. It runs with both hard-FPU and CPU Boost settings.

The POSIX file-sync fixture redirects only the production backend sync syscall. Two EINTR results must be followed by a real successful sync for both explicit flush and writable close; EIO must propagate after one attempt. Real file readback is checked, and read-only close must issue no sync. The runner skips this POSIX-only fixture on native MinGW/MSYS.


Descriptor-mapping regressions start at a nonzero position, map the whole file,
and verify the next caller read, descriptor ownership and retained contents after
close. Writable copies must not alter the file; empty-file and mapping-error
paths must preserve position too. These cases also run against system GLib.
`arithmetic-parity.asm` exercises 73,728 cases of ADD/ADC/SUB/SBB, INC/DEC,
NEG and SHL/SHR/SAR at 8/16/32 bits, both input flag seeds, all low bytes,
and three high-bit patterns. SETP/SETNP, CMOVP/CMOVNP, JP and JNP must match
a fixed parity table (incoming PF for zero shifts), while full flag snapshots
must match the same operation before a predicate. Two extra cases keep native
x87 arithmetic live before an arithmetic PF consumer. Both FPU/Boost settings run.


Callback-data lifetime tests replace a callback, destroy its source, or replace
it from recursive dispatch. The old payload must remain alive until all active
calls finish and its destructor must run once. The same vectors run against
system GLib. Callback state allocates on registration, not on each dispatch.

The qcow2 tail-conversion fixture covers 336 v2/v3, 512-byte/64-KiB cluster, compressed/uncompressed and logical-length cases. Nonzero padding beyond virtual EOF must not force a zero-data write; a nonzero last valid byte must survive. Instrumented production conversion counts payload read bytes and output writes, and real-file readback checks exact size/content. Compressed clusters still decompress completely. Fixtures are minimal read-only conversion inputs, not refcounted writable images.
`test-byte-carry.py` evaluates production entry-carry IR against all unsigned
byte operand pairs with eight dirty upper-bit combinations per target width,
checks that lazy operands remain unchanged and the FP flush occurs, and checks
all eight metadata encodings. `sub-byte-carry.asm` tests all 65,536 operand pairs
through direct/shared entries, ADC, INC, SETC and JC, full flag readback and two
live x87 cases. The mixed-class CF/STI ROM also covers byte SUB/CMP/NEG.

The qcow2 vector fixture rejects negative counts, missing descriptor arrays and, on 64-bit size_t hosts, oversized lengths and signed-overflowing totals. Counted file calls must remain zero. Valid empty requests, including at virtual EOF, remain accepted. The same checks run with pending autoclear flags and must leave them untouched. Synthetic huge spans use no allocated payload because they must be rejected before access.

The IDE cancellation test extracts the actual cancellation and buffered-completion functions, using the production QLIST implementation and a modeled callback/drain boundary. Eight scenarios cover recursion, draining/freeing requests from a callback, a newly submitted request, existing orphans, unbuffered draining and the accepted request limit. Callbacks must run once, canceled data must not be copied and request/buffer frees must balance. Linux runs use ASan/UBSan; the same cases can run as a native Windows executable. This models callback lifetime behavior, not a full device or block backend.

The IDE callback model also completes a read successfully or with EIO before canceling the remaining reads from its callback, and submits a fresh read from a success callback. Completed requests must receive only their original status, success data must be available inside the callback, and the completing request/buffer must remain owned until the callback returns. All eleven scenarios retain balanced cleanup.
`entry-compare.asm` checks 396,288 same-block versus entry relations across
byte operand pairs, 32-bit boundaries and addition fallback classes, plus two
live x87 cases. SETcc,
CMOVcc, Jcc and shared entries must agree and preserve full flags. `sahf.asm`
checks all AH bytes across 15,104 logical/arithmetic/shared-entry scenarios,
OF/DF and GPR preservation, plus two live x87 cases. Both run under all four
hard-FPU/CPU-Boost combinations; shared entries guard against unsafe ANY aliases.

`entry-invalidate.asm` checks variable-count shifts after SUBB/SUBL/ZERO
entries, including zero counts, carry reads, signed predicates and SAHF overflow.
The old Boost core fails the SHL carry case; Boost-off passes. Dynamic-to-dynamic
flag updates must invalidate the entry proof before the class-equality shortcut.

`test-shift-class.py` checks the production retention predicate against all
256 masked count outcomes and every incoming flag operation in both target-width
models. ANY and invalid proofs are rejected. The entry invalidation ROM includes
both unchanged-class paths (with a post-shift jump) and class-changing negatives.

The Linux native-file short-transfer test uses GNU linker wrapping around actual
POSIX file calls. It checks 3,388 combinations of partial vector/scalar transfers,
EINTR, EOF, zero progress and terminal errors, including resumed offsets, gaps
and complete write readback. It does not model the Windows file API.

`test-tb-cache-flags.py` executes production flag-check IR against an independent
exact-or-ANY oracle across class and mode mismatches. ANY-only requests must reject
every specific class. The entry invalidation ROM repeats mixed-class shared targets
and flag-overwriting targets to exercise both cache rejection and safe ANY hits.

ANY means code independent of the incoming flag class, not independent of guest
flag values; such code may still read flags through the normal runtime helpers.

`test-tb-secondary.py` executes the production primary and secondary cache
stores, lookup, promotion and invalidation paths with Boost on and off.
Population starts only after a same-address class conflict; empty and address-
collision misses keep the original path. It checks class-dependent entries,
independent primary entries, identity
mismatches, code invalidation and page/full flushes across two CPU caches.
The two-page ROM alternates carry classes into an ADC-dependent crossing
instruction block before second-page remaps, writes to both code pages and
an instruction-fetch fault.

The `test-fixed-shader-ownership.py` fixture extracts the production fixed-module
creation path. Across 10,000 calls it checks that native creation receives the
compiler payload pointer and exact byte length, that the words remain unchanged,
and that the owning byte array is released exactly once after native creation.
The GPU-probe callers use this path because their layouts are defined by the
host; guest shader reflection remains enabled. Swizzle kernels now use immutable
SPIR-V directly.

`test-spv-swizzle.py` validates the immutable swizzle/unswizzle SPIR-V with
`spirv-val` when available, checks stable borrowed word pointers and the two
storage buffers plus 44-byte push layout. With `--gpu`, it compares the original
GLSL and direct modules against an independent CPU Morton-coordinate interleaver
for 1/2/4-byte textures, palettes, 3D levels, narrow and large dimensions, source
alignment, partial output words and complete input/output guards. It also reports optional GPU timestamps for
512K-texel conversion batches; these measure kernel time, not core latency. GPU readback
and reference compilation belong to the fixture only. Runtime compilation is
removed from the production swizzle kernels; GPU-probe GLSL still uses the
reflection-free fixed-module path.

`test-vk-report-storage.py` extracts the production asynchronous report poller,
its report type, capacity and result-storage declaration. A delayed GPU model
checks the exact destination pointer, range, stride and nonblocking flags.
Across 27,000 polls it checks clear/report ordering, partial submissions,
out-of-order availability, repeated unavailable polls, scales 1-9, 32-bit
accumulator wraparound, full 1,024-slot batches, queue drain/reset and storage
guards against a separate CPU model. Allocation wrappers verify the poller
makes no heap allocations. Completed nodes return to the Vulkan report pool.
The fixture also executes the idle-wait path: 10,000 one-slot completions skip
the duplicate query-result poll. Multi-slot ranges, intervening submissions,
wrong-slot caches, driver errors, no-work waits and pool reset/reinit guards
retain ordinary polling or invalidate the saved pair as appropriate.

`test-vk-report-pool.py` executes production report initialization, clear/get
requests, polling and finalization with the actual shared `ObjPool`. Allocator
spies check 72,186 requests, allocation-free reuse within the reserve, FIFO
fields and writes, delayed completion, overflow allocation/retirement, teardown
with pending reserve and overflow nodes, and reinitialization with cost queries
both enabled and disabled. No queue limit or synchronization is added by pooling.

`test-vk-parked-readback-batch.py` runs production range selection, older-writer
ordering, batching, settlement and callback retirement against the previous
per-image behavior. Across 2,000 generated range workloads it checks exact RAM,
copy order, live-range learning, final dirty/owed state and callbacks; synchronous conversion
fallbacks must not increase waits. The all-direct 25-producer fixture reduces
25 modeled waits to one; 130 producers split at the real 64-entry batch capacity
into three waits. Four no-copy retirement cases verify pending copies settle
before callbacks are disarmed. These are scheduling-model counts, not measured
whole-game latency improvements; no image transfer or hashing bytes are omitted.

Indexed input batching extends the producer model with 2,000 ordered pairs of
ranges, including overlap and shared producers. Final RAM, copy order, callback
state and live-range learning must equal two sequential materializations,
without increasing modeled waits. A palette/index fixture reduces two waits to
one with identical transfers. `test-vk-indexed-inputs.py` exercises 128
production eligibility decisions and exact range order: compatible single-level
surfaces and possible GPU cube paths keep palette-only handling; known RAM
fallbacks combine both inputs. Palette hashing still waits for RAM visibility.

`test-vk-ring-pressure.py` executes production aux submission, fence retirement,
ring-tail selection and allocation with byte-level ownership and payload guards.
An unfinished earlier submission must cause zero fence-status calls across
1,000 fitting allocations (the old eager loop made 1,000 calls). Under pressure,
retiring one completed slot must not poll another slot when the gap now fits.
Deterministic cases cover wrapping, delayed completion, staging growth,
unswizzle exhaustion and full wrapped spans whose endpoints are equal. Across
20,000 delayed submissions and more than 100,000 allocations, neither ring may
overwrite a current or unretired recording. A private run against the previous
allocator reproduced the equal-endpoint overwrite; these are ownership and
driver-call checks, not whole-game latency measurements.

`test-vk-arena-flush.py` extracts production host flushing and verifies exact
non-coherent dirty-tail ranges, zero allocator calls for coherent/unchanged
arenas, clean state after a flush, reset/reuse and host flushing before queue submission. The COW-tail fixture checks dirty marking
for fresh snapshots and tail growth, plus isolated snapshot starts with
non-coherent atoms from 512 through 8,192 bytes. Payload and immutable-reader
checks continue to cover cache reuse, slot rotation and settled arena recycling.

The vertex-flush fixture also executes production mirror updates. It verifies
merged disjoint/overlapping write envelopes, clean/coherent flush skips, exact
initial full-upload bounds and copied payload bytes. A download-triggered flush
and an occupied-page finish/settle must consume only earlier dirty writes; the
following copy must remain tracked for its own submission. Zero-size writes
create no dirty range. Queue submission now supplies the host-write visibility operation; non-coherent
cache flushes still occur before that submission.

Direct main submissions: the recording-epoch fixture covers all eight staging
combinations, both cost-model modes and every finish reason over 192,000 cases.
Each queue call contains one submit info: main alone with no staging, or sync
then main in one command-buffer list. No internal semaphore operations remain;
slot fences, rotation and retired-resource lifetimes retain their existing
checks. The production queue wrapper is included: frontend semaphore handoff
and balanced queue locking must survive both command-buffer-list shapes. Host flushing must occur before the queue call, and empty sync command
buffers must not be begun or ended. The vertex-flush fixture now rejects
recorded HOST barriers while retaining its coherent/non-coherent range tests.

`test-vk-staging-visibility.py` executes production buffer creation/recreation
and staging copies against a byte-visibility model. Across all eight coherency
combinations it checks flush-before-copy, exact payloads, empty skips, refreshed
memory properties after recreation and TRANSFER-to-index/vertex/uniform
barriers; uniform reads must include both vertex and fragment shader stages.
Queue submission provides the host-write visibility guarantee described in the
[Vulkan specification](https://docs.vulkan.org/spec/latest/chapters/synchronization.html#synchronization-submission-host-writes),
while [pipeline barriers](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdPipelineBarrier.html)
cover later command buffers on the same queue. These guarantees do not remove
non-coherent cache flushing or transfer-to-consumer dependencies.

Auxiliary staging uploads now use the same pre-submission host-write guarantee.
The visibility fixture covers coherent/empty staging-flush skips, exact
non-coherent ranges, a missing imported buffer and the imported-RAM-only upload
barrier. Image transitions and GPU compute/transfer dependencies are retained.
The ring ownership model additionally owns whole non-coherent flush atoms,
including 64-512-byte atoms during randomized submissions, 128-byte adjacent
uploads and a growth that changes memory type. Staging alignment is selected
after growth so the replacement allocation's properties control it.

`test-vk-crosshair.py` uses the bundled Volk loader to execute the production
crosshair attachment clears on a Vulkan GPU. It compares 384 full images with
an independent pixel reference using the former transfer path's byte colors.
Cases cover hidden players, overlaps, clipping at every edge, tiny and tall
render areas, and a restricted dynamic scissor. Crosshairs remain inside the
display render pass; rasterization order preserves outline and player ordering
without staging buffers, buffer-to-image copies or transfer-layout transitions.
The test reports whether the validation layer is available.

`test-vk-display-retirement.py` executes production display retirement and slot
reuse against 20,000 delayed same-queue workloads. It checks newest-first probes,
all completion frontiers, shuffled slot order, reuse and mandatory fence resets.
Eight completed submissions require one status query; older slots already
covered by that completion skip waits; the shutdown drain still waits for unfinished slots. Drawing now uses the separate nonblocking slot-acquisition path.
[Vulkan fence ordering](https://docs.vulkan.org/spec/latest/chapters/synchronization.html#synchronization-fences-signaling)
covers earlier commands and fence signal operations on the same queue.

`test-thread-local.py CHECKOUT [CC]` checks production ordinary TLS accessors with 16 libretro-common threads, independent zero-initialized values and stable per-thread reader addresses. The Windows key-publication race remains covered by `test-win32-tls.c`.


`test-tls-publication.py CHECKOUT [CC]` tests the production key publisher with real libretro-common TLS and thread providers. A deliberately paused first allocator cannot prevent another thread from publishing and exiting; subsequent readers retain the canonical key and isolated values. It checks losing-key cleanup, retained reader storage and 33 permanent keys across 16 threads. Environment variables `TLS_PUBLICATION_CFLAGS`, `TLS_PUBLICATION_BINARY` and `TLS_PUBLICATION_SKIP_RUN` support sanitizer and native cross-build checks.

`test-win-poll.py CHECKOUT [CC]` compiles production Windows polling and the high-resolution timer path. On POSIX, a Windows wait-API model compares 20,000 cases with the previous production backend, checking canaries, readiness, auto-reset events, APCs/errors, the 64-handle limit, timer-only wakes, unavailable/failed timers and saturated timeouts. Descriptor-copy instrumentation must remain zero. On Windows it uses actual event/timer handles, exact-size arrays and all 64 descriptor slots. `WIN_POLL_CFLAGS`, `WIN_POLL_BINARY` and `WIN_POLL_SKIP_RUN` support sanitizers and cross-builds.

`test-timer-deadline-readers.py CHECKOUT [CC]` compiles the production queues with real libretro-common thread primitives (and mutexes when testing older revisions). Six readers query both cached deadline views and expired state while 100,000 writer cycles modify, anticipate, cancel and free timers and toggle clock enablement. Cached reads assert they never acquire a mutex; a paused-owner test supplies an invalid head pointer to prove they neither wait nor dereference nodes. `TIMER_READ_CFLAGS`, `TIMER_READ_BINARY` and `TIMER_READ_SKIP_RUN` support sanitizers/native cross-builds. `TIMER_READ_BASELINE=REV TIMER_READ_ARGS=bench` compares the same out-of-line APIs on a 32-timer list (16 leading external timers); `TIMER_READ_ARGS=bench` tests current code. Generic attribute-mask queries and queue mutations now belong to the frontend thread; older comparison revisions retain their queue mutex.

Timer benchmark comparisons must exclude the reader-role TLS assertion from **both** builds. The runner now selects `TIMER_READ_BENCHMARK` automatically for `TIMER_READ_ARGS=bench` or `matrix`; cross-built binaries need `TIMER_READ_BENCHMARK=1` when compiling. Benchmark execution refuses a build with assertion instrumentation enabled. Normal oracle/concurrency tests keep their assertions. Earlier modification-time comparisons with the assertion enabled only on the current build were biased and should not be used. `TIMER_READ_ARGS=matrix` measures 1/8/32/128-timer queues with stable head/middle/tail rearms, crossing moves, equal-deadline peers and delete/rearm pairs.

`test-nv2a-guest-wakes.py` covers the DOAX gameplay-cadence regression with
production PGRAPH/PCRTC/PFIFO/NV_USER MMIO handlers, `pfifo_kick_guest`, `pfifo_kick` and
`nv2a_libretro_guest_busy`, and the production idle-time decision. Across 10,000 schedules it verifies that flip
release, interrupt acknowledgement, FIFO enable and scanout changes publish
guest work before notifying the worker. PFIFO register writes and channel DMA
PUT/GET/REF updates are covered too, including their register values. A consumed/coalesced wake does not
retire unfinished work; acknowledging an older kick cannot retire a newer one.
Resumed GPU work must prevent idle clock skips; after completion, core-only
redraw/reset kicks remain uncounted. No-op/IRQ-enable writes do
not invent work. The dedicated GitHub Actions job needs no game assets or GPU;
the same test also runs in the regular suite.

`test-depth-host-pack.py CHECKOUT [CC]` compiles production descriptor writes,
pack dispatch, readback routing and host barriers against a buffer/queue model.
Six thousand schedules check exact packed output, direct host writes with no
GPU buffer copy, oversized transfer fallback, unchanged direct-RAM routing,
capped descriptor ranges and compute-versus-transfer host visibility. It also
requires the synchronous readback wait to remain before CPU invalidation.
The native CI job runs this and the buffer-growth ownership oracle.
Optional `--gpu` builds the real Vulkan production descriptor/pack helpers
and compares direct host output with the old device-buffer/copy route across
D24S8/D32S8, native/2x scale and three sizes. It requires Vulkan headers/loader
and queue timestamps; it runs without validation layers and prints scoped GPU
timings from alternating batches, not whole-core latency.

`test-linear-texture-staging.py CHECKOUT [CC]` runs production linear staging
and copy-region construction without a GPU. Thirty thousand width/height/pitch,
alignment and direct-buffer cases verify sampled texels, zero-copy eligibility,
exact staging bytes and no padding fetches. The sparse-pitch case reserves and
copies 1 KiB instead of 256 KiB. It also runs in the native CI regression job.
The Vulkan `test-vulkan-linear-span.py` oracle checks the corresponding real
GPU copy regions when Vulkan and validation layers are available.

`test-pvideo-image-pool.py CHECKOUT [CC]` compiles production overlay acquisition
and the shared Vulkan image pool. Twenty thousand alternating modes must use
two image allocations and two views. Generic texture/surface acquisitions
discard retained overlay views. Exact image keys, heap pressure, per-image and total
byte limits, fixed pool capacity, eviction and final cleanup are checked.
It runs without game assets or a GPU in CI, alongside the retirement test.

`test-pvideo-retirement.py CHECKOUT [CC]` runs production auxiliary submission,
fence retirement, deferred destruction, overlay ownership transfer and
conversion descriptor updates against independently delayed upload/display
completion. It checks 20,000 resize/upload/display schedules, unconsumed
uploads, slot-safe descriptor updates, exactly-once destruction, and zero
waits during retirement. Initialization routing also keeps fixed pipeline,
descriptor allocation and sampler creation out of the upload/resize path.
It runs without a GPU or game assets in CI.

`test-pvideo-dirty.py CHECKOUT [CC]` compiles the production RAM dirty-client
handlers and Vulkan PVIDEO upload path against independent consumer bitmaps
and a driver model. It checks 24,096 mask/range/reset cases and 20,000 source
write/metadata/reset/in-flight-write schedules. Cache hits must skip staging,
flush, conversion, image-copy barriers and auxiliary submission. Fifty
crop-only redraws must perform one upload. Explicit GPU writeback/blit marks,
state-load invalidation, six-client propagation, early-return queries and
range bounds are covered. No image comparison buffer is needed in the core.

`test-yuv422.py CHECKOUT [CC] [--bench]` compares the production pair converter
with the prior scalar helpers for all 24-bit Y/U/V combinations in both packed
orders, complementary second luma, unaligned rows and odd-width guard pages.
The default oracle run uses UB traps. The optional alternating benchmark
removes instrumentation from both implementations and reports conversion-only
timing; it does not measure whole-core latency. Both tests run without game
assets or a GPU in the regular suite and GPU-progress CI job.

`test-gpu-wait-deadline.py CHECKOUT [CC]` compiles the production main-loop
waits, descriptor-poll path and guest idle-time accounting against a scheduler
model. Its 342 cases cover bounded/unbounded/nonblocking waits, zero and
sub-microsecond bounds, overflow-safe rounding, notification during wait
preparation, due bottom halves, socket/host timer deadline clamping, BQL
release/reacquisition and balanced eventcount wait bookkeeping. Deterministic
waits and waits without GPU work do not advance guest time; non-deterministic
GPU waits advance only by actual elapsed host time, capped at the next guest
deadline. This
runs in the regular suite and the DOAX GPU-progress CI job without game assets.

`test-icount-publication.py CHECKOUT [CC]` compiles production instruction accounting, clock publication, skip and restore callbacks. It compares owner arithmetic/I/O boundaries and fractional conversion with the prior implementation; six observers must see complete snapshots across 100,000 count/bias updates. A paused writer stops between a raw-field update and publication, while observers must return the previous committed timestamp. Successful loads and resume after partial state changes must rebuild publication. `ICOUNT_TEST_CFLAGS`, `ICOUNT_TEST_BINARY` and `ICOUNT_TEST_SKIP_RUN` support sanitizers/native cross-builds; `ICOUNT_TEST_BASELINE=REV` selects prior production code; `ICOUNT_TEST_ARGS=bench` compares out-of-line observer/idle-owner/update paths with identical common TLS access and no role-check assertion in either getter. Writer serialization is modeled, so microbenchmarks do not measure full clock-lock or game costs.


`test-owner-elapsed-clock.py CHECKOUT [CC]` compares the production elapsed-clock implementation with an independent 50,000-step lifecycle model: run, pause, resume, forward-only skip, saved/restored offsets and icount forwarding. The elapsed clock is an emulation-thread API; observing threads use the published instruction clock. `OWNER_CLOCK_BASELINE=REV` selects the previous implementation; `OWNER_CLOCK_CFLAGS`, `OWNER_CLOCK_BINARY` and `OWNER_CLOCK_SKIP_RUN` support sanitizers/native cross-builds, and `OWNER_CLOCK_ARGS=bench` times out-of-line paused/running reads with a controlled host clock. Real-core hardware-watch ownership checks are recorded with delivery verification.

Timer dispatch and clock enablement belong to the emulation thread. Timer queues no longer allocate completion events or reset/set them around callback dispatch; timer mutations belong to the same owner. The queue oracle checks that clock initialization creates zero completion events.

Timer queues have one frontend/emulation-thread owner, enforced using the libretro-common thread ID. Mutation, pending/expiry queries and arbitrary attribute-mask traversal are owner-only. Cross-thread deadline observers retain the two atomic scalar views, without node access or reclamation waits. The queue oracle expects zero mutexes and completion events; old revision comparisons detect the former queue locks.

`test-aio-handler-lifetime.py CHECKOUT [CC]` compiles production Windows/POSIX handler removal, dispatch and deferred-free paths with the production frontend ownership/nesting helpers. In 10,000 schedules per backend, callbacks delete themselves and the next handler, recurse to a second dispatcher and delete another handler there. No node may be freed until the outer walk resumes; deleted callbacks must not run. Idle removal and zero-depth reclamation are checked too. `AIO_HANDLER_CFLAGS`, `AIO_HANDLER_BINARY_PREFIX` and `AIO_HANDLER_SKIP_RUN` support sanitizers/native builds. The AIO exclusion gate implementation/API has been removed; background BH scheduling and notifier publication remain atomic.

The POSIX core now uses direct GSource descriptor registration/check/dispatch. The unused synchronous fdmon wait/TLS poll-array copies, epoll upgrade backend and userspace busy-poll state are removed. Descriptor readiness, nested callback lifetime and native Windows socket behavior remain covered by the AIO tests.

`test-posix-aio-source.py CHECKOUT [CC]` compiles production POSIX registration/readiness/dispatch with the core’s compatibility GSource and nonblocking pipes. Under ASan/UBSan it checks 100 nested callback replacements/deletions, no-data readiness, writable and HUP events, and 96 ready descriptors. `POSIX_AIO_CFLAGS`, `POSIX_AIO_BINARY` and `POSIX_AIO_SKIP_RUN` support other builds; a Windows compiler is skipped because this integration needs a POSIX host.

`test-qcow2-workspace.py CHECKOUT [CC]` uses production QCOW2 code and disposable compressed images to count full-cluster copies during partial write/zero and inject data-write failures. It verifies original data after failure, changed ranges and untouched neighbors after success, cache invalidation, and external qemu-img comparison/check. `QCOW_WORKSPACE_BASELINE=REV` compares old production copy counts; `QCOW_WORKSPACE_CLUSTER_SIZE`, `QCOW_WORKSPACE_CFLAGS`, `QCOW_WORKSPACE_BINARY`, `QCOW_WORKSPACE_FIXTURE_DIR` and `QCOW_WORKSPACE_SKIP_RUN` control cluster size, sanitizers and native builds. qemu-img is required. Injected allocation-stage failures may leave pre-existing permitted orphan clusters; the failure fixture checks data equivalence, while successful images receive qemu-img structural checks.

`test-dma-retry.py CHECKOUT [CC]` compiles production DMA retry registration/cancellation, release publication and teardown against a coalescing atomic BH scheduler model. Real libretro-common threads force four first-registration/release windows, cancel while a releasing thread is paused before enqueue, retire queued callbacks before freeing their opaque AddressSpace, and race 20,000 registrations/releases. Notification BH allocation must remain lazy; callbacks/list operations stay on the owner. `DMA_RETRY_CFLAGS`, `DMA_RETRY_BINARY` and `DMA_RETRY_SKIP_RUN` support sanitizers/native builds. Actual BH queue behavior remains covered by full-core integration.

`test-win-path-root.py CHECKOUT [CC]` checks production Windows root parsing on portable cases under sanitizers. Native Windows also compares 20,044 ASCII/UTF-8, drive, UNC and extended-prefix cases with the former PathCchSkipRoot implementation, resolved dynamically by the test when available. The core itself no longer imports PathCch or allocates/converts a wide path for resource relocation. `WIN_ROOT_CFLAGS`, `WIN_ROOT_BINARY` and `WIN_ROOT_SKIP_RUN` support cross-builds.

`test-gl-dispatch-win32.py` builds a real WGL fixture (OpenGL 4 required).
Run its frontend and native modes as separate processes. It verifies common
resolution, native-only setup with no frontend GL context, shared buffer
visibility, context restore, exact extension matching and no table rewrite
after worker publication. `GL_DISPATCH_BINARY` and `GL_DISPATCH_SKIP_RUN`
allow cross-compiling and executing the fixture on Windows.

`test-packed-overlay.py` checks production packed-row copying and GPU YUY2
conversion against the previous CPU RGBA converter (3.2 million pixels,
byte-exact). `test-packed-overlay-vulkan.py` additionally executes the actual
converter, internal GLSL compiler, descriptor handling and barriers on Vulkan
with synchronization validation, including drained source-buffer replacement
and fallback preflight. Compute writes the existing RGBA image directly,
removing the conversion-buffer-to-image copy; native filtering is unchanged.

`test-gl-readback-fused.py` compiles the actual synchronous/pending GL
readback functions and swizzler against mocked GL transfers and an independent
Morton/sample oracle. It forces GPU-conversion fallbacks, checks flip/stride/
format/scale and stale-PBO cleanup, and verifies that scaled swizzled paths
allocate no native-size scratch image. Sanitizers run by default; native cross
execution uses FUSED_READBACK_BINARY, FUSED_READBACK_SKIP_RUN and
FUSED_READBACK_CFLAGS. These are CPU fallback/interface tests, not GL-driver tests.

`test-gl-pitched-surface.py` executes the actual upload transfer segment,
pixel-store layout selection, CPU expansion and GPU-grow code on EGL/GL or
native Windows WGL via libretro-common. It checks pitched/irregular byte rows,
colour/integer/depth formats, fresh/reused grow textures, scale 1/2/3, swizzled
input, source guards and unpack-state restoration. GPU depth blits compare
against the former compact native-upload/blit reference (which may quantize
a depth bit). PITCHED_GL_BINARY / PITCHED_GL_SKIP_RUN support cross builds.

`test-gl-readpixels-flip.py` compiles the actual flip/pack-state wrapper with
mock GL writes against an independent pixel oracle. Both detected and forced
portable routes cover 14,400 cases each (padding, unaligned/tail rows, odd/zero
dimensions, state restoration and unflipped PBO handoff). CPU flags are a fixture
adapter; the core uses libretro-common detection. GL_FLIP_BENCH=1 with
GL_FLIP_CFLAGS="" runs an isolated swap benchmark; it is not a readback/FPS
benchmark. GL_FLIP_BINARY / GL_FLIP_SKIP_RUN support cross builds.

`test-pgraph-fence.py CHECKOUT [CC]` runs the production fence publisher and
MMIO read path with libretro-common threads and atomics. It checks 524,288
publications against immutable payload slots, full-width register values and
dirty-bit behavior. With an AArch64 cross compiler installed it also checks
that the actual portable atomics emit LDAR/STLR without full barriers or
exclusive retry loops; on x86-64 it rejects locked instructions, fences and
helper calls in the publisher. This is host-memory publication coverage, not
a GPU completion or title-performance test.

`test-gl-scale-scratch.py` compiles the production scratch implementation and
checks discard-on-growth allocation, retained capacity across 10000 alternating
requests, empty requests, finalization and reuse after finalization. GL pitched
upload and fallback readback tests exercise the renderer-owned scale scratch.
SCALE_SCRATCH_BINARY / SCALE_SCRATCH_SKIP_RUN support native cross builds.

`test-palette-unswizzle.py` compiles the real Morton palette helper and GL/Vulkan
CPU fallback callers. An independent 3D address oracle covers rectangular
power-of-two levels, volume slices, palette/source/output misalignment, guards,
source immutability, one final Vulkan range and no GL index scratch. The GPU
unswizzle success branch is preserved. PALETTE_CFLAGS, PALETTE_BINARY and
PALETTE_SKIP_RUN support optimized or native cross builds; --bench after the
compiler compares the old unswizzle+convert functions with the fused helper.

`test-ohci-pending.py CHECKOUT [CC]` extracts production packet ownership,
TD/ED service, completion and cancellation code with controlled USB and DMA
callbacks and the production owned-iovec lifecycle. It covers 2,865 scenarios: 40 concurrent endpoints, shared control
endpoint ownership, reverse completions, growth and allocation-free reuse,
immediate callback delivery, paused endpoints, device address changes, hub
subtree removal, rewind, zero/partial transfers, backend errors, DMA faults and bounded TD cycles.
The ASan/UBSan run checks that callbacks retain stable packet addresses through
key-table growth/compaction and that freed packets are not used after faults.
The old single-packet VMState bytes remain as ignored padding, without a
runtime buffer; outstanding host requests are canceled before state load and
guest TDs are reissued. Isochronous transfers retain their existing path.
This fixture does not model physical USB passthrough or external device rewind.
The core USB ROM also checks button release after repeated state loads. Win64
validation additionally loaded an original-controller state with zero RAM
differences, and passed reset and Vulkan reload under strict audio/video
cadence. OpenGL reload retains its existing restart restriction.

`test-rgb655-unswizzle.py` checks all 65536 RGB655 words against independent
channel equations, rectangular Morton levels and unaligned source/output.
Actual GL/Vulkan callers must skip raw scratch and Vulkan must stage final RGBA
with alpha 127. RGB655_CFLAGS/BINARY/SKIP_RUN support native cross builds.
`--bench` after the compiler compares the frozen c07b7f1065 converter plus
unswizzle/alpha expansion with the fused production helper; buffers are
preallocated and measurements cover CPU conversion only, not game FPS.

`flash.asm -DSTREAM [-DPCM=0]` plays 255 SSL segments from the same physical
source address. `run.sh` compares 60 frames at address zero against the normal
address through `check-stream-address.py`: exactly 48,048 stereo samples,
bit-identical PCM, a non-silent 1 kHz tone and strict frame/audio cadence.
This tests valid zero-address segments; it does not validate incomplete or
malformed SSL descriptors or claim to resolve every crash reported in #3072.

`python libretro/tests/test-vsh-state-cache.py . gcc` compares cached state-program
execution with the existing interpreter for 52001 programs and 1000 production
method launches. It checks exact constants/dirty flags, overlapping starts,
partial uploads, restored source words, early/missing end markers, invalid
programs and zero launch allocations. An unchanged instruction is decoded once;
changing input constants or v0 still executes the program every time.

`test-gl-texture-scratch.py` compiles the actual GL texture scratch helper
with a poisoning allocator. It checks 14000 alternating requests, zero-size
requests, geometric capacity growth, no allocation on reuse, discard-before-
allocate and preservation of the other live scratch slot. TEX_SCRATCH_BINARY,
TEX_SCRATCH_CFLAGS and TEX_SCRATCH_SKIP_RUN support native cross builds.

The S3TC decoder and its normalized oracle now compile as standard C without
QEMU, GLib or libretro-common. `make s3tctest` links only the decoder, oracle
and math library. GL upload tests compile the production decoder unchanged,
without allocation/include rewriting. Texture conversion APIs take caller-owned
output; the unused allocating wrappers have been removed.

`test-packed-swizzle.py CHECKOUT [CC]` checks 6,000 production routing,
descriptor, barrier and Morton-addressing cases, including capped-range fallback
and conversion-buffer reuse. Native CI runs it and the buffer-growth ownership
oracle. Optional `--gpu` runs the production depth-pack and Morton shaders on
Vulkan, comparing the former ring-copy route against the direct packed source
for D24S8/D32S8, native/2x scaling and three rectangular/square sizes. It checks
identical guest bytes, two successive readbacks per command buffer and one
removed copy per readback; timings describe this isolated GPU
chain, not whole-core latency. The GPU fixture does not enable validation layers.

`test-volume-block-copies.py` checks production compressed-volume CPU staging
against a coordinate oracle, including 8/16-byte specialization and the generic
block-size fallback, odd XY extents, partial Z groups, direct VRAM, guards and
metadata. VOLUME_COPY_CFLAGS/BINARY/SKIP_RUN support native cross builds.
`--bench` after the compiler compares a frozen fcea99f7cf control with current
code, with both copy bodies inlined into equivalent runtime-BC1/BC3 wrappers
and preallocated output. CPU helper timings only.

The depth-host oracle also covers 514 eligible direct packed-swizzle routes
that must omit the stale compute-to-transfer dependency. Linear, capped-range
and host-readback routes retain their transfer dependencies. The optional
packed-swizzle GPU fixture counts these dependencies across two successive
readbacks, in addition to checking copies and final bytes.

### Same-page translated code stores

`python3 libretro/tests/test-tb-store-overlap.py .` runs 184,320 cases against
production physical-page invalidation with controlled page lists. It checks
nearby data stores, actual code modifications, single-instruction restarts,
discontiguous second-page mappings, external/DMA writes and disabled precise
SMC. The byte-by-byte physical-address oracle is independent of the production
range calculation. REP MOVS/STOS guest tests additionally cover self-modifying
code with CPU Boost enabled and disabled. No lock implementation is substituted
or added to the emulator by this fixture.

### OpenGL shader-cache identity strings

`python3 libretro/tests/test-gl-cache-strings.py .` checks the production mapped
string reader against truncated lengths/payloads, every corrupted byte including
the terminator, and all four combinations of missing driver vendor/version.
Normal identities and existing cache encodings are preserved; fallback strings
are selected only at initialization, with no repeated driver query.

`test-vk-memory.py ROOT CC [--gpu]` exercises the production reserved
Vulkan heap with a deterministic driver oracle: fragmented lifetimes, binding
alignment and overlap, linear/image isolation, persistent mappings,
noncoherent atom isolation, dedicated requirements and failure rollback. Its
reservation cases assert no driver allocations, mappings or frees during ordinary
reuse and buffer exhaustion. Image-capacity/fragmentation/type/dedicated pressure
reclaims first, then uses overflow; empty overflow blocks are released while
fixed blocks stay live. Real driver-OOM cleanup is checked. `--gpu` forces image
reservation overflow, then checks actual image clears, direct mapped readbacks,
offset copies and guard-byte checks, enabling Khronos validation when available.

`test-fused-depth.py CHECKOUT [CC]` executes the immutable fused depth SPIR-V
through a bounded SSA interpreter and compares 48,000 cases with an independent
Morton/depth/stencil oracle. It covers both formats, 128/256 workgroups, scales
1/2/3, rectangular and degenerate dimensions, offsets and padded dispatch tails.
A second oracle compiles production initialization, eligibility, descriptor
refresh, dispatch and teardown: 6,000 dispatches create no additional objects;
format, plane-size, descriptor-range, alignment and guest bounds retain fallbacks.
`spirv-val` validates all four fused modules when installed. CI runs both native
oracles without copyrighted firmware, game files or a GPU.

Optional `--gpu` compares the actual production two-pass and fused paths with
identical bytes, zero intermediate copies and dispatch counts 4 versus 2 for two
successive readbacks per submission. It covers 28 format/scale/layout cases and
11,200 chains. `--group128` selects the 128-thread device fallback. These are GPU
microbenchmarks, not whole-core latency measurements; the fixture enables no
validation layer. Original pack/unpack shader encodings and all CPU-readback,
linear, incompatible-layout and range-limited fallback routes remain intact.

`test-vk-memory.py` also replays all 718 allocation/free events from a 6000-frame
Ninja Gaiden Black cold boot at 8x. The fixture contains only host allocation
sizes/alignment/lifetimes, with units scaled 1:256, and no guest assets. It
verifies the old 384 MiB reservation completes through overflow, while the
corrected startup reservation handles the complete sequence
without any new driver allocations or mappings. The shared reservation helper
is also checked at 1x/2x/4x/8x and for overflow.


`test-memory-poll.py CHECKOUT [CC]` covers the NGB slot-0 throughput regression
using the production x86 proof and RAM-borrow helper. 52,580 arithmetic/opcode/
mode/mapping cases check the unsigned countdown, every instruction byte,
interruptibility, ordinary RAM only, segment bases/limits, page/region bounds, counter
release and code modifications. No title address is embedded in the core.
`test-idle-interrupts.py` covers both kernel and RAM proofs after pending IRQs,
stops and chain invalidations. CI also retains `test-quiet-progress.py`: useful
store-free computation must continue instead of acquiring an artificial wait.
Hardware-independent proof/work tests prevent the regression without asserting
an unreliable FPS threshold on shared CI hosts. Real slot-0 8x timing comparisons
and the game suite remain separate from these native oracles.

The DSP tick now runs on the engine owner because its live-RAM DMA must settle
before the guest resumes. `test-dsp-guest-ram.py` compiles that actual dispatcher
and tick, preventing a deferred-worker regression while retaining scratch growth
and page-reuse coverage. GP/EP register effects occur in that same order; the
frontend queue's SPSC ownership, sample cadence and existing lead are unchanged.

`test-quiet-store-prefix.py` exercises the production translation-time rewrite
that removes an overwritten quiet-counter preamble. Straight-line prefixes may
remove it; branches, labels, helper calls and guest accesses retain it. The
reset itself remains, as do frame instruction budgets and interrupt checks.

`test-secondary-cache.py` runs the production block lookup against primary
collisions, empty/invalidated primary slots and secondary identity mismatches.
A valid secondary entry avoids the global lookup and is promoted; invalid PC,
mode or cflags entries must miss. The production cache-store and flush paths
also cover creating class variants, PC-relative virtual identities, eviction,
invalidated entries and clearing both arrays. CPU boost disabled retains the
primary path.

The native DSP test also warms one PC in all eight scaling/LF identities,
toggles LF without flushing and compares architectural boundaries with the
interpreter. A changed P word and an explicit flush must invalidate all loop
variants. REP code remains shared per scaling mode. Both loop indexes are
prepared together: no extra metadata allocation when LF changes. Worst-case
metadata grows by 160 KiB per DSP; the executable arena retains its 4 MiB cap.

The native DSP suite covers register BCLR/BSET/BCHG/BTST masks and carry,
upper status bookkeeping, accumulator limiting and loop-end post-processing.
It also compares reserved-slot parallel moves and ordinary self-move width
masking, retaining pre-ALU copies for accumulator parts. A warmed JSR/RTS loop
must enter generated code once per run; stack errors and interrupt pipelines
continue through the existing post-processing path.

Native DSP coverage also checks status-register writes across all eight scaling/DO identities, warmed status-restore chains and discarded reserved-slot moves. Program-memory effective-address reads cover all accepted address modes and ordinary register destinations; code writes and repeated status writes retain their conservative paths.

`make dspstatus` compares 100 frames of nested sample/pass loops with production-size blocks for two memory seeds. Deferred ADD flags feed an SR read, ordinary register BSET and SR restoration before conditional moves; program-memory data is read directly during the same loop.

A conservative 512-byte translated-word coverage map skips DSP invalidation scans for untouched data words. Coverage persists until a full cache flush, so stale bits only cause extra scans. Extension words and both ordinary/REP translations are covered; a native regression checks data writes skip the scan and changed instruction extensions invalidate generated code.

`test-early-memory-wait.py` exercises the production nomination and pre-instruction budget-refund helper across 16-bit instruction-count boundaries, both request-half states and rejected live proofs. `test-idle-candidate-prefix.py` verifies every existing idle-proof entry remains eligible, with exhaustive opcode prefixes and a real protected next host page. Runtime proofs still validate guest code, mappings, interrupts and wait conditions afresh.

`make dspbudget` compares the native run-until-halt path against both the interpreter and the old chunked JIT for six production-size DSP programs, two memory seeds and 100 frames each (2,400 frame comparisons). The chunked-JIT comparison also requires an identical leftover cycle budget at every halt. The programs exercise REP, nested loops, conditional calls, flags and status changes, program reads, DMA and hardware halts.

`test-tcg-budget-register.py` executes the production x86 emitter decisions for 150,368 cases: only the exact environment-relative 16-bit budget may use R15; other fields and base registers keep normal memory operations. It checks full 16-bit truncation, helper publication/reload order, helper-return preservation and independent request-half updates. It also requires synchronization around every ordinary and software-MMU C call site. The emitter feature is gated by the x86-64 target header; other host backends retain their memory budget.

Native test harness maintenance also includes the out-of-line voice-signature helper in both voice fixtures, links texture swizzle dependencies into the shader-seed validator, and converts embedded VFS paths explicitly in an MSYS2 native run.

The frame suite distinguishes an arbitrary RAM spin (`SPIN`, no idle proof) from the exact supported timer-counter loop (`PROVEN_WAIT`). Both retain strict audio cadence; only the proven wait is required to exceed 4x real time. The ROM pins the exact supported instruction encodings and masks PIC lines before enabling interrupts.

Standalone shader-generation and POSIX AIO fixtures use `glib_fixture.py` to link the core’s compatibility implementation. They do not query `pkg-config glib-2.0` or link upstream GLib. The Windows/Wine CI job installs native graphics-test headers because these source-extraction fixtures run on the Linux host.

Texture conversion partition guard:

```sh
python3 libretro/tests/test-texture-conversion-partition.py .
```

Checks that every converted swizzled format has a fused upload path and the remaining conversions are linear YUV. Pair with `test-palette-unswizzle.py` and `test-rgb655-unswizzle.py` for pixel and caller-storage coverage.
`test-seed-merge-view.py CHECKOUT [CC]` exercises the production seed parser, borrowed section lookup and merge. Mapping storage is poisoned and released before the write callback. It checks deduplication, caps, missing/malformed/mismatched files, write failures, no intermediate section duplication and the independently owned public read API. `SEED_VIEW_CFLAGS` enables sanitizers.


The S3TC oracle also covers 1,944 clipped image/volume shapes, partial four-slice groups, unaligned output and full-output guards, alongside exhaustive normalized color/alpha checks. CPU BC2/BC3 decode consumes packed alpha directly while writing final RGBA pixels.

GPU completion races are covered in `test-nv2a-guest-wakes.py` and `test-gpu-wait-deadline.py`: completion after an idle sample invalidates that sample, and completion between the parked slice exit and the frame-owner recheck must resume a runnable guest before skipping time. A halted guest still advances to its timer when no device work remains. Both fixtures run in the DOAX GPU-progress CI job.

The display ROM's exact four-GPU-round-trip check runs in deterministic mode and uses the supported RAM-counter wait encoding. The flash masks PIC lines before entering the ROM; the wait saves/restores interrupt flags. An arbitrary equality spin remains unproven and consumes instruction time, so it cannot provide a host-speed-independent GPU-progress oracle.

`test-pipeline-cache-export.py CHECKOUT [CC]` runs production Vulkan pipeline-cache export and teardown against a driver oracle: scratch reuse/geometric growth, growth between size query and export, incomplete data, failed-write retries, query errors and release after the synchronous final save. `PCACHE_EXPORT_CFLAGS` supports sanitizers.


The GL volume compression oracle forces both CPU and GL 4.3 compute decoding, retains native VTC coverage, and checks 9,437,184 GPU pixels against normalized BC color/alpha rules. It checks partial four-slice groups, odd/clipped dimensions, mip uploads, program/SSBO-range/image state restoration, caller selection and storage-limit rejection. `GL_VOLUME_BAD_SHADER=1` tests compile-failure fallback; `GL_VOLUME_BENCH=1` compares warmed full CPU/upload, compute/upload and native VTC paths over eight rotating rounds with preallocated guest input. `GL_VOLUME_BINARY` retains the executable; `GL_VOLUME_SKIP_RUN=1` permits running Windows tests through a noninteractive debugger wrapper.
Shader-layout ownership tests require one module allocation, 16-byte parameter alignment, shared immutable metadata, non-overlapping parameter regions and independent mutable storage across modules. `SHADER_LAYOUT_CFLAGS` enables additional compiler/sanitizer flags.

`test-pipeline-job-view.py CHECKOUT [CC]` checks production job linking, slot clearing and release publication across 36,992 count/lifetime/slot-reuse cases, plus full-ring rejection and both pipeline-library halves. Synchronous jobs borrow caller vertex descriptions; queued jobs rebind to owned keys before publication. `PIPELINE_JOB_CFLAGS` enables sanitizers.

### CPU blit row coalescing

`python3 libretro/tests/test-blit-copy.py .` compares the shared production copy
helper with forward, row-by-row `memmove` across contiguous and pitched copies,
both overlap directions, identical buffers, zero pitches and empty dimensions.
It checks the entire backing store and counts copy calls: disjoint packed
multi-row copies issue one call; dependent overlapping rows retain their order.
This does not exercise GPU transfers or claim class 0x39 support.


`test-vulkan-s3tc-decode.py CHECKOUT [CC]` runs production planning, shared integer shader, descriptor/view ownership, source selection and barriers through the internal compiler and real Vulkan. It covers 1,035 image cases / 35,902,305 exact pixels, exhaustive endpoint/selector palettes, odd mips, cube borders, skipped faces, compressed snapshot poisoning, direct/unaligned buffers, eight queued submissions GPU-produced compressed input, and preflight rejection. Synchronization validation is on by default. `VK_S3TC_NO_VALIDATION=1` is an explicit escape for native environments without the validation layer; `VK_S3TC_BINARY` and `VK_S3TC_SKIP_RUN=1` retain a fixture for noninteractive native runs. `VK_S3TC_BENCH=1` compares production CPU decode/staging with snapshot/direct compute for one- and four-level volumes, including submission/fence completion and GPU timestamps. Timed CPU input uses cached heap RAM, and coherent GPU buffers prefer HOST_CACHED like the core allocator; the direct buffer models imported guest RAM without timing its setup copy.

`test-texture-aux-retirement.py CHECKOUT [CC]` executes production texture release and deferred retirement through 48 independent main/display/aux completion schedules, borrowed-image ownership and wrap-near main counters. `TEXTURE_AUX_CFLAGS` controls sanitizers.
`test-pipeline-ahead-key.py CHECKOUT [CC]` runs production speculative-job construction against an independent final-key/hash oracle in 4,096 cases. It checks direct slot construction, source lifetime, shader replacement, module ordering, cache/in-flight hits and full-ring backpressure. `PIPELINE_AHEAD_CFLAGS` enables sanitizers.

### PFIFO deferred-method progress

`python3 libretro/tests/test-pfifo-no-progress.py .` exercises production pusher
accounting for negative, zero and positive method results. It checks one dispatch
per zero-progress attempt, unchanged GET/method/count registers, successful later
retry, and incrementing/non-incrementing batches from one through 64 words.
This defines the no-progress contract; it does not enable class 0x39 transfers.

`test-gpl-lookup-cache.py CHECKOUT [CC]` compares production GPL lookups to a first-match scan across 122,120 cases: zero/large hashes, cache-slot collisions, duplicate hashes, misses followed by insertion, independently moving pre/fragment arrays, and renderer reinitialization. Warm hits require one library-entry probe and allocate nothing. `GPL_LOOKUP_CFLAGS` enables sanitizers.

Worker-module lifetime tests follow the combined module/parameter allocation path and verify that shared metadata outlives the shader words and either module. `VK_WORKER_MODULE_CFLAGS` enables extra sanitizer flags; assertion failures exit normally without opening a Windows crash dialog.

For release timing, use `VK_S3TC_BENCH=1 VK_S3TC_NO_VALIDATION=1`; benchmark builds default to no sanitizers. Normal Linux correctness builds default to UBSan; `VK_S3TC_CFLAGS` can override the extra compiler flags. Full-size nine-mip cube callers are covered above the production size threshold, including border crops and preserved skipped faces.
`test-wgl-context-versions.py CHECKOUT [CC]` checks production Windows context negotiation: prefer 4.5, then 4.3, 4.1 and 4.0, preserving the share-context and DC arguments on every attempt. Unsupported drivers return failure. The real WGL dispatch test also reports the selected version and checks sharing from a 4.0 frontend context. `WGL_CORE_CFLAGS` enables sanitizers.

`test-gl-module-export.py CHECKOUT [CC]` exercises production OpenGL module export with 100 same-buffer saves, geometric growth, byte-exact headers and copied-length payloads, driver/open/write failures and release/reinitialization. `GL_MODULE_EXPORT_CFLAGS` enables sanitizers. Export storage belongs solely to the flip-time save queue.

The Vulkan S3TC uploader publishes all source/image descriptors in one driver call after creating the retired slot views and before recording dispatches. The existing full-mip cube, queued-slot, pixel and synchronization-validation cases cover this batching path.

`test-dirty-page-runs.py CHECKOUT [CC]` checks production dirty-run collection against an independent page oracle, including clipped callbacks, word/block boundaries, other clients, reprotection ordering and injected late writes. `--real-block-size` uses production bitmap block geometry. `DIRTY_RUN_CFLAGS` controls sanitizer flags; `DIRTY_RUN_BINARY` and `DIRTY_RUN_SKIP_RUN=1` retain native fixtures for noninteractive execution.

`test-gl-vertex-sparse.py CHECKOUT [CC]` exercises production GL vertex uploads with 3,011 byte-exact mirror cases: sparse/dense/fragmented changes, small spans, clipped page bounds, foreign dirty-bit consumers, quick checks, late writes and reset. `--real-gl` enables native WGL uploads and final real-buffer readback. `GL_VERTEX_SPARSE_CFLAGS`, `GL_VERTEX_SPARSE_BINARY` and `GL_VERTEX_SPARSE_SKIP_RUN=1` control sanitizer flags and noninteractive native fixture execution. Bitmap/dirty-provider stubs isolate upload ownership; the separate dirty-run fixture tests the real atomic collector.
`test-vk-image-pressure.py CHECKOUT [CC]` uses the production allocator, retirement drain and allocation-retry callback with a deterministic driver; it creates no window or game session. It checks 48 pressure schedules across draw/display/upload completion orders, active command recordings, pooled/direct destruction and submission-counter wrap, with live image bytes preserved. Pending GPU users must remain untouched while
overflow serves the replacement; completed reclamation needs no backing growth.
Overflow blocks are freed once empty, without additional maps. Six driver-creation error cases cover bounded host/device-memory retries and non-memory failures. This guards false allocation failures when completed images are still awaiting retirement-list collection; it does not prove every game fits the fixed reservation.

`test-inline-vertex-storage.py CHECKOUT [CC]` executes production inline backfill/append: constant attributes allocate nothing, all sixteen first-use arrays preserve float/NaN bits, 300 batches reuse allocations, and the 524287-vertex limit remains valid. `INLINE_STORAGE_CFLAGS`, `INLINE_STORAGE_BINARY` and `INLINE_STORAGE_SKIP_RUN=1` support sanitized and noninteractive native fixtures. Allocation-request savings are not a resident-memory or FPS measurement.

The GL readpixels flip fixture explicitly checks AVX2, SSE2 and portable routes (14,400 cases each). The SSE2 route removes the heap row temporary on supported x86 targets; AVX2 remains preferred. Release benchmark mode alternates old/new order across eight rounds. Forced SSE2 measurements on a modern CPU do not establish performance on all older CPUs or game FPS.

`test-gl-report-storage.py CHECKOUT [CC]` runs production GL report handling, queue primitives and object pool through 1,526 schedules / 15,051 query lifetimes: no separate allocation for inline lists, same-object queue handoff, geometric overflow, scale rounding, FIFO availability, clear, blocking drain, pool exhaustion, teardown and reinitialization. Query GL calls use deterministic stubs. `GL_REPORT_STORAGE_CFLAGS`, `GL_REPORT_STORAGE_BINARY` and `GL_REPORT_STORAGE_SKIP_RUN=1` support sanitized and native runs.
### Nonblocking Vulkan display slot acquisition

`python3 libretro/tests/test-vk-display-slot.py .` extracts production slot
acquisition, the early render admission boundary and the cached-scanout retry
condition. Busy, completed, cached-completed and unused slots are checked for
bounded queries, reset ordering and preservation of published-frame counters.
Repeated busy attempts leave work and slot rotation untouched; a mode-only
change remains eligible for retry. The drawing path no longer calls the fence
wait helper; shutdown draining retains it. This does not remove the separate
libretro frame-end acknowledgement wait or establish frontend image retirement.

`test-vk-depth-source.py CHECKOUT [CC] [--gpu]` checks production source eligibility and depth row packing: 122,880 eligibility cases and 1,188 packed/padded/offset/empty copies, aligned region batches, byte-exact destinations and source/guard preservation. CPU mode defaults to ASan/UBSan; GPU mode can use `DEPTH_SOURCE_CFLAGS=-fsanitize=undefined`. GPU mode requires synchronization validation unless `DEPTH_SOURCE_NO_VALIDATION=1` is explicitly set. GPU fixture buffers model imported memory without testing the external-host import extension itself. `DEPTH_SOURCE_BINARY` and `DEPTH_SOURCE_SKIP_RUN=1` retain native fixtures. The existing depth-kernel fixtures cover unpack shaders, which are unchanged.

`test-seed-batch-read.py CHECKOUT [CC]` exercises production batch/individual seed loaders with real libretro-common inflate: 480 legacy-equivalence cases, independent local-first ordering, poisoned mapping/decompression lifetimes, missing/stale/malformed/corrupt input and inflater failure. Both sections share one mapping/inflation with no duplicated section arrays. It also checks all 14 games / 38 sections in the bundled dictionary byte-for-byte. `SEED_BATCH_CFLAGS`, `SEED_BATCH_BINARY` and `SEED_BATCH_SKIP_RUN=1` support sanitized and native execution; retained native binaries accept the dictionary path as their optional argument.

Vulkan texture uploads consume the binding key captured during cache lookup: shape, texture/palette offsets and cube face span. The S3TC fixture supplies cached offsets and rejects a second DMA resolution; its existing 1,035 GPU image cases validate exact pixels across direct/snapshot sources, mip/cube/border/skip layouts and queued slots. The runner also checks RAM staging is passed the same binding key and does not re-decode source registers.
### Vulkan frontend image ownership and resize retirement

`python3 libretro/tests/test-vk-frame-handoff.py .` extracts production frontend
acquisition and resize retirement with the real libretro-common atomic handoff.
It checks 20,009 deterministic schedules, injected acquisition races and two
libretro-common threads making 300,000 attempts each. Snapshot metadata must be
consistent; both old and candidate images remain protected through presentation.
Resized images are destroyed only after their display submission serial settles,
and full retirement storage defers without image destruction or counter advance.
Backend fence completion is controlled in this fixture; the separate frame-end
acknowledgement wait is not removed by this ownership change.

`test-ide-write-generation.py CHECKOUT [CC]` executes production PIO completion and the retro_atomic save-state write counter through 6,144 success/error/retry/IRQ/continuation schedules, plus counter wrap and missing-drive behavior. It checks DMA write-only completion placement and removal of block accounting from active paths. `IDE_WRITE_CFLAGS`, `IDE_WRITE_BINARY` and `IDE_WRITE_SKIP_RUN=1` support sanitized and native runs. The HDD/DVD ROM integration tests cover DMA, PIO reads and out-of-range errors after telemetry removal.
fixture covers asynchronous GL/shared-device Vulkan and legacy interop acknowledgement.


Shared-device Vulkan now requests a frame-end scanout refresh without waiting for
FIFO acknowledgement. Presentation consumes the latest claimed publication or
duplicates the previous image. GL also publishes renderer-composed images without
CPU acknowledgement; legacy Vulkan interop still acknowledges frame end synchronously. `test-frame-completion.py` covers all four Vulkan flag
combinations, including an owner that remains busy in the asynchronous path.

`test-null-chardev.py CHECKOUT [CC]` runs the production sink and generic write/log paths through 960 combinations of short writes, retry/error handling, best-effort logging and zero-length requests. Unlogged null sinks take no locks and never read the payload; logged or real backends preserve their existing behavior. NULL source/INT_MAX and negative-length cases are covered. `NULL_CHARDEV_CFLAGS`, `NULL_CHARDEV_BINARY` and `NULL_CHARDEV_SKIP_RUN=1` support sanitized and native fixtures.

OpenGL presentation receives width/height with the acquired resolved texture.
The display slot already includes scanout clipping and internal-resolution
scale; presentation does not query texture-level dimensions from the driver.
The GPU display ROM covers clipping, mode changes and overlays. Repeat it at
internal scale 2 to check geometry and pixels after display-texture recreation.


`test-native-color-volumes.py CHECKOUT PARENT CANDIDATE HARNESS SCRATCH`
checks single-level 2x2x2 RGBA8 volumes with independent depth slices, both mixed
filter directions, biased footprints, all four mip filters and Force LOD0.
Matching filters and existing 2D/cube/G8B8 paths compare exact parent pixels.
Use `--assembler yasm` as needed; `--windows-paths` runs a Windows harness from
WSL. Scalar and cross-API volume comparisons allow one color LSB for rounding.
Probe observations are test-only; the emulator adds no readback or waits.

The region-TB fixture now enforces the core ownership contract (one Xbox CPU, MTTCG disabled, one region, no region mutexes and reviewed index callers). Its 25,000 checks cover 400,074 recycles, 112,111 removals and 104 flushes; production region allocation/exhaustion and 100 safe-work resets also retain prologue/highwater semantics. `REGION_TBS_BINARY` and `REGION_TBS_SKIP_RUN=1` retain native fixtures. These restrictions are ownership requirements, not synchronization for a future concurrent reader or translator.


The volume runner's `--mip-chains` matrix uses 8x8x8 swizzled bases and a
contrasting 4x4x4 lower mip. It tests nonmip filters, guest maximum zero and
Force LOD0 on an intact chain, sampling both end depth slices. Full chains,
positive minimum clamps, matching filters and old single-level volumes compare
exact parent pixels. Force LOD0 retains the stored volume chain and dimensions;
the CPU shape fixture checks this across 3,840 metadata combinations.

The region ownership guard now verifies TCG startup asserts one machine CPU and passes a literal one-translator count. The obsolete MTTCG selector/property is gone; startup had always forced it off. Existing translation-cache size, split-WX and instruction-debug controls remain.

	est-store-compare.py CHECKOUT [CC] extracts the production unchanged-store comparator. It checks 2,263,040 value/width/alignment combinations, all scalar and partial widths, guarded page edges, high-bit truncation, and no-read behavior for unknown/wide writes. Linux defaults to ASan/UBSan; STORE_COMPARE_CFLAGS, STORE_COMPARE_BINARY and STORE_COMPARE_SKIP_RUN support retained native Windows fixtures.


`test-fixed-depth-sampler.py CHECKOUT PARENT CANDIDATE HARNESS SCRATCH`
checks fixed D16 filtering before shadow comparison on swizzled and linear 2D
images. It covers five reference depths, four biases, mixed min/mag filters,
all mip modes, guest maximum zero and Force LOD0 with a contrasting lower mip.
Matching filters, swizzled full chains and positive minima, and existing
color/cube/volume/G8B8 paths compare exact parent pixels. Linear images with
stale mip fields remain single-level and use the scalar model. Depth probes
return binary comparison results;
it adds no emulator readback, shader code, settings or runtime diagnostics.
Use `--assembler yasm` as needed and `--windows-paths` for a Windows harness in WSL.

	est-notdirty-host.py CHECKOUT [CC] checks production dirty/watchpoint handling with resolved host addresses: 2,048 dirty/Boost/watch/alignment cases plus unknown probe writes. It checks physical-address dirty tracking separately from the host pointer and rejects a repeated RAM lookup. NOTDIRTY_HOST_CFLAGS, NOTDIRTY_HOST_BINARY and NOTDIRTY_HOST_SKIP_RUN support native Windows fixtures. The store comparator fixture now takes the resolved host pointer directly.

### OpenGL composed display mailbox

`test-gl-display-mailbox.py CHECKOUT [CC]` extracts production mailbox publish,
take, acquisition, front release and slot reuse functions with real libretro-common
atomics and threads. It checks 4,098 controlled schedules, persistent front
ownership, superseded tickets, early boot and 100,000 attempts per thread.
Fake GPU fences never signal: only server waits are allowed, and every created
fence must be released. Linux defaults to ASan/UBSan. `GL_MAILBOX_BINARY`,
`GL_MAILBOX_SKIP_RUN` and `GL_MAILBOX_CFLAGS` support native fixtures.

The renderer composes directly into its write slot before publication; the
frontend keeps its read slot until the next successful take. The third slot
is a replaceable latest-image mailbox, not an extra queued frame. A fence queued
before returning the old read slot covers its preceding frontend blits and
orders GPU reuse, without a fence/flush per duplicate run. Composition retains the same shader
and image passes, suspends guest/cost queries and invalidates affected state
caches. No live source surface crosses contexts. Renderer teardown frees its
composition resources after frontend use ends. Legacy Vulkan interop and
other emulator blocking paths remain separate.

The source-texture reference pool and per-surface display references are removed.
Composition uses an empty VAO with gl_VertexID, so it needs no VBO; its full-screen
triangle overwrites the complete target, so it needs no preceding color clear.
Only the render context is created for GL; early setup preserves the frontend
context rather than relying on context-local object names from another context.

Published tickets contain only a texture handle, frame stamp, overlay flag and
fence. Each ticket shares the statically aligned 64-byte slot with its image
metadata and frontend fence; full composition parameters remain owner-local.
In the mailbox revision, the Win64 ticket shrank from 120 to 24 bytes, SurfaceBinding from 216 to
208, and PGRAPHGLState from 7,440,896 to 7,440,424 bytes. Static display state
grows from 128 to 320 bytes for the third slot and cache-line separation.
The frontend getter stack falls from 336 to 96 bytes; owner post_scanout grows
from 272 to 400 bytes because it now composes. No hardware cache counters were
measured. Demand downloads remain separate work; idle reports now use interruptible retirement.

`test-opcode-byte.py CHECKOUT [CC]` checks the production byte fetch and general loader: 302,292 first/second-page reads, guarded edges, noncontiguous host mappings, existing/replaced page locks, MMIO recording and fault propagation. First-page reads must avoid the general loader. Linux defaults to ASan/UBSan; OPCODE_BYTE_CFLAGS, OPCODE_BYTE_BINARY and OPCODE_BYTE_SKIP_RUN support native Windows fixtures.


`test-float-depth-sampler.py CHECKOUT PARENT CANDIDATE HARNESS SCRATCH`
checks NV2A F16 decode before filtering and shadow comparison. The matrix covers
zero, exponent boundaries, maximum codes, exact comparison thresholds, bilinear
ramps, mixed filters/biases, stale mip fields and Force LOD0. GPU clears alternate
with encoded RAM textures, including repeated use of one address. Existing fixed
depth/color/cube/volume/G8B8 controls compare exact parent pixels. Use
`--assembler yasm` and `--windows-paths` as appropriate.

`test-f16-gather-sampler.py CHECKOUT PARENT CANDIDATE HARNESS SCRATCH`
compares centered gathers with the four-read F16 implementation and an independent
2D scalar model: 6,144 exact pixels per API across checkerboard rows/columns,
exponent boundaries, mixed filters, LOD biases, wrapped coordinates, native
surface copies and repeated RAM/native use of one address. Matching-filter cases
also enable Force LOD0. Use the same assembler/Windows path flags as above.
The shader still decodes each texel before interpolation; only the linear fetch
changes. Point sampling remains one explicit LOD0 read. A dedicated F16 key bit
refreshes cached F16 shaders while preserving other shader keys. No runtime
readback, synchronization or diagnostic code is added. Game FPS effects remain unmeasured.

`test-float-depth-codec.py CHECKOUT [--gpu] [--output DIRECTORY]` verifies shader
key/seed ABI and fixed-depth byte compatibility. With `--gpu`, `glslc` compiles the
actual production decoder expressions and a Vulkan fixture checks all 65,536
codes against an independent exponent/mantissa model. Test-only readbacks are
not linked into the emulator. Floating-depth render/guest-readback precision
outside texture sampling retains its existing limitations.

`test-float-depth-legacy.py CHECKOUT PARENT [CC]` compares 512 non-F16 shadow
shader snippets byte-for-byte with the selected parent, including fixed 16/24-bit
depth, all comparisons, projective/rectangle sampling and GL/Vulkan generation.

The opcode-fetch fixture also exercises the production 16/32-bit fetches: 523,688 endian/alignment/page-crossing/MMIO cases. First-page scalar reads avoid the general loader and aligned reads must still use the existing atomic primitives. Both general-loader output and recorded MMIO bytes retain host/guest byte-order behavior.

The page-collection fixture now checks append/repeated-tail and reverse-order insertion explicitly, including heap growth and absent pages. Every memmove must have nonzero length; ascending inserts make none. Its 160,000 reference cases cover 69,309 contention retries and 21,570 grown arrays. PAGE_COLLECTION_CFLAGS, PAGE_COLLECTION_BINARY and PAGE_COLLECTION_SKIP_RUN retain native Windows fixtures.

The page-collection fixture additionally counts descriptor lookups for a TB with an instruction offset, same-page aliases and a distinct second physical page. A one-page collection must do just the range scan lookup; a different page must still be found and locked. All 160,000 contention/growth/reference cases remain covered.

The page-collection fixture also extracts the production physical-range invalidator. Each of its 160,000 cases checks ascending target callbacks, descriptor identity, first/full-page and last-page clipping, and all expected locks held until completion. Pages collected outside the requested range must not become invalidation targets, and descriptor lookup counts must stay unchanged during the invalidation pass.

The page-collection fixture now extracts the small-store invalidation wrapper too. Its 23,040 aligned 1/2/4/8-byte stores cover all offsets on a page with absent, aliased or distinct second TB pages. Checks require one first-page descriptor lookup, exact store bounds/return-address forwarding, necessary second-page lookup/locks and no-descriptor early return. Existing range and contention tests remain covered.

`test-x86-access-ptr.py CHECKOUT [CC]` extracts the production pointer classifier and all scalar access load/store functions. Its 327,500 page-split/width/offset/RAM-MMIO cases verify pointer selection, actual load/store bytes and slow-helper routing with MMU index/return address preserved. Original code reproduces 44 crossing and 32,774 second-page classification failures; no bad pointer is dereferenced during baseline reproduction. X86_ACCESS_PTR_SOURCE supports a baseline source; X86_ACCESS_PTR_CFLAGS, X86_ACCESS_PTR_BINARY and X86_ACCESS_PTR_SKIP_RUN support native fixtures.

The x86 access fixture also extracts both preparation functions. Its 655,360 poisoned-object/reuse/page-offset/width/RAM-MMIO/adjacency cases verify all live fields, first/second probe order and sizes, MMU index/return address, contiguous coalescing and immediate reuse for a one-byte access. Struct padding is not an observable field or save-state payload. Existing 327,500 pointer/load/store cases remain covered.

`test-jvs-frame.py CHECKOUT [CC]` extracts production JVS response framing and compares it with the original full-frame path. Its 544,840 payload/length/pattern/alias/canary cases cover 0..256 payload bytes and all 0..264 output capacities, including checksum/count wrap and zero-capacity NULL output. JVS_FRAME_CFLAGS, JVS_FRAME_BINARY and JVS_FRAME_SKIP_RUN support native fixtures. Parsing and command handling finish before output framing, preserving input/output aliasing.

The packed-overlay GPU fixtures dispatch one invocation per YUY2 pair and check exact pair-based workgroup counts, including a matching preflight limit and zero-group fallback. Vulkan: 45 cases/2,251,544 exact pixels, snapshot mutations and synchronization validation; GL oracle: 222 cases/3,270,359 exact pixels. VULKAN_OVERLAY_CFLAGS, VULKAN_OVERLAY_LIBS, VULKAN_OVERLAY_BINARY and VULKAN_OVERLAY_SKIP_RUN retain native fixtures. Test-only OVERLAY_ALLOW_NO_VALIDATION permits a native device without validation layers; output explicitly reports validation_enabled=0. The default still requires validation. Direct imported-RAM overlay reads were rejected because they would lose the required pre-submission snapshot.

### Deferred OpenGL flip retirement

`test-gl-flip-retirement.py CHECKOUT [CC]` extracts the production flip start,
readiness, completion and discard functions, owner-dispatch hook, FIFO gate and
idle report-poll guard. It checks 615 controlled schedules and 5,000 mixed
ordinary/cost frames: unavailable/ready markers, bounded one-shot polls,
previous/current-frame selection, unchanged pending queries, latched scale,
exactly-once completion/deadline billing, reset-independent backpressure and renderer teardown.
Fence-allocation failure remains backpressured without repeated allocation or
query reads. Linux defaults to ASan/UBSan; `GL_FLIP_BINARY`, `GL_FLIP_SKIP_RUN`
and `GL_FLIP_CFLAGS` support native fixtures.

A flip closes its query segment and submits one fence. Ordinary flips defer
against the previous frame; frames containing cost queries defer against the
current frame before reusing query slots. At most two markers are retained.
The FIFO owner polls once per dispatch, including empty-FIFO dispatches; a busy
result does not self-kick, replay the flip or consume new methods. The idle
report-poll callback is skipped while a flip is pending. Existing frontend/frame requests provide wakeups. The runtime pending
flag survives a guest reset until retirement. Neither it nor GPU handles are
serialized. Renderer teardown discards pending private cost samples while keeping
guest reports on their existing drain path. No guest query result is dropped.

The core currently forces the cost model off; its asynchronous query path has
controlled-model coverage rather than native-game coverage. Demand downloads,
lifecycle drains remain separate blocking paths. Idle report retirement now
uses interruptible eventcount parks, described below. The native
GPU ROM's 3,120-3,135 and 3,280 FLIP_STALL sequence checks PBO freshness and
subsequent FIFO progress through 3,400 frames.

Fence readiness uses `glGetSynciv(..., GL_SYNC_STATUS, ...)` rather than a client
wait. A signaled fence covers preceding GL work; see the [Khronos synchronization
specification](https://registry.khronos.org/OpenGL/specs/gl/glspec45.core.pdf).

Win64 code generation keeps PGRAPHState at 4,295,312 bytes, including the new
owner flag in existing padding; the following guest_wrote_regs offset is
unchanged. GL state grows by 16 bytes and the constant renderer interface by
8 bytes. GL flip start and readiness each use 176 stack bytes, matching the
old flip handler; the eager call compiles to a tail jump so those frames do
not nest. These are layout/code-generation checks, not hardware cache counters
or game-latency measurements.

While blocked, one early display attempt may publish GPU-ordered imagery
without awaiting physical completion. Later display requests stay latched; on
retirement they are serviced before the FIFO resumes, without a second GPU
poll or an extra queued video frame. A failed marker disables early display
work. The model also checks this budget, coalescing and maintenance progress.


EP ownership regression coverage
-------------------------------

`python3 libretro/tests/test-dsp-ownership.py . cc` extracts the production
RAM guards, DMA walker, post dispatcher, worker loop and sparse TLB updates
into host fixtures. It covers shared table reads, conflicting writes, direct
pointer lifetime, invalid/table-alias admission, imported GPU page leases,
victim TLB swaps, remapping, resize and fail-closed index overflow. A threaded
case runs 20,000 delayed EP jobs through signed and zero epoch wrap, followed
by stop/join; a separate stress case races 200,000 external borrow lifetimes
against DSP admission. Set `DSP_OWNERSHIP_SANITIZE=1` for ASan/UBSan.

The guard-retirement fixture checks 2,000 arm/retire epochs: missing and
external CPUs retain protection, the CPU retires each epoch once, and 66,001
inactive checks avoid both CPU lookup and callback access, including before
setup and after teardown.

The DSP guest-RAM CI job runs these checks, the original GP scratch-growth
regression and the exact frontend audio-cadence regression. These host tests
need no game, BIOS or GPU. Game captures and throughput still need live
testing; passing these fixtures is not a claim of a universal latency gain.

`test-vk-ready-order.py CHECKOUT [CC]` extracts production ready-cache push, lookup, take and clear logic. A FIFO reference checks 52,317 states, 35,541 module lifetimes and 414 window compactions, including collisions, repeated keys, free-slot permutation, exact reference transfer and clear/reuse. Full-cache compaction occurs once per 32 evictions, not every eviction: index-move traffic averages 96 rather than 3,068 bytes per eviction, excluding the unchanged module record assignment. Static headroom is 32 indices plus a pointer (136 bytes on Win64); capacity stays 768. A circular prototype was rejected for lookup regressions. VK_READY_SOURCE supports a parent source; VK_READY_CFLAGS, VK_READY_BINARY and VK_READY_SKIP_RUN support native fixtures. -DBENCH enables isolated bookkeeping timing with compact synthetic keys and a reference stub; modes cover overflow push, miss and take/refill at base and displaced windows. These are not shader compilation or game FPS measurements.

### Vulkan flip command-slot retirement

`test-vk-flip-retirement.py CHECKOUT [CC]` extracts the production Vulkan flip,
readiness callback and shared owner continuation. It checks 2,176 schedules
covering both slots, rotation enabled/disabled, empty flips, cost-mode settlement,
busy/ready fences, deferred deadline billing and maintenance that settles a slot
before its next poll. Busy attempts make exactly one status probe, retain FIFO
backpressure and leave the other slot untouched; successful retirement happens
once. The flip uses its existing settle probes without adding another probe.
Linux defaults to ASan/UBSan; `VK_FLIP_CFLAGS`, `VK_FLIP_BINARY` and
`VK_FLIP_SKIP_RUN` support native fixtures.

The runtime flag and owner hook introduced for GL now also defer subsequent
Vulkan methods while the command slot selected after a flip is busy. Independent
display submissions keep their existing bounded ownership rules. No new buffers,
PSOs, copies, locks or frame queue are introduced. Mid-method slot exhaustion,
demand readbacks, cost-mode synchronous finishes and lifecycle drains still have
their existing waits. Renderer teardown clears the owner flag after command
resources have settled; the flag is runtime-only and is not migrated.

Win64 layout checks leave PGRAPHState at 4,295,312 bytes, PGRAPHVkState at
5,845,216 bytes and PGRAPHRenderer at 232 bytes. The flip and finalize handlers
retain their previous 64-byte stack frames; the readiness callback also uses
64 bytes (ABI register saves and outgoing-call space). No hardware cache-miss
or game-latency improvement is established by these code-generation checks.

### Vulkan eager-submit retirement

`test-vk-eager-retirement.py CHECKOUT [CC]` extracts production eager submission,
slot retirement and command-buffer reclamation. Its 12,480 schedules cover
both command slots, recording/no-recording, threshold edges, free/busy/ready
fences, unavailable/error status, and submission-serial rollover. A successful
readiness probe clears the slot's pending bit and performs normal GPU-RAM
ownership, garbage and timestamp retirement before rotating; the following
reclaim must make neither a second status probe nor a fence wait. Busy attempts
retain pending ownership and make no submission. A separate genuinely busy
reclaim retains its existing wait policy. Linux defaults to ASan/UBSan;
`VK_EAGER_CFLAGS`, `VK_EAGER_BINARY` and `VK_EAGER_SKIP_RUN` support native tests.

This changes no eager threshold, PSOs, image copies, buffers, locks or frame
queue. It removes a redundant wait API call for an already completed slot,
not the separate waits for resource exhaustion or demand readbacks.

Win64 code generation inlines the small eager helper into draw_end and keeps
its stack frame at 96 bytes. PGRAPHState (4,295,312 bytes), PGRAPHVkState
(5,845,216 bytes) and PGRAPHRenderer (232 bytes) are unchanged. These layout
and assembly checks do not establish hardware cache-miss or game-latency gains.

`test-g8b8-gather-boundary.py CHECKOUT [--parent REV] [--cc CC] [--output DIR]`
executes the production base-level signed G8B8 shader in OpenGL against a scalar
2D reference. It checks 81,920 signed components just before/after texel boundaries,
including 1x64, 2x7, 4x4, 7x2 and 64x1 images and all sixteen wrap/edge/mirror/border
address pairs. Native Windows accepts `--epoxy-root DIRECTORY` for a separate
libepoxy build. The four texture lanes are selected at the calculated quad center;
weights still come from the original coordinates. Point and explicit-mip paths
retain their behavior. Only base-level gather shader keys gain a revision tag in
the otherwise mip-only upper stage flag, preserving seed layout and other keys.

`test-g8b8-centered-sampler.py CHECKOUT PARENT CANDIDATE HARNESS SCRATCH` checks
3,456 guest pixels per API: near-boundary mixed-filter colors, ordinary bump/color,
luminance profiles, mixed filters/biases, swizzled/linear single-level images,
edge/border addressing, explicit-mip controls and Force LOD0. Ordinary pixels must
match the parent exactly; boundary pixels follow the scalar model. The builder's
`--mixed --gather-boundary before|after` selects flat coordinates at either side
of a texel boundary. Use `--windows-paths` for a Windows harness through WSL.
These tests exercise the implemented filtering model, not a hardware NV2A oracle.

The eager-retirement fixture additionally covers 1,032 serial-proof reclamation
schedules and eight production allocation-retry/reclaim sequences. Equal or
older submissions skip waiting without a new status probe; newer unfinished
submissions retain the wait and its optional reuse-time accounting. Serial
rollover and exactly-once ownership/garbage/timestamp cleanup are checked. A
completed later submission on the same queue also proves the earlier slot safe,
even if the earlier slot's own status was not observed ready. Allocation retry
still leaves slot flags and GPU-RAM ownership for normal slot cleanup.

The serial-proof check preserves the Win64 reclaim function's 96-byte stack
frame and all renderer structure sizes. It adds no allocations, PSOs, copies,
locks, status queries or frame queue. Hardware cache-miss and game-latency
effects are unmeasured; unfinished submissions still have their existing wait.

`test-lru-promote.py CHECKOUT [CC]` compares production lookup with the original unconditional unlink/reinsert algorithm over 30,000 operations. It checks exact global/bin ordering, callbacks, counts, collisions, blocked eviction and flush/reuse. A separate 10,000-hit test maps the live MRU container/node read-only: the optimized lookup must not write its links. The original source faults on that negative control. Linux defaults to ASan/UBSan. LRU_PROMOTE_SOURCE, LRU_PROMOTE_CFLAGS, LRU_PROMOTE_BINARY and LRU_PROMOTE_SKIP_RUN support parent/native runs; -DBENCH enables exploratory hit/collision/miss timing with small keys. Callbacks still run on every hit; this only removes redundant container-link updates.

`test-g8b8-mip-boundary.py CHECKOUT [--cc CC] [--output DIR]` executes the
production explicit-mip signed G8B8 generator on an OpenGL compute context.
Native Windows accepts `--epoxy-root DIRECTORY` for a separate libepoxy build.
The independent scalar oracle checks 1,327,104 signed components in 162 variants:
both GL/Vulkan generation branches, all nine wrap/mirror/edge address pairs,
matching point/bilinear/trilinear and mixed min/mag filters, lower clamps zero,
one and four, integer/fractional LODs and a 64x32 chain through its 1x1 tail.
Coordinates lie just before/after texel boundaries and include negative periods.
Each mip uses different byte patterns with signed discontinuities.

A test uniform supplies the host LOD-query result; the production shader still
selects levels, applies lower clamps, addresses integer taps, decodes each tap
and interpolates. Both generated branches execute in OpenGL here; this does not
validate native Vulkan execution or host derivative/query behavior. Existing guest
GL/Vulkan tests cover actual sampler queries. A deliberately shifted fetch fails
the scalar oracle. Test buffers/readbacks are confined to the standalone fixture.

The retirement fixture also extracts the production full-slot drain. Eighty
schedules cover both slot orders, every pending mask, known/unknown completion
and serial rollover. Draining waits only on the latest unproved submission's
fence, or makes no driver call when all completion is already proved. It makes
no status probes and releases neither GPU-RAM ownership nor garbage/timestamps
before the wait returns. Repeated drains never drop ownership twice.

Both command slots submit to the same queue. The latest submission's fence
covers preceding commands as specified by the [Vulkan synchronization rules](https://docs.vulkan.org/spec/latest/chapters/synchronization.html).
This reduces redundant synchronization calls; it does not make a necessary
full drain asynchronous. No buffers, copies, PSOs, locks or frame queue change.

Win64 full-drain stack usage falls from 96 to 80 bytes; renderer structure
sizes remain unchanged. This is a code-generation result, not a measured
hardware cache-miss or game-latency improvement.

The LRU fixture also compares flush, eviction and active visits (including current-node eviction) with independent original scans. Guarded pages prove full-cache allocation avoids an unrelated newer node and empty helpers avoid all list/bin/node metadata. A parent header without count gates must fault on a read. Benchmark mode 4 fills 51,200 nodes (the Vulkan shader-module cache capacity) before measuring 1,000 full-cache misses; modes 0-3 retain the small-cache comparisons. These synthetic timings are not frame-rate measurements.

`test-convolution-projection.py CHECKOUT [--parent REV] [--cc CC] [--output DIR]`
executes the production convolution helper on an OpenGL compute context. Native
Windows accepts `--epoxy-root DIRECTORY` for a separate libepoxy build. An
independent scalar kernel checks 1,572,864 RGBA components per GPU across Q
0.25/0.5/1/2/4/-1, normalized and rectangle coordinates, point/linear host sampling,
four image shapes and all sixteen wrap/edge/mirror/border pairs. The 262,144 Q=1
controls match parent float results within 1e-6. Linear host sampling permits one
color LSB for hardware interpolation precision. Zero-Q behavior is outside this
fixture. The helper keeps its existing projective divide and nine kernel reads;
o new runtime transfer, synchronization or diagnostic path is added.

`test-convolution-sampler.py CHECKOUT PARENT CANDIDATE HARNESS SCRATCH` checks
1,728 guest pixels per API: Gaussian projective Q 0.5/1/2, Force LOD0 and native
RGBA8/RGB565/cube/volume/F16 controls. Q=1 and ordinary controls must remain exact
parent pixels; other convolution pixels follow the scalar two-column kernel.
Use `--windows-paths` for a Windows harness through WSL.

`test-convolution-state.py CHECKOUT [--cc CC]` checks 168 kernel/filter/stage keys.
Only convolution enum values gain a revision bit to refresh cached programs;
disabled convolution and other stage bytes keep their keys and enum size. The
existing Quincunx Gaussian fallback and min-only convolution selection remain
separate limitations; these tests establish projected kernel spacing.

The ring-pressure fixture adds 136 auxiliary full-drain schedules: every ring
start, zero through eight pending submissions, and an optional open recording.
A nonempty full drain waits once on the newest submission's fence, releases
completed slots in FIFO order, and leaves open recording ranges intact. Empty
and repeated drains add no waits or duplicate ownership releases. The existing
108,609 byte-ownership allocations over 20,000 delayed submissions and 20,000
PVIDEO resize/upload/display schedules remain covered. Ring pressure and
single-slot reuse retain their existing oldest-first behavior.

`VK_RING_BINARY`, `VK_RING_SKIP_RUN` and `VK_RING_CFLAGS` allow native fixtures;
the default compiler flags retain UBSan with traps. Full auxiliary drains still
wait for real completion. The change adds no state fields, buffers, copies,
PSOs, locks, status polls or video frame queue. Newest-fence coverage uses the
same-queue synchronization rule cited above.

Win64 code generation inlines the release helper. Oldest-slot retirement
keeps its 96-byte stack frame; the full drain now uses one 96-byte frame
instead of a 48-byte caller nested with the old 96-byte retirement function.
Renderer structure sizes are unchanged. No hardware cache-miss or game-latency
gain is established by this code-generation check.

`test-convolution-tables.py CHECKOUT BUILD PARENT_PSH_OBJECT SCRATCH` links the
full production fragment generator and core SPIR-V compiler from a configured
core build. Save the parent's `hw/xbox/nv2a/pgraph/glsl/psh.o` before building the
candidate; use matching build settings and unchanged public interfaces. The
script gets compiler flags from `make -n -W`. `--make`, repeated `--make-arg`,
`--bash` and `--objcopy` select host tools. Native Windows normally passes
`--make-arg platform=win` with MinGW/MSYS tools on PATH. Only standalone test
object copies lose PE unwind metadata to let section GC discard unrelated code;
the emulator build objects remain intact.

The matrix covers 384 complete GL/Vulkan shader sources: disabled/ordinary/shadow
stages, normalized/rectangle coordinates, both convolution kernels and every
convolution-stage mask. Programs needing convolution get one unchanged table
pair even with multiple stages; 264 unused pairs disappear. Removing those
declarations and separately unused common/dot-mapping definitions must leave the
remaining parent/candidate shader text byte-identical. All 192
Vulkan variants per side compile with the core compiler, and image-operation
counts remain equal. The table pair occupies 336 GLSL bytes. Reported SPIR-V deltas include
other omitted unused helpers in the compared builds. This is module/source-size evidence,
not an FPS or shader-compilation latency benchmark. Cache hits remain valid;
the smaller declarations apply to newly generated programs.

The ring-pressure fixture additionally exercises the production synchronous
readback submission helper in 64 schedules covering every ring start and zero
through seven prior submissions. Each readback waits exactly once on its own
newest fence, retires earlier ownership in FIFO order, and leaves both staging
rings empty for reuse. All owned bytes are checked before release; repeated
drains make no extra waits or duplicate releases. This uses the existing
same-queue completion rule and single-fence drain, with no new buffers, copies,
PSOs, locks, status polls or queued video frames. Necessary readback completion
remains synchronous.

Win64 code generation reduces the synchronous submission helper's stack
frame from 64 to 48 bytes, with unchanged renderer layouts. Hardware
cache-miss and game-latency effects are unmeasured.

Ready-order coverage now exercises oldest-record adoption at every occupancy from 1 to 768 for 33 cycles each, including every window offset and compaction boundary. It checks the full slot permutation and FIFO/reference oracle, with 399,069 checks and 356,181 lifetimes. On the new head-take path, tracked index moves occur only at the existing 32-step compaction boundary. Benchmark mode 5 repeatedly adopts and reinserts the oldest record. Physical free-slot reuse order is private; active FIFO order and module ownership are preserved.

For queues of at most 32 entries, oldest adoption retains the direct shift: its zero to 124 bytes avoids window compaction for tiny queues. Larger queues use pointer advancement. The occupancy oracle asserts both copy policies; benchmark modes 6/7 adopt and reinsert the oldest of 1/16 entries.

`test-dotmap-helpers.py CHECKOUT BUILD PARENT_PSH_OBJECT SCRATCH` uses the same
configured-build driver/link options as `test-convolution-tables.py`. Save the
parent `glsl/psh.o` before building the candidate. It compares 448 complete
GL/Vulkan variants per side across all eight mappings: ordinary/disabled/shadow
programs, DOTPRODUCT, DOT_ST/ZW, diffuse/specular reflection, 3D/cube sampling,
repeated mappings and all 64 diffuse-reflection look-ahead pairs. Inactive next
stages still supply the reflection mapping. Unsupported hemisphere and constant
specular fallback behavior is preserved. One case combines convolution and dot
mapping to check their independent lazy declarations.

Only referenced mapping definitions may remain, once each and byte-identical to
the parent. Removing mapping definitions and independently unused common helpers must leave
the remaining complete shader text byte-identical. All 224 Vulkan variants per side compile with the
core compiler; image-operation counts remain equal. The eight mapping definitions occupy 882 GLSL bytes. Whole-module deltas may
include other omitted common helpers in the compared builds. Keys and persistent state
are unchanged. Cache hits remain valid; the generator checks establish code size. See the
compiler benchmark below for CPU timing.

GL shader-cache version coverage checks both borrowed mapped-key views and owned key copies. It verifies exact borrowed byte addresses and contents at 32 input alignments, every truncated prefix of a 2 KiB-key header, legacy/changed versions and the owned round trip. Linux defaults to ASan/UBSan; GL_SHADER_VERSION_CFLAGS, GL_SHADER_VERSION_BINARY and GL_SHADER_VERSION_SKIP_RUN support native Win32/Win64 fixtures. Windows temporary files use the user temporary directory and delete-on-close. No mapped key pointer escapes unmapping.

The display-retirement fixture adds 36,864 production teardown schedules over
all pending masks, rotated slot serial orders, known-completion points, and
with/without the split-queue semaphores. It checks at most one wait on the
newest unproved submission, exactly-once resets and destruction, completion
before resource destruction, cleared handles and idempotent teardown. Existing
20,000 display retirement workloads and per-frame busy-slot tests remain covered.
All display slots submit to one display queue even when it differs from the
renderer queue, so the newest display fence covers the older submissions.

`VK_DISPLAY_RETIRE_BINARY`, `VK_DISPLAY_RETIRE_SKIP_RUN` and
`VK_DISPLAY_RETIRE_CFLAGS` support native fixtures; default flags retain UBSan
with traps. This changes only teardown order. No state fields, buffers, copies,
PSOs, locks, status polls or video frame queue are added. A necessary teardown
drain still waits.

Win64 finalize_display stack usage grows from 128 to 144 bytes to retain the
circular traversal start across driver calls; the wait helper stays inlined.
This is confined to teardown, with unchanged renderer layouts and no per-frame
stack change. Hardware cache-miss and game-latency effects are unmeasured.

The shader-cache handoff fixture also executes the production disk reader against the production writer output. Across 32 mapping alignments, both retained mappings and the 64-mapping fallback preserve state/format/binary bytes and release ownership exactly once. The fallback frees input before publication, catching state copies made too late under ASan. Every truncated binary is rejected without queue publication and its invalid fixture file is removed. Existing 18,000 concurrent publications, owner-only LRU, shutdown and immutable save snapshots remain covered.

Sparse jump-cache flushing
-------------------------

The jump cache keeps 256 bytes of preallocated atomic group summaries.
Insertions and class promotions publish their pointer before marking its
64-slot group. Full invalidators atomically take the groups and retain atomic
pointer clears; individual page/TB invalidators may leave conservative marks.
No generation checks are added to successful lookups.

`test-tb-secondary.py CHECKOUT [CC]` includes 2,000 sparse/dense-boundary
flush epochs against a full-scan oracle, high/low cache-mode transitions and
repeated empty flushes. It forces a flush immediately before publication in
all four insertion/promotion paths. On POSIX, a negative control that marks
before publication must fail because it loses tracking of a surviving entry.
The existing 24,000 lookup/invalidation scenarios remain enabled.

### Bounded Vulkan scanout-mode retirement

`test-vk-scanout-mode-retirement.py CHECKOUT [CC]` extracts production size
accounting, eviction planning, resource release and pressure-driven reservation.
Thirty thousand schedules compare feasibility with all 256 eviction subsets,
covering independent aux/display ownership, LRU choice, scratch images, pool
acceptance/refusal, insufficient safe space and oversized single modes. Failed
planning must leave the cache untouched; actual retries probe each queue domain
once and release no image until both prove completion. Empty capacity fits
without driver probes. Linux defaults to ASan/UBSan; `VK_MODE_CFLAGS`,
`VK_MODE_BINARY` and `VK_MODE_SKIP_RUN` support native fixtures.

Private mode backgrounds are never main-draw attachments. Track their last
aux and display submissions, then reuse the existing image pool only after
both domains retire. Mode changes keep the eight-entry/32 MiB policy and its
single-oversized-mode exception. A busy cache retains its images and frontend
publication, with the existing display retry flag preserving mode-only updates.
No shader/PSO variants, image copies, locks or video frame queue are added.
Other materialization, readback, allocation and ring-pressure paths retain
their separate policies; this removes the explicit mode-eviction drain only.

The GPU ROM at frames 4500-4620 initializes a purple RAM background with a red
GPU tile, cycles twelve distinct widths at fixed pitch, then returns to the
first evicted width. Pixel checks and right-edge inside/outside probes verify
content and geometry. Native save/load during mode churn and reset also run
with strict video/audio cadence; both RAM restores must be byte-exact.

Win64 layout checks add two 64-bit serials per mode: mode size grows from
139,976 to 139,992 bytes and renderer state from 5,845,216 to 5,845,344 bytes
(128 bytes total). PGRAPHState remains 4,295,312 bytes. Display render stacks
remain 1,056 bytes internally and 112 bytes at the public entry. Composition
grows from 91,856 to 91,872 bytes; the mode-miss planner uses a 72-byte frame.
Hardware cache-miss and game-latency effects are unmeasured. Scratch-image
lifetime and budget paths have controlled-model coverage; native GPU testing
uses NVIDIA shared-device presentation.

`test-common-helpers.py CHECKOUT BUILD PARENT_PSH_OBJECT SCRATCH` uses the same
configured-build driver/link options as the table/dot-mapping tests. Save the
parent `glsl/psh.o` before building the candidate. It checks 772 complete fragment
variants per side on GL/Vulkan generation: the dot/reflection matrix plus all
fifteen G8B8 sign masks, point/linear/mixed/mip paths, valid rectangle layouts,
bump/luminance with 2D/3D destinations and native/fallback cube coordinates.

Only referenced common definitions may remain, once each and byte-identical to
the parent. Dot-mapping dependencies must precede their callers. Removing common
definitions leaves all remaining shader text byte-identical, including the
Kahan/barycentric depth code. The unused inverse sign helper is never emitted.
All 386 Vulkan variants per side compile with the core compiler; image-operation
counts remain unchanged. The ordinary 2D cases omit 1,554 GLSL bytes and 4,008
SPIR-V bytes in the checked builds. Keys and persistent state are unchanged.
The scalar GPU G8B8 boundary/mip fixtures continue to test actual sign decoding;
`shader_helpers_fixture.py` provides only test-side extraction/canonicalization.
These results measure source/module size, not FPS or shader compile latency.

Disk-result allocation now uses the non-clearing allocator. The handoff fixture poisons non-clearing allocations with 0xa5 and asserts that every valid disk-reader round trip takes that path, then checks parsed state, binary owner/format/size, MPSC adoption and release for retained/capped mappings. This verifies that allocator zeros are not required by any published field or the queue link.

### Vulkan scanout upload accounting

`test-vk-scanout-upload.py CHECKOUT [CC]` extracts the production upload-region
loop. Eight thousand schedules over four batches each cover 16/32-bit pixels,
packed fallback staging and direct guest RAM, varied pitches/rectangles and
nonzero allocation prefixes. A per-byte reference checks every copied row,
alignment gap and guard byte; descriptor offsets, row lengths, extents and
GPU-RAM ownership spans must match exactly. Direct RAM makes no CPU image
copy and leaves staging accounting untouched. Linux defaults to ASan/UBSan;
`VK_SCANOUT_UPLOAD_CFLAGS`, `VK_SCANOUT_UPLOAD_BINARY` and
`VK_SCANOUT_UPLOAD_SKIP_RUN` support native fixtures.

The fallback now advances packed staging offsets in the existing copy loop,
removing the second rectangle pass. Direct guest-RAM uploads omit packed
staging arithmetic. The initial full-recording capacity reservation and final
flush remain intact. No additional copies, buffers, PSOs, locks, waits, queue
submissions or video frame queue are introduced.

Win64 composition stack usage falls from 91,872 to 91,856 bytes; renderer
structure sizes remain unchanged. Hardware cache-miss and game-latency effects
are unmeasured. Both direct-RAM and fallback copy paths have byte-exact
controlled-model coverage; native GPU regressions additionally exercise the
renderer and mode-change lifecycle.

`bench-fragment-compiler.py CHECKOUT PARENT_SOURCES CANDIDATE_SOURCES OUTPUT`
measures the production GLSL-to-SPIR-V compiler on paired generated `1-*.frag`
files (the Vulkan variants exported by the full-generator fixtures). Use `--cc`
for a compiler path and optional `--cpu INDEX` to pin only each benchmark child
thread to an available logical CPU. Preserve before/after source directories
when running the generator comparisons; no emulator is loaded by the benchmark.

The script permits only omitted helper/table declarations with identical
remaining code and used definitions, deduplicates paired sources, writes
canonical LF bytes, and preloads the corpus before timing. Both SPIR-V 1.5/1.6
run with exported layouts enabled, matching fragment compilation; uniform
metadata must stay equal. Eight paired rounds alternate execution order after
warm-up and baseline calibration of at least 0.4 process-CPU seconds per batch.
The timed binary has no allocation wrappers. A separately built binary records
direct malloc/calloc/realloc calls and requested capacities, excluding allocator
internals; these are not peak/live/RSS memory measurements.

Output includes corpus hashes, compiler identity, raw JSONL samples and paired
median/min/max timing ratios. CPU clocks can be quantized and host scheduling or
frequency can vary; use the paired samples rather than a single absolute time.
There is no timing pass/fail threshold and no automatic CI benchmark. This is
compile/result-disposal work on a synthetic corpus, excluding shader generation,
I/O, cache lookup, driver/pipeline creation and gameplay. It does not measure
game FPS or stutter. All instrumentation stays in the standalone executable.

`test-seed-inplace.py CHECKOUT [CC]` compares production sibling compaction with the original copy filter using the real ShaderState layout, canonicalizer/zeroing helpers, XXH3 and compatibility hash set. Its 128 cases cover 76,464 inputs across counts 0 through 4,096, disabled/enabled Boost, already-dynamic states, all-unique/all-duplicate/mixed families, forced hash collisions, exact output bytes/order, repeated filtering and boundary canaries. Linux defaults to ASan/UBSan; SEED_INPLACE_CFLAGS, SEED_INPLACE_BINARY and SEED_INPLACE_SKIP_RUN support native Win32/Win64 runs. The caller retains its original owned allocation and only the returned prefix is live.

The surface-blend fixture adds 73,728 register-bit/format/feature schedules.
The production dirty check and key builder share their canonical register
projection; a retained copy of the previous projection verifies identical
key values. Dynamic and unused bit writes must reuse the pipeline, while
static key differences and shader/texture/render-pass/clear/vertex-input
changes still invalidate it. An explicit dynamic-blend update after clearing
the dirty map must still emit the changed host state. Existing 16,384 blend
mapping, format-transition and dynamic-state cases remain covered.

`SURFACE_BLEND_BINARY`, `SURFACE_BLEND_SKIP_RUN` and `SURFACE_BLEND_CFLAGS`
support native fixtures, retaining UBSan with traps by default. Existing keys,
PSO/shader variants, dynamic-state emission and pipeline lifetime are unchanged;
no buffers, copies, locks, waits or video frame queue are introduced.

The fixture also executes the production create_pipeline fast-path prefix:
a dynamic-only write must return before key construction after clearing dirty
markers; speculative-ahead work and real static/shader changes must still
reach the key-building path.

The register table is shared read-only data rather than duplicated local
arrays, and the projection stays inline. Win64 create_pipeline stack usage
falls from 3,552 to 3,536 bytes with unchanged renderer layouts. Hardware
cache-miss, compilation-stutter and game-latency effects are unmeasured.

The seed-inplace fixture also executes the production module-key collection block with poisoned destination allocation, real ShaderModuleCacheKey/geometry-predicate construction, actual hashes and deduplication. It compares populated key bytes and order against the original zero-filled destination and asserts that the unused tail stays poisoned. Both geometry/no-geometry families and forced hash collisions are covered. Temporary-key zeroing remains essential for deterministic full-union hashes; only redundant destination initialization is removed.

Seed inputs now carry varied nonzero FixedFunctionVshState padding. Compaction and destination collection use explicit memcpy for complete object representations; they do not rely on C structure assignment preserving padding bytes. The real-layout output and hash oracle checks remain byte-exact.

The surface-blend fixture additionally checks all 578 old/new attribute-list
length and dynamic-input feature combinations (zero through sixteen entries).
The previously accepted shrinking-prefix case is a regression: a static
pipeline with extra trailing attributes must not be reused. Existing canonical
keys zero the unused tail, so the next entry's valid format detects a longer
old list without extra fields or key-format changes. The full-length guard
avoids reading beyond the array. Dynamic vertex input keeps its existing
command-state path. No buffers, copies, locks, waits or frame queue are added;
a genuinely different static input layout can require its own PSO.

Win64 create_pipeline remains at 3,536 stack bytes with unchanged renderer
layouts. The guard applies only to static input. Native NVIDIA tests also
exercise a private build with dynamic vertex input disabled before pipeline
initialization; the temporary feature override is removed for delivery.
Static list transitions additionally have exhaustive controlled-model coverage. Valid attributes use supported vertex formats as required by the
[Vulkan attribute description rules](https://docs.vulkan.org/refpages/latest/refpages/source/VkVertexInputAttributeDescription.html).
Hardware cache-miss and game-latency effects are unmeasured.


`test-qht-inline.py CHECKOUT [CC]` exercises the production inline QHT reader and generic entry point with native retro atomics. It covers hash collisions, chained buckets, null pointers, absent keys, map replacement, forced sequence retries and 200,000 concurrent reads. Set `QHT_SANITIZE=1` for address/undefined behavior sanitizers. RCU object lifetime is modeled by retaining every map, bucket and item until readers finish.

Seed batch coverage also checks `pgraph_seed_read_first`: requests are tried in order, only the first nonempty valid result is populated, and every other output is cleared. The 480 source/availability cases compare state-first/module-fallback bytes/counts with legacy combined reads, including missing, stale, malformed and failed-inflate inputs. Reused outputs are poisoned to catch skipped initialization. An independent valid-input check verifies one fewer materialized array while retaining one mapping/inflation and exact local-first order. Bundled dictionary and existing combined/ownership tests remain covered.

The surface-blend fixture also extracts the production pipeline-cache entry
initializer. A poisoned allocation/recycled entry with dynamic line width set
must reset that flag, layout, pipeline handle and draw stamp while preserving
all key bytes. This regression fails before the fix. Clear-pipeline creation
does not request dynamic line width, so it must not inherit metadata from an
old cache occupant. Normal pipeline construction and worker adoption still
assign their computed line-width metadata. No extra key copy, buffer, PSO,
lock, wait or video frame queue is introduced.

Win64 entry-initializer stack remains 8 bytes and create_pipeline remains
3,536 bytes, with unchanged renderer layouts. Hardware cache-miss and
game-latency effects are unmeasured.


`test-sse-ordered-compare.py CHECKOUT [CC]` compares the four production COMIS helpers with real SoftFloat in both optimized and generic configurations. Each configuration covers 1,280,000 cases: float32/64 boundary and random pairs, signed zero, infinities, signaling/quiet NaNs, subnormals, 16 guest rounding/flushing modes, sticky exception flags, operand aliasing and source preservation. On x86-64 it also changes host MXCSR and checks ordinary inputs leave it unchanged. Instrumented fallback counts require zero general comparison calls for ordinary inputs and one for exceptional inputs.

The seed key fixture now compares production construction directly with the original unconditional three-slot clear. It checks all active key bytes and hashes; when geometry is absent, its kind stays zero and all payload bytes retain poison. All four production callers skip that absent payload. Geometry-enabled construction and the complete module-key collection oracle remain covered.

LRU visits now stop after the entry-time active count, even when callbacks evict their current node. The order/callback model still checks 30,000 operations, and the membership fixture covers every occupancy across 8,200 cases. A protected free-node suffix proves the visitor does not dereference it after the last active callback, including current-node eviction; the parent implementation faults on that read as expected. LRU_VISIT_SOURCE, LRU_VISIT_CFLAGS, LRU_VISIT_BINARY and LRU_VISIT_SKIP_RUN support parent/native comparisons. Timings include five occupancies in a 1,024-node pool and are synthetic, not game FPS measurements.

### Interruptible idle report retirement

`test-pfifo-report-retirement.py CHECKOUT [CC]` extracts the complete production
idle path. Sixty schedules cover submitted/unsubmitted/ready reports, pending
flips, kicks, park requests, shutdown and frame-end races. A thousand delayed
retries retain outstanding guest work without waking the emulator for unchanged
results, then complete without another guest submission. Frame-end requests are
acknowledged while results remain unavailable. UBSan is enabled by default.

GL and Vulkan idle callbacks now observe availability instead of blocking for
results. PFIFO parks through libretro-common's eventcount with a 100 us timeout
request while a submitted query is pending; OS scheduling can delay the retry.
Kicks interrupt the park. Unsubmitted queries retain ordinary kick-driven
parking. The existing GL/Vulkan storage fixtures reject premature publication
and check delayed availability; Vulkan retains its one-slot cached result and
zero polling allocations. No new locks, fields, copies, PSOs or frame queue are
introduced. GPU resource-pressure waits, demand downloads and lifecycle drains
remain. Deterministic guest execution still preserves report completion order;
this change does not establish a globally bounded retro_run duration.

Win64 code generation keeps PFIFO thread stack usage at 288 bytes and the
Vulkan poll at 96 bytes; the GL poll decreases from 64 to 48 bytes. PGRAPHState
remains 4,295,312 bytes and the renderer interface 232 bytes. These are layout
and stack observations, not hardware cache-miss or latency measurements.

The native GPU ROM at frame 4700 requests a 48,000-sample triangle report,
then a clear and an empty report. Offsets 0x300100 and 0x300120 retain timestamps,
counts and completion words for both renderer checks. GL GET_REPORT already
flushes closed queries; idle polls add no repeated glFlush calls.

The guarded LRU fixture also flushes one early-bin entry with later bin pages inaccessible. It verifies list/count updates complete before returning; the parent faults on a later-bin read. Existing 30,000 exact order/callback operations include flushes and blocked evictions. BENCH modes 5-8 time refill+flush cycles for one early/late-bin entry and 512/16,384 distributed entries; these are synthetic cycles, not isolated flush or game FPS measurements.

### Completion proofs in Vulkan settle polling

`test-vk-eager-retirement.py CHECKOUT [CC]` also exercises the production settle
poll in 384 schedules: both slot orders, pending masks, uint32 serial rollover,
known completion before/at/after each submission, partially completed GPU work,
and open/closed recordings. Proven slots perform no driver status query but
still drop both command-buffer RAM claims, retire trash and resolve timestamps
exactly once. An unavailable unproved slot remains owned. Global teardown runs
only after all slots retire and no recording is open. A repeated poll cannot
release ownership again. `VK_EAGER_SOURCE` permits a baseline production source
for regression checks. Existing eager/reclaim/full-drain coverage is retained.
No state, buffer copies, PSO variants, waits or locks are added. This reduces
redundant driver calls; no hardware cache-miss or frame-time benefit is claimed.

Win64 settle-poll stack usage remains 80 bytes; PGRAPHState, PGRAPHVkState
and the renderer interface remain 4,295,312, 5,845,344 and 232 bytes respectively.


`test-sse-native-sqrt.py CHECKOUT [CC]` exercises the production scalar and packed sqrt helpers with actual SoftFloat, both with its normal host shortcuts and with those shortcuts disabled in the oracle. Each oracle runs 393,216 result/status/alias cases across XMM/YMM widths, eight rounding policies, guest flushing, sticky flags and exceptional/random inputs. x86-64 cases poison MXCSR rounding, flushing, masks and sticky flags and require exact restoration. Fallback calls are counted per lane. A negative control removes the rounding check from the sticky-state fast path and must fail. Only the four SSE rounding policies use native instructions; other policies retain software behavior.

Seed merge coverage instruments merged-payload allocation: unchanged/duplicate-only merges allocate none, changed merges allocate exactly the final byte count. Another 512 randomized cases compare an independent hash-only/order/cap oracle, including forced collisions, pointer-vector growth, failed writes and poisoned mapping release. Record bytes are copied before unmapping, and no borrowed pointer survives into the file writer. SEED_VIEW_BINARY and SEED_VIEW_SKIP_RUN support native fixtures. The streaming file writer removes the separate whole-file assembly buffer.

### Completion proofs at Vulkan readiness checks

The eager-submit and deferred-flip fixtures each add 1,032 cases covering both
slots, serial rollover, known completion 64 submissions before/after the target,
and unavailable/ready fences. A serial proof bypasses the driver query while
still retiring the slot exactly once. An unproved unavailable fence retains
ownership and backpressure. Eager submission then uses the normal slot rotation;
flip retirement resumes through the existing owner continuation. Both baseline
sources fail the new no-redundant-query cases. `VK_FLIP_SOURCE` and the existing
`VK_EAGER_SOURCE` allow baseline production-source overrides. No state, waits,
locks, copies, PSOs or frame queues are added. Resource-pressure waits inside
partially executed draw setup remain a separate resumable-method task.

Win64 draw-end stack usage remains 96 bytes and flip-readiness stack usage
64 bytes. PGRAPHState/PGRAPHVkState/renderer-interface sizes remain
4,295,312/5,845,344/232 bytes. No hardware cache-miss reduction is established.

`test-fragment-register-keys.py CHECKOUT [--cc CC]` protects existing shader
cache identities across 65,602 register triples. Production assignments must
preserve full SHADERCTL, SHADERPROG and COMBINECTL words; the dynamic combiner
uniform assignment must also preserve the raw guest word. The shadow function
must retain the guest three-bit value even when unused. This deliberately
keeps ignored bits in keys until saved states and disk-cache compatibility can
be handled together. The experimental key canonicalization and masked dirty
check have been rolled back after a user-reported compilation-stutter regression.
Image equivalence alone does not validate shader-cache reuse or frame pacing.

The full-generator ignored-field equivalence and active combiner/texture/shadow
fixtures remain in place. They protect rendering correctness, while the raw-key
fixture separately protects compatibility; equivalent shader source does not
imply interchangeable binary cache keys.

`VK_MEMORY_SOURCE` overrides the allocator source for negative-control runs. The
new simultaneous-image capacity regression fails with the previous allocator.
The fix adds no runtime diagnostic logging or per-frame GPU queries/waits. Normal
reserved-pool reuse remains allocation-free; exceptional image pressure may make
a driver memory allocation rather than crashing at the fixed reservation limit.
The DOA3 character-select crash report identified the render-target allocation
assertion in an 8x build; its exact Vulkan return code was not present in the
screenshot. Saved-state entry can have a smaller live set than cold/menu entry.

`test-seed-stream.py CHECKOUT [CC]` executes the production writer and parser against an independent byte-image oracle and injected stream failures. Its 2,560 cases check duplicate/replaced/unknown sections, reserved bytes, zero-length sections, valid malformed prefixes, size overflow, absent mappings, short writes, open/close/rename failure cleanup, direct mapped spans and unmapping before atomic commit. Linux defaults to ASan/UBSan; SEED_STREAM_CFLAGS, SEED_STREAM_BINARY and SEED_STREAM_SKIP_RUN support native Win32/Win64. Seed parsing checks multiplication against remaining input before forming a byte count.

### Opportunistic Vulkan snapshot-arena recycling

The 75% snapshot-arena threshold no longer submits the current recording or
waits for GPU work. Both arenas may reset only when the existing main-queue
completion serial covers every submitted reader and no current draw has pending
or bound snapshots. Otherwise immutable snapshots append into remaining space.
Actual exhaustion retains its existing synchronous mirror fallback. No storage,
copy pass, PSO, fence query, lock or frame buffering is added.

`test-vk-cow-tail.py CHECKOUT [CC]` adds 160 threshold schedules for both slots,
serial rollover, completed/unproved readers, pending/bound snapshots and
coherent/non-coherent memory. They assert zero finishes/settles at the threshold,
unchanged live prefixes, correct copied bytes and epoch invalidation only when
safe. An unproved full arena still takes exactly one existing fallback. The old
production source fails this regression; 100,000 selector and 10,000 live-reader
cases remain. `VK_COW_SOURCE`, `VK_COW_BINARY`, `VK_COW_SKIP_RUN` and
`VK_COW_CFLAGS` permit baseline checks and native cross-built fixtures.

Mirror consumed-page marks are intentionally retained: the open recording may
still read the mirror even though it references no arena snapshots. Clearing
those marks here would permit an unsafe mirror overwrite. No hardware stall,
cache-miss or total copy-volume improvement has been measured for this change.

Win64 code generation keeps sync_vertex_ram_buffer and pgraph_vk_flush_draw
at 704-byte stack usage, bind_vertex_buffer at 368 bytes, and all renderer
state sizes unchanged. The snapshot helper remains inlined.

### Reusing spare Vulkan snapshot-arena capacity

A snapshot that does not fit the active arena may append into the other
existing arena. Both buffers remain append-only; no live prefix is overwritten
and no arena epoch is reset by this spill. The destination arena's coherence
and non-coherent atom size govern alignment and dirty-range flushing. Cached
snapshots carry that arena's index and epoch, and existing attribute bindings
already select the buffer by index. No new buffer, copy pass, PSO, fence query,
lock or frame buffering is introduced. The blocking mirror fallback remains
when neither safe tail fits.

The COW-tail fixture adds 48 schedules covering both source slots, pending and
bound current-draw references, mixed destination coherence, atom sizes 256-8192,
unaligned tail offsets, live prefix preservation, destination dirty ranges and
cache reuse without another copy. A four-snapshot case fills both arenas before
the fifth takes the fallback. The baseline fails the spill regression. Existing
160 recycle, 100,000 selector, 10,000 live-reader and 80,000 cache-lookup cases
are retained. Native runtime checks do not establish a hardware stall-time or
cache-miss improvement under arena pressure.

Win64 sync_vertex_ram_buffer stack usage rises from 704 to 736 bytes; the
snapshot stays inlined and normal uploads gain no helper call. Outlining the
spare-space arithmetic did not reduce that frame and was rejected. Flush-draw
stack usage stays 704 bytes; renderer state sizes are unchanged. This stack
tradeoff is recorded separately from unmeasured hardware cache/spill costs.

### Tail growth after snapshot-arena spill

Clean growing snapshots now check both arenas' last allocations, active first.
An eligible prefix extends in its existing arena; the copy and dirty range use
that arena's memory properties. Epoch, tail position, bounds and non-coherent
atom-alignment checks remain mandatory. Download/writeback and late CPU dirty
checks still invalidate stale prefixes before extension. No new buffer, copy
pass, synchronization, PSO, lock or frame queue is introduced.

The 48 spill schedules now grow each cached allocation and assert a suffix-only
4 KiB copy when alignment permits, otherwise an independent 8 KiB allocation
that preserves the old prefix. Slot rotation can retain the previous arena's
valid prefix too. Two additional download-mutation cases reject reuse of the
other arena's stale prefix. Live-reader and 100,000 tail-selector cases remain.
The old production implementation fails the new copied-byte regression before
any fixture signature changes. Hardware cache/stall effects remain unmeasured.

Win64 code generation changes the vertex-sync caller frame from 736 to 720
bytes and emits a shared 24-byte tail selector, called at most twice after a
clean snapshot cache miss. Flush-draw remains 704 bytes and renderer state
sizes are unchanged. Caller and nested selector usage are reported separately.

## Disk completion ownership

`test-block-completion.py CHECKOUT [CC]` extracts production submit, poll, cancel and drain functions. It checks callback reentry, next-pass and cross-drive deferral, cancellation with immediate slot reuse, and 100,000 randomized scheduling operations with independent callback/counter accounting. The GPU-wait fixture prevents parking after disk progress in eventcount, descriptor and notifier paths.

`test-disk-completion-bench.py CHECKOUT [CC]` measures source-extracted completion scheduling with generic BH polling retained. Set `DISK_BENCH_SOURCE` to a prior `block/libretro-disk.c` for the old BH path. Reports empty passes and batches of 1, 4, 8 and 24 completions across up to three drives. CPU kicks use production functions and common atomics/eventcount; DSP fences are modeled equally. This isolates scheduling cost, excluding disk I/O and FPS. `DISK_BENCH_SKIP_RUN=1` and `DISK_BENCH_BINARY` support supervised cross-platform runs.

`test-sse-directed.py CHECKOUT [CC]` tests the production scalar/packed add, subtract and multiply helpers against actual SoftFloat for three directed rounding modes. The 2,359,296 cases cover XMM/YMM/wide widths, Boost on/off, flushing and tininess policies, sticky flags, all register aliases, exceptional/boundary/random inputs and hostile host MXCSR settings. Native-lane selection is checked by counting software calls; removing the guest inexact merge must fail a negative control. Safe inputs have exponents [-60,60] for float32 and [-500,500] for float64, or are signed zero: neither products nor cancellation can produce subnormal results or overflow. The round-to-nearest Boost exception-flag limitation remains separate.

### VP voice-state ownership with repeated handles

The parallel VP scheduler now rejects queues containing a repeated voice handle,
even when their mix-bin masks appear independent. Filter, resampler and envelope
state belong to the voice, so two workers must not execute it concurrently.
These queues use the existing ordered serial dispatch directly, without worker
wakes, private mixing or reduction. Unique handles retain normal scheduling.
The guard is a local 256-bit set; no persistent state, mutex, spinlock, sample
copy or audio queue is added. This does not remove the existing completion
barrier for other parallel frames.

`test-vp-threads.py CHECKOUT [CC] [SCENARIOS]` now rejects 256 repeated-handle
queues across every ownership-bit boundary and accepts all 256 unique handles.
One quarter of the randomized scenarios share a list tail, comparing real voice
processing on one/four workers against the same serial result. Existing HRTF,
filters, envelopes, pitch, LFOs, multipass and monitor coverage remains.
`VP_THREADS_SOURCE` supports a baseline production-source override.

`VP_THREADS_BINARY`/`VP_THREADS_SKIP_RUN` support native cross-built fixtures.
The `VP_REPEAT` NASM test-ROM define shares the looping PCM voice between the
2D and 3D lists; compare strict one/four-worker audio hashes and lifecycle runs.
Win64 scheduling stack usage changes from 3,248 to 3,264 bytes; voice_process
remains 1,248 bytes and the VP frame 160 bytes. Dispatch/filter state sizes
remain 3,120/1,120 bytes. The guard adds no per-voice processing fields.

## Guest display gamma

`test-display-gamma.py CHECKOUT --cc CC` executes the production DAC writer, atomic snapshot and ramp cache against 1,024 sequences, including identity/repeated writes, concurrent-write retry, palette wrapping and version wrapping. `--gpu --output DIR` additionally compiles the actual GL and Vulkan native/byte presentation shaders and checks 90 GL and 180 Vulkan cases: independent channel curves, identity bypass, alpha, video-overlay color-key ordering and RGBA8/RGB565/1555 inputs. Sixteen-bit curves are checked against each driver's neutral conversion; RGBA8 uses an independent pixel oracle. Vulkan checks the 128-byte push ABI, 1,024-byte lookup layout and eight aligned uniform snapshots. Synchronization validation is enabled when available, and generated modules are checked with `spirv-val` when installed. On Windows, provide `--epoxy-root DIR` containing `libepoxy/include` and `libepoxy.a`.

The runtime reuses the presentation draw. Vulkan reserves and maps eight small lookup snapshots at startup, with immutable descriptor bindings; OpenGL uses packed uniforms. Unchanged palettes avoid uploads. Palette-only changes invalidate cached presentation; the display-slot regression also checks gamma-only redraw/retry behavior.
