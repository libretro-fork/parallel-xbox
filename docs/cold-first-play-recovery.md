# Cold-first-play recovery and regression prevention

## Pending fragment-corpus scheduling, 2026-11-16

The scalar checkpoint's public CI still times out on the full positive fragment
corpus after 300 seconds, and the software vertex batch 7168:14096 has a separate
300-second timeout. Compiler comparisons can remain inconclusive; these jobs
are not declared green. A private combined-register-publication experiment
passes the native numerical corpus but also times out with a new, initially
empty Mesa cache after reaching roughly case 1668. Its first prepared draw is
about 0.589 seconds, versus 0.594 seconds in a fresh-cache parent prefix probe.
The prefix contains four cases and the independent oracles, not the full
corpus. This experiment does not establish recovery and is not integrated.

The test runner now partitions all 1,960 positive cases into contiguous ranges
across at most four independent GL contexts. One 300-second watchdog starts
before any child is launched and covers the entire positive corpus. Each
child retains both invariant native fragments and its own textures, buffers,
pipeline, readbacks and teardown. The aggregate requires exact, non-overlapping
range boundaries, case counts, all 627,200 color/depth checks and positive
written/discarded counts. It cannot accept an incomplete child's manifest as
a full-corpus pass. The first range retains the complete independent constant,
shadow and half-plane/window/volume oracles once, with their existing probes.
All 23 deliberate GPU fault controls keep their original serial execution and
300-second limits. No shader generator, runtime renderer, corpus case, image
comparison or worker limit changes.

A required CPU-only runner fixture executes before the GL tests. Synthetic
child manifests test one, two, four and capped contexts; nine required bad
controls cover wrong check counts, ranges and case counts, missing oracles,
duplicate summaries, missing live pixels, child failure, watchdog expiry and
partial launch failure. Every started child must be retired. These are
metadata/process tests, never a substitute for actual GPU execution.

The native positive corpus passes with four contexts, and all 23 serial GPU
fault controls pass.
The metadata controls pass on Windows and Linux. A separate Linux run starts
with an empty private Mesa cache and CPU affinity restricted to two CPUs; its
two positive ranges cover 0:980 and 980:1960, each with 313,600 exact checks,
and all independent oracles pass inside the unchanged shared deadline. Its
remaining serial fault controls are pending. The private cache directory
avoids changing user caches. This establishes numerical test execution only;
it does not establish shader-API or gameplay latency improvement.

Further fixed-function diagnostics reject a final diffuse FMA rule which fixes
case 28740 but introduces twelve other differences. A structural ambient-source
predicate also fails: a complete four-worker lighting-cohort comparison over 24080:36368 has
eleven parent differences and thirteen candidate differences. Earlier diagnostic
runs stopped other workers on their first failure, so their smaller counts
were incomplete. These math interventions remain private and unintegrated.
Cold-first-play recovery, complete ready coverage, literal and fixed-function
precision, Vulkan first use and gameplay allocations remain open.

## Pending named fragment registers, 2026-11-16

The local disabled-by-default prepared fragment generator now emits sixteen
named vec4 registers instead of a constant-index array. Dynamic selection still
uses the existing getter and predicated component writes. Immutable helper
spans append directly into the final source owner; there is no second source
payload or new runtime resource. Ordinary GL/Vulkan source remains byte-identical
across 73,728 cases / 614,644,992 bytes. The 256 prepared-source owner schedules
and five deliberate ownership failures still pass.

The complete positive GPU corpus passes on NVIDIA and llvmpipe: 1,960 cases /
627,200 exact color/depth checks, plus the independent constant, kill, window,
volume and 1,544-probe shadow oracles. Native fragment orchestration retains
29 startup failure points and eleven required fault controls. All 23 GPU fault
controls pass on both drivers, including a new named-register alias mutation.
The full corpus, per-process 300-second limit and worker cap are unchanged.
The existing literal-combiner witnesses 948, 988 and 1008 produce the same
one-bit differences in both parent and candidate. They remain failures; this
rewrite does not solve their contraction differences.

Private startup profiling attributes the long first-use delay to GL_LINK_STATUS,
not shader-source assembly, shader deletion or native-pipeline validation.
Fresh-identifier paired diagnostics report array link waits of 622.657, 611.994
and 602.972 ms versus named registers at 584.177, 577.617 and 576.757 ms.
The repeat gain is roughly five percent, superseding the earlier apparent
halving from unmatched cache histories. Private driver cache state remains
uncontrolled; these are API-phase diagnostics, not cache-cold recovery.

Matched GPU execution in one native context uses twelve alternating batches,
four warm draws, sixteen measured draws and 1,048,576 pixels. Parent/candidate
median microseconds for zero/one/four/eight 2D stages are
17.602/17.548, 23.928/23.916, 53.172/53.166 and 94.674/94.620;
four-stage volume and cube results are 53.306/53.306 and 53.362/53.358.
This simple warmed fixture shows essentially unchanged GPU cost. It does not
measure a retail workload, complete frame latency or a cold first encounter.

Experimental current-main builds use libretro base 27d3a4628f and the same
shadow/alpha/bump checkpoint, coherently rebuilding all GL objects. Only the
fragment-generator object differs; all 543 other objects match. NGB and DOA3
arena images match byte-for-byte after two external restores per process,
with correct audio/video cadence. These correctness runs overlap other
correctness checks and are not latency evidence. The build root's default
source, objects and DLL are restored byte-for-byte after private builds.

Two attempted gameplay timing comparisons are invalid: the first overlapped
a recording test at launch, and the second was stopped by a private process
monitor when unrelated builds started during its candidate run. They are
retained as failed qualification attempts. There is no gameplay-speedup claim
for this rewrite and no change to the timing or correctness gates.

First startup still exceeds half a second in the diagnostic. Cold-first-play
recovery remains BAD/unqualified, prepared rendering stays off by default,
and the broader fixed-function, literal precision, Vulkan readiness and
allocation work remains open. This checkpoint is not promoted to libretro.

## Pending fixed-depth shadow coverage, 2026-11-16

The local prepared GL fragment prototype adds shadow-map encoding 1 for
PROJECT2D/PROJECT3D, matching the four observed shadow states in the DOA3
first-fight inventory. Existing per-stage control bits select manual depth
comparison; the same two native fragment owners and existing 2D samplers are
used. No shadow-sampler alias, native program, host payload buffer, worker,
reservation, seed layout or ordinary generated source is added. Stage texture
owners remain live through their draw. Encoded F16/filter variants, unsupported
dimensions/cube routes and invalid comparison values stay correctly uncovered.

The shader preserves the ordinary generator's early NEVER/ALWAYS behavior,
rectangle coordinate normalization, no shadow border adjustment, comparison
against zero in PROJECT2D and against clamped projected z in PROJECT3D. Depth
RGBA metadata selects D24 first, otherwise float depth or D16. A CPU oracle
checks 1,544 comparison/coordinate probes and 12,352 exact channels, including
all eight comparisons, four stages, both projection modes, three depth ranges,
marker priority and a two-texel rectangle selection test. The ordinary/ready
comparison retains all 1,704 cases and adds 256 shadow states: 627,200 exact
color/depth values and two invariant ready sources. Six added invalid domains
reject before touching controls. Native and software positive tests and all twenty-two required fault controls
pass within the unchanged per-process 300-second limit.

Ordinary source/ownership checks retain all 73,728 byte-identical shaders
(614,644,992 bytes), 256 prepared-owner schedules and all five owner faults.
Native fragment orchestration retains its 29 startup failures and eleven
fault controls. There is no extra per-bind source owner or payload copy. These
counts do not prove that the driver performs no internal allocations.

Matched experimental builds on current libretro base 27d3a4628f coherently
rebuild every GL object; all 543 non-fragment objects match between parent and
candidate. Both use the same alpha-rounding/bump checkpoint and exclude the
unqualified fixed-function prototype. Only glsl/psh.o changes. DOA3 arena and
NGB images match byte-for-byte after two fresh-process restores, with correct
audio/video cadence. A separate fresh DOA3 first-fight diagnostic reports 2,598
prepared-shadow uploads, four ready module lookups and zero shadow fallback
lookups, versus zero prepared lookups/uploads and six fallback lookups in the
parent. Lookups are not compilation API counts; no six-compilation or timing
improvement is claimed. Diagnostics are private atomic counters, restored out
of all release objects and source after collection.

Readbacks, concurrent software checks and uncontrolled driver caches leave
those diagnostic/image timings unqualified. Fresh-boot camera positions are
not paired, so throughput alone cannot prove equal scene work. An isolated ABBA run preserves 599 guest presents/completions and 600 fight
callbacks per run without a 16.683 ms fight deadline miss. Parent guest rates
are 113.340/117.895 FPS versus 119.455/119.351 for the candidate; fight p99
values are 12.011/10.247 ms versus 10.072/11.067 ms and fight maxima
13.179/10.819 ms versus 11.907/12.786 ms. Scene variation and timing spread
prevent a speedup claim. The first candidate's frame zero costs 1,216.359 ms,
versus 93.064 ms on its repeat and 93.106/93.030 ms in the parent. This spike is
retained as an open startup qualification failure; private driver-cache state
is uncontrolled and shader-API timing attribution is still needed. Full
cold/warm qualification and remaining fixed-function/fragment precision
coverage are open. This is local experimental work; default rendering remains
unchanged and shader-stutter recovery remains BAD/unqualified.

## Pending dependent bump-texture coverage, 2026-11-16

The local experimental fragment prototype now covers unsigned bump environment
and luminance stages 1..3 with a validated earlier-stage source. It borrows two
previously unused bits in each existing per-stage control word; shader/seed
layouts and ordinary generated source stay unchanged. Existing bump matrix,
scale/offset uniforms and typed 2D/3D sampler aliases supply the operation. No
native program, host payload buffer or per-bind allocation is added. Native
texture owners still remain live through the consuming draw. Signed-source,
shadow, cube and forward/self-input cases remain correctly uncovered.

A private production-source probe retained all 1,416 fragment cases and added
288 dependent matrix/source/dimension/rectangle/luminance cases: 545,280 exact
color/depth checks on NVIDIA and llvmpipe, two invariant prepared sources, and
the existing independent constant-register and kill/window/volume oracles.
Four required faults (matrix, signed-byte decode, luminance and source selector)
fail in the added cases on both devices. Five added invalid-domain checks reject
atomically on both devices. Early private logs inherited an incorrect 1,416-case
label; the actual 0..1703 loop and 545,280 comparisons prove the expanded scope.
The fixture label is corrected. The integrated fixture passes all 1,704 cases
and all seventeen required controls on NVIDIA and llvmpipe under the existing
per-process deadline. The source-owner fixture retains 73,728 byte-identical
ordinary shaders (614,644,992 bytes), 256 prepared-owner lifetimes and five
mandatory faults. Native-owner orchestration retains its 29 startup-failure
schedules and eleven fault controls.

Matched experimental binaries share the private 0863fb0c1 build base, alpha
rounding checkpoint and all 543 non-fragment objects; only `glsl/psh.o` changes.
They exclude the numerically failing fixed-function prototype and contain no
release diagnostics. NGB and DOA3 arena images match byte-for-byte after two
fresh-process restores, with correct audio/video cadence. Fresh-boot DOA3 images
have different camera positions and do not establish equivalence; a copied
legacy-list assertion from the newer cache fix is also inapplicable to that
older build base. Its failure is retained, not relabelled a pass. These are
correctness checks, not timing: readbacks and concurrent software validation
leave latency unqualified. A separate diagnostic first-fight run records zero bump-ready uploads,
ready lookups and fallback lookups on both binaries: these stages are not used
in this DOA3 sample, so no retail compilation reduction is claimed. The mapped
seed inventory contains 81 distinct states, 49 with fixed-function vertices,
four with shadow textures, and only texture modes 0..3. These are unique-state
counts, not draw frequencies. Fixed-function and shadow coverage are therefore
more relevant to this observed sample. Current-main integration, other retail
coverage counts, matched cold/warm timing and cold-play recovery remain pending. This prototype
is local and unshipped; prepared rendering remains disabled by default.

Separate arithmetic witnesses reject blanket FMA: it matches literal cases 948
and 1008 but breaks forced-dynamic case 948 by one bit. Merely spelling the
products together does not fix the literal mismatch. These witnesses remain
failed evidence; no arithmetic tolerance or reference is relaxed. Existing
literal 948/988/1008 and fixed-function numerical gaps remain open.

## Prepared-fragment qualification update, 2026-11-16

The experimental path remains disabled by default. Legal non-dyadic constant
values expose precision differences hidden by the original fixture's uniform
0.5 constants. A private 1,308-state probe on NVIDIA finds four mismatching
states in the committed return-writer source: 922, 948, 988 and 1008. These are
exact float-output differences, not proof of a visible retail-game defect.

The alpha-product shortcut contracts multiplication with output bias in case
922; its dynamic reference rounds the product first. An alpha-only `precise`
intermediate fixes that witness. The expanded maintained fixture retains every
original case and adds 108 dynamic rich-combiner cases with byte/255 constants.
It rejects the committed source at new case 1318 and passes the candidate on
NVIDIA with all 13 fault controls. A separate independent identity oracle checks
72 shared/unique/final C0/C1 selections against the uploaded array, with 576
exact channel checks and three deliberate indexing faults.

This does not resolve the three literal-combiner witnesses. A broader private
attempt to force precise RGB and RGBA products also changes software-GPU dot
rounding (forced-dynamic case 929) and is rejected. The alpha-only candidate
passes all 1,416 cases / 453,120 exact outputs on NVIDIA and llvmpipe under the
existing 300-second process watchdog. Both devices pass the independent slot
oracle; NVIDIA passes all 13 fault controls, and llvmpipe additionally rejects
all three new slot faults. The prior independent-if software run retains its
ten original fault-control results. ASan/UBSan verifies all 73,728 legacy
sources / 614,644,992 bytes against the pinned source and all five lifetime
faults, including 256 prepared builds with one final owner each.

The independent-if writer retains every constant-index RGB/alpha/RGBA
destination and never writes register zero. Matched experimental release DLLs
differ in only the PSH object; all other 543 objects, including refreshed build
metadata, match. Four isolated fresh-process DOA3 runs advance 599 guest
Presents/completions per 600-live-callback window, with about 9.993 seconds of
guest time and no fight callback over 16.683 ms. Parent fight p99 is
10.430/11.887 ms versus candidate 10.632/10.527 ms. This does not establish a
speedup. No test readback allocation occurs in those gameplay runs.

The first candidate startup still reaches 1,077.832 ms, versus 92.665 ms on
its repeat. Driver caches are uncontrolled, so this is not a matched
cold-driver comparison and does not qualify recovery. Neither the arithmetic
fix nor the independent-if writer establishes full ready coverage or
cold-first-play recovery. The earlier experimental checkpoint is present on
libretro, disabled by default; this follow-up remains local pending delivery
qualification.

Status, 2026-11-08: **BAD / REGRESSED / NOT QUALIFIED**. DOA3's first fights
have reported obvious shader/PSO hitches. This is the defect to recover from,
not an acceptable new baseline. The separately fixed CPU carry regression
and its copyright-screen throughput result do not establish shader recovery.

## Acceptance target

Starting and playing for the first time without a pre-existing compiled
shader/pipeline cache should be nearly imperceptibly different from fully
warmed play. No obvious compilation pauses, missing draws, incomplete frames,
artificial guest slowdown or audio disruption. Faster averages do not excuse
a worse first encounter. Existing caches remain supported; do not change
defaults or rely on deleting/rebuilding caches to conceal the regression.

This is an engineering target with measurable release criteria, not a promise
that an arbitrary driver will never take time to compile an unknown program.
An uncovered synchronous path stays correct and is reported as unfinished.

## What the supplied documents establish

The October 30 Xbox shader/PSO handoff is the primary architecture map, pinned
to 6bf967489482. Revalidate its details against current source. It distinguishes
generated GLSL, SPIR-V, live modules, complete pipeline recipes and ready PSOs;
it explicitly records remaining synchronous misses. Full-pipeline ahead work
can run without graphics pipeline libraries. The user's `no_gpl` configuration
must be a first-class acceptance configuration, not worked around by forcing GPL.

The November 8 Xbox 360 handover is evidence for measurement discipline,
native emitters, numerical negative controls, immutable publication and
explicit borrowed lifetimes. Its CPU translation gains and static lane counts
are not measurements of Xbox NV2A pipeline latency or smooth retail gameplay.
Do not transplant Xenos/DXIL-specific choices into NV2A without a local audit.

Use the supplied speed-of-light essay as a work-removal principle: eliminate
unnecessary layers, scans, allocations and payload movement. Its attributed
quotes and theoretical examples are not a measured performance baseline.

The general implementation plan is a proposal, not implemented fact or an
instruction to execute blindly. In particular:

- Static XBE/XEX scanning cannot be assumed to discover every dynamic shader
  or complete PSO state. Treat it as optional supplemental discovery later.
- A universal compute interpreter is not automatically a correct graphics
  fallback. Expand and prove the existing dynamic graphics siblings first.
- Complete CPU pipeline creation through the worker's existing release/acquire
  protocol; do not invent Vulkan GPU fences for host compiler completion.
- MDI, external-host-memory import and large fixed reservations are separate
  measured projects. They do not establish correct tiling/coherence, eliminate
  PSO compilation, or guarantee allocation/stutter immunity.

## Recovery sequence

### 1. Freeze evidence and a genuine acceptance reference

The repeat-run corpus also includes **JSRF - Jet Set Radio Future (USA)**
(title ID 5345000A). Run both OpenGL and Vulkan with fresh and populated core
caches, preserving driver-cache qualification separately. The current
reproducible smoke sequence is 6,000 frames from a fresh boot with no inputs,
at native internal resolution; its endpoint is a 3D attract scene. Keep a
separate scene capture to verify that endpoint, then collect repeat counts
and timings without readbacks. Include save/two exact RAM restores/reset as
a separate lifecycle case. Report this as boot/attract coverage, never as
interactive gameplay or a substitute for DOA3 first-fight qualification.
Include **Crazy Taxi 3 - High Roller (USA)** in the same repeat matrix on
both renderers. Verify its 3D attract intro with a separate capture before
qualifying the chosen no-input frame window; log boot/attract and lifecycle
coverage separately from interactive gameplay. Keep each game's caches and
capture artifacts separate, and repeat parent/candidate cold and warm runs
with matching per-game settings.

Include **Ninja Gaiden Black (USA) (En,Ja), savestate slot 0** in the
repeat corpus on both OpenGL and Vulkan. Slot 0 is RetroArch's unnumbered
`.state` file, not `.state1`. Record its source hash and originating build
when available, and keep the private fixture outside the repository. Test
restoration in a fresh process with empty core caches, repeat with populated
core caches, and check a separate matched parent/candidate scene capture.
These are loaded-state/cache-readiness cases, not cold-boot qualification.
Repeat restoration before continuing the scene and exercise lifecycle
transitions separately. Keep driver-cache qualification explicit.

Compressed fixtures require bounded decoding. Borrow a validated core-state
block from a read-only mapping whose owner survives the last unserialize;
do not clone the whole decoded state into another payload buffer. Match the
harness's expected audio phase to the serialized `libretro-frame` phase at
each external restore; retain exact per-run audio checks. A missing RAM
sidecar means RAM equivalence is unqualified, not an automatic pass.

Record image/build hashes and matching options for parent/candidate repeats;
no private game image is distributed with CI or tests.

Verify checkpoint contents instead of trusting filenames. A DOA3 fixture
named "first fight" was found to restore the title screen; retain those
attempts as unqualified for fight coverage. A replacement checkpoint must
record the live-fight marker (state 1, scene 10, phase 1), a matching scene
capture and its source hash before use. Keep mismatched cold-boot images
separate from matched loaded-state images. Check the actual renderer and
the existence of the requested capture; a successful restore or harness
exit does not establish that a scene was captured on the requested backend.

Preserve the current bad binary, startup commit, options, driver/GPU/OS,
harness hash, cache manifests and failing frame IDs before changing code.
Identify and replay a last-known-good cold-play build; the November 6 build
used for the carry bisect is only a throughput reference until cold gameplay
is verified. Pin its full SHA in `SHADER_ACCEPTED_BASELINE` only after that
verification. Never automatically advance this variable to the parent or tip.

Start with an actual fresh boot through DOA3's first fights and second-stage
transition, using a repeatable input recording. Keep the copyright screen as
a separate CPU-throughput control. Add DOA3's industrial arena, CT3 first route,
NGB first combat/door, JSRF first area, Otogi first combat and a cutout-heavy
DOAXVB transition. Revalidate historical fixture manifests and executable/state
compatibility. A filename saying "first fight" is not evidence the checkpoint
is gameplay; inspect it separately. Loaded states supplement fresh boot and
never replace it.

The public corpus should also grow synthetic NV2A FIFO/state traces covering
combiners/alpha, blend/depth/stencil, topology, vertex formats, texgen,
attachments and utility conversions without requiring retail assets. Existing
key/worker and clear/geometry fixtures cover parts of this space; they are not
yet a complete synthetic first-play renderer trace suite.

Deliverable: versioned fixture metadata, input sequences, expected progression
and private asset hashes; no retail assets in GitHub artifacts.

### 2. Separate cache conditions and measure the first encounter

For each baseline/candidate pair, restore the same private system/HDD baseline
and use a new private cache directory for each cold repeat. Record:

| Condition | What is present | Purpose |
| --- | --- | --- |
| Fresh install / no compiled cache | No learned local states or application compiled cache; disclose bundled recipes | Primary first-boot acceptance |
| Strict discovery diagnostic | No learned or bundled recipes, where supported | Reveal uncovered first-use work; do not silently label seeded runs all-cold |
| Seeds only | Identical recipes, no application driver blob | Separate preparation coverage from driver compilation |
| Warm persistent cache | Same seeds and compatible compiled caches | Compare cold/warm user experience |
| Warm process | Existing live modules/PSOs | Separate live-cache hits from reload/build cost |

Application cache absence does not prove a cold vendor driver cache. Record
driver-cache controls and availability; do not claim it cold when unverified.
Use isolated driver-cache locations/controls where supported. Never delete
the live RetroArch system/cache or mutate user saves for a benchmark.

Run unthrottled diagnosis and frontend-paced perceptual checks separately,
at native and the user's 8x scale, with fixed CPU/APU settings. Include no_gpl
and capability-supported GPL as separate configurations, plus GL where affected.
Use ABBA and reversed-order repeats on idle hardware. Do not discard boot,
first draw, first use or transition spikes as "warmup" in cold-play results.

Record presented-frame and callback p50/p95/p99/p99.9/max, worst-frame IDs,
long frames above 1.5x and 2x the refresh interval, deadline misses, audio
cadence and game progress. Keep traces for the worst few events even when
they are too rare to move p99. The existing 8% throughput threshold is not a
shader-stutter allowance. Public-runner CPU limits are coarse alarms; a
reproducible visible hitch still fails physical-gameplay acceptance.

### 3. Attribute blocking work before changing architecture

Use opt-in diagnostic builds to time GLSL generation, SPIR-V translation,
module/layout adoption, full PSO creation, GPL halves/linking, upload-time
prewarm, synchronous misses and cache serialization/I/O independently.
Correlate each event with frame ID, exact complete key/family, cache state,
worker phase, queue occupancy, duplicate jobs and fallback readiness.

Classify misses: absent sibling, absent sibling PSO for the current render
pass/static state, missing words, late module adoption, duplicate work,
queue saturation, driver build/link, allocation/retirement or cache I/O.
Do not infer a shader stall from its appearance or GPU utilization alone.

Remove diagnostics and rebuild objects before clean timing. No image hashing,
readback, synchronous GPU query polling, competing builds or per-draw release
logging. Require zero readback allocations in timing logs. Separate GPU
execution, CPU submission gaps and frontend display pacing.

Deliverable: a ranked stall ledger with count, total time, p99/max, frame IDs,
thread/owner, root cause, removal target and an independently verified replay.

### 4. Repair the correct fallback and complete-PSO path

Prioritize the cause demonstrated by the ledger. Verify the full state machine
from unseen through queued words, adopted words, queued PSO and landed PSO.
Having words ready must not prematurely remove a usable fallback or force a
synchronous specialization before its matching pipeline lands.

Cover the actual attachment/render-pass, vertex-layout, alpha/combiner and
dynamic/static state families. Reuse a correct ready sibling while immutable
specialization jobs run; a full ring defers optional work rather than making
the draw wait or spin. Preserve exact-key comparisons, padding, cancellation,
duplicate-result destruction, generations and submission retirement.

For no_gpl, evaluate bounded observed complete-PSO recipes and earlier worker
preparation after proving missing static-state coverage. Recipes are portable
state metadata, not native handles. They supplement first-use correctness;
success after a learned second run is not success for unlearned first play.
For GL, distinguish CPU source preparation from context-affine compile/link
and avoid premature completion/location queries that serialize the driver.

Only then optimize measured emitter/parser, repeated compilation, key scans,
layout work or allocations. Direct SPIR-V emission is useful where measured;
raw word count alone does not prove lower driver/GPU cost. No arbitrary worker
or memory-cap increase, render-pass removal or precision downgrade as a fix.

Track the zero post-boot draw-critical create/compile/wait targets by shader,
PSO and utility kind. Preserve a correct fallback for each family. Ratchet
verified per-corpus budgets downward as work/time improves; a deliberate
increase requires a separate explicit user-approved exception. Static counts
are work evidence and must not be mistaken for driver/GPU execution time.

### 5. Apply the same ownership and I/O rule throughout

For every change keep a ledger: removed payload copies/slurps, bytes scanned,
allocations, cache reads/writes, mapping lifetime, queue ownership and final
CPU/GPU consumer. Prefer existing VFS mapping/validated bounded views, direct
final-destination reads, and immutable reference transfer. No second full
file buffer, shader-word clone, shadow copy of a driver cache blob or
serialization work on the gameplay critical path.

Asynchronous consumers retain the mapping/storage owner until completion;
synchronous parser/API lends end only after return. Validate truncated input,
offset/length overflow, mapping failure, cancellation, eviction, hot reload,
reset/load, device change and teardown. Stable job slots own complete recipes
and correct interior pointers. Borrowed resource handles need native GPU
retirement, not merely CPU-job completion. Preserve small owned constants or
bounded snapshots when mutation/lifetime makes borrowing unsafe, and count
and justify them explicitly. "Zero-copy" never means removing ownership.

The CPU CI benchmark itself maps NUL-terminated scratch GLSL directly. Its
preparation appends a sentinel in place without cloning the payload. The
compiler owns its generated words; validation writes that final owned span
directly outside the timing interval. No new runtime instrumentation is added.

### 6. Keep every commit comparable in GitHub CI

Implemented in `.github/workflows/shader-regressions.yml`:

1. Enumerate every new commit in an ordinary push, not just the final tip.
   PR/manual/new-branch runs check their head. Pushes over 32 new commits fail
   explicitly and must be split. Nothing is silently sampled away.
2. Execute production-function PSO keys, stable queued jobs, module lifetime,
   worker drain/backpressure, cache I/O publication, layout and prefix-view
   tests, including negative controls.
3. Build baseline and candidate `seedshaders`; compile their complete bundled
   dictionaries and validate all exported SPIR-V 1.6 modules.
4. Benchmark both actual compilers on the identical union-derived corpus:
   largest plus hash-ranked vertex/geometry/fragment modules, both SPIR-V
   1.5/1.6, fresh compiler results, serial ABBA repeats. Keep hashes, per-module
   samples, distribution tails, stage totals and large isolated regressions.
   A deliberately delayed compiler must fail the gate. Baseline API/corpus
   incompatibility and noisy results are unqualified, not skipped successes.
5. Use software Vulkan to create real test PSOs and run numerical geometry,
   clear and local-initialization checks. This catches API/semantic errors;
   lavapipe timing is not hardware PSO or gameplay performance.
   Utility and vertex tests have separate jobs. Vertex workers interleave
   disjoint 16-case chunks across the existing four slots, rotating owners
   each round so low-bit selectors do not pin expensive shader families to
   one slot. The original batch deadline is not renewed. Eight fragment jobs
   partition
   the original 160 batches, covering all 82,704 cases exactly once, with one
   original batch at a time in each job. Up to four existing worker slots
   execute disjoint subranges under that batch's one shared 300-second
   watchdog, including spawning and coverage verification. Every child retains
   all numerical comparisons and validation checks. The original C coverage
   assertions run on their summed counters for the original batch, with
   negative controls for every required signal. No child receives a fresh
   300-second allowance. Each fragment artifact includes its exact batch
   manifest; the contracts check rejects gaps, overlaps, lost numerical
   controls and invalid shard/worker settings. A revision/reference expansion
   over GitHub's 256-job matrix limit fails explicitly and requires splitting
   the push. The required gate still depends on every software job.
6. Retain versioned logs, CSV, manifests and JSON for every revision/reference.
   Compare to both parent and the separately pinned accepted SHA when configured.

Public CPU timing initially flags >5% repeatable aggregate/stage slowdown,
>5% run spread as inconclusive, and >10% plus >50 us median isolated-module
growth. These are hosted-runner alarms, not permission to accept smaller
measurable regressions on stable reference hardware or any visible hitch.
Do not loosen them silently. Compilation metrics exclude source generation,
Vulkan module/PSO creation and GPU execution, which require separate gates.

Administrator setup still required, not claimed complete:

- Set `SHADER_ACCEPTED_BASELINE` to a full verified SHA. Missing configuration
  is prominently marked unqualified; the regressed parent is not an accepted
  smoothness reference.
- Make the fixed-name `Shader and PSO required gate` required in repository
  branch protection or rulesets. It fails if any revision/reference job fails,
  is cancelled or is skipped. Creating YAML does not itself change repository
  protection; this status is not physical cold-play qualification.
- Provision a trusted `self-hosted, shader-reference` runner, private fixture
  service and `SHADER_HARDWARE_QUALIFIER` executable path. Then set
  `SHADER_HARDWARE_ENABLED=true`. Never execute private assets or untrusted PR
  code automatically on that machine.
- The qualifier receives all revision/reference pairs, the accepted SHA and
  the explicitly configured `SHADER_CPU_AFFINITY_JSON` logical CPU set. Record
  observed start/end process affinity in schema-2 callback manifests; missing
  or changed affinity is unqualified, not guessed from the host identity.
  It must implement the cache/scene matrix above, return nonzero for a new
  hitch, invalid progression, absent fixtures or inconclusive run, and put
  only sanitized metadata/timings in `public-report/`. Its implementation,
  fixtures and physical GPU runs are remaining recovery work, not a shipped
  guarantee from this workflow hook.
- After the service, the workflow independently audits raw callback evidence
  with `gate-cold-play-frames.py`, including when the service fails. The
  [callback evidence contract](cold-play-callback-evidence.md) defines the
  bounded JSONL/CSV format, complete comparisons and rejection controls.
  Missing artifacts, a hitched reference or inconclusive timings cannot
  become a green result through the service's exit code alone. This added
  callback gate is not the full GPU/presentation/ownership qualifier.

Until hardware qualification exists and recovery passes, CPU/software-GPU
green checks do not authorize describing cold play as fixed. Documentation
and CI changes leave the existing runtime regression explicitly open.

## Completion evidence

### Handoff, key and lifetime repairs

In-flight full PSOs remain queued, preserving the correct dynamic sibling
until render-thread adoption. A ready cache probe requires an exact complete
key and a non-null pipeline; in-flight deduplication compares complete keys
too. Completed demand PSOs are drained before GPL/dynamic-state/attachment
warmup guards. Only a started worker with pending jobs is polled.

Stencil write masks are dynamic with the extended dynamic-state group, so
`STENCIL_WRITE_ENABLE` no longer splits that in-memory PSO key. Static-device
keys retain the bit. Compatibility decision: no serialized format bump;
`PipelineKey` is an ephemeral host lookup key. Its layout, `ShaderState`,
module keys, generated shader code and driver-cache validation are unchanged.

Disabling seeding stops new seed submissions without skipping outstanding
native ownership retirement. The bank and one-shot flag are reset after
worker quiescence on renderer finalization. Retired pump progress is cleared
before its borrowed bank base is released. Lookups borrow existing immutable
keys only during the call. No new geometry/SPIR-V payload copies, threads,
per-draw clocks or persistent guest pointers are introduced.

Production-function tests cover 40,960 ahead-key cases, 1,024 completion
service configurations, 200,000 register-key projections against a frozen
reference, and repeated renderer generations/option transitions. Required
negative controls reject premature readiness, hash collisions, null handles,
GPL-only completion servicing, stale seed flags and early bank release.

The broader complete-PSO seed warmer is held in a separate local experiment,
not part of these repairs. Its first state-verified comparison failed warm
play qualification (about 13% worse means and new deadline misses). Preserve
that evidence. CPU cache-domain affinity is an additional controlled repeat,
not permission to erase failures or claim recovery from warmed driver data.

Earlier fixed-input private tests omitted creation of fresh system folders;
their "warm" labels were invalid because writes did not persist. Keep their
blocking-API traces, but do not use them as cache comparisons. The corrected
private runner verifies persistence and 600 live-round callbacks through
bounded read-only test probes, with image checks separate from timing.

The narrow repair's controlled repeat used two ABBA blocks, each with four
fresh boots and their persistent-cache repeats (16 processes, 9,600 verified
live callbacks). Native-cache-empty runs had 130 learned descriptors; they
were not descriptor-empty. On Ryzen 9 9950X3D/RTX 5090, Vulkan/no-GPL/8x, all
live callbacks stayed below 16.683 ms. Cold live means changed -0.89%; warm
means +0.035%, within the comparison's noise allowance. Worst live callbacks
were 12.620/13.460 ms baseline cold/warm and 12.094/13.900 ms candidate.
Process affinity was confined to the 96 MiB L3 domain for both revisions;
no system power/clock settings changed. Driver cache state was uncontrolled.
These are host callback measurements, not presented-frame latency or proof
that uncovered first-use driver compilation is fixed.

A separate two-block ABBA comparison started eight processes with neither
learned descriptors nor a native pipeline cache. It verified another 4,800
live callbacks, with no 16.683 ms misses in either revision. Means were
indistinguishable (-0.13% candidate); worst live callbacks were 13.316 ms
baseline and 12.473 ms candidate. The bundled dictionary has no DOA3 entry.
The same driver-cache caveat applies. This qualifies the narrow repair for
this first-fight workload, not all scenes, utility PSOs or cold-driver states.

Separate first-fight captures were byte-identical at 5760x3840. A lifecycle
smoke saved and loaded twice (zero RAM differences), closed/reloaded content
and recreated the shared Vulkan device, then reset and resumed live play.
Audio totals matched with zero cadence errors. That smoke is not a timing
comparison; the release build excludes its passive guest-state probe.

The subsequent general-build fixture repair changes no runtime source:
reuse the byte-identical `d51d97ceb4` runtime evidence above. Its frozen
legacy x86 boundary comparison still exercises 1,048,576 cases and three
negative controls. Guest-wake tests use the production borrowed PRAMIN
accessor. Texture fixtures cover absent/present surfaces and pending uploads
regardless of reinterpretation, and reject the former byte-alias-only rule.
This does not promote the runtime or its driver-cache state to a smooth LKG.
The IDE fixture follow-up likewise changes no runtime: extract complete
buffered read functions and their production queue limit instead of pulling
in adjacent unrelated DMA-reset code. Preserve byte/gap/error/orphan/reentry,
zero steady-state allocations and balanced owner-release checks. The separate
cancellation, restart, trim and write-generation checks also pass locally.
The existing blend oracle now reflects the same proven dynamic stencil bit
projection (73,728 register/format/feature cases). Recording-epoch fixtures
model the existing inline-lend materialization hook, exercise 192,000
retirement schedules, and reject a finish omitting the hook. Both tests now
run in per-commit shader contracts too. This is test-only; runtime evidence
and unresolved first-use failures remain unchanged.
Further fixture-only repairs use the actual block medium enum and reject
synchronous CHD reads, model newer pre-load fields without retaining inline
lends, and test the existing nonblocking frontend finish contract. A wait
negative control must fail. These three checks now run in shader contracts;
1,000 inline batches, 36 handoff cases and 40,000 frame requests pass locally.
This restores tests of earlier runtime changes, not new rendering behavior.
The index-load fixture also models the existing PRAMIN-restore/release
boundary, requiring release even on rejection after rebuilding bounds.
Vertex-format tests use the actual borrowed-inline accessor and poison
canonical storage while testing borrowed input. Another 13,504 array/inline
cases and all signed 16-bit conversions pass. Required negative controls
reject ignored lends and a missing migration release. Both join per-commit
shader contracts without changing runtime sources or accepted baselines.
The general QCOW2 integration fixture also uses the core's C11 language
standard for its existing `max_align_t` owner storage. Conversion cases pass
locally; runtime and alignment are unchanged. The hosted ARM64 PRAMIN timeout
remains a separately recorded unqualified platform result, not a relaxed gate.

### OpenGL dynamic vertex-family queue pressure

A dynamic lighting family needs both vertex prefix variants before its
adoption count reaches two. The service previously popped an owned pending
record with only one free generation slot, then ignored the failed second
enqueue. That lost a variant permanently. The service now requires two free
slots before consuming that record. Only the render thread changes slot
ownership, so the worker cannot take a checked slot between the capacity
test and publication. Other producers retain their one-slot checks.

No queue is enlarged, no pending payload is cloned, and no wait/spin, worker,
retained lend or release profiling is added. Job/key layouts, generated code,
module identity, serialized seeds and driver validation are unchanged; no
cache version change is needed. This is an OpenGL scheduling repair, not a
fix for the uncovered Vulkan first-use full-PSO path.

The production-function test uses the actual module-key layout and covers
all 65,536 occupancy patterns with one and eight pending families (131,072
cases), mixed queued/completed owners, retry after retirement, both prefix
keys and poisoned consumed-bank storage. A negative control restoring the
one-slot dequeue must fail. The existing 100,000-job concurrent worker test
also checks publication, duplicates, slot reuse and parking.

On Ryzen 9 9950X3D/RTX 5090, OpenGL/8x, a two-block ABBA comparison used
16 fresh boots: application-cache-empty and persistent-cache repeats.
Both revisions ran in the same 96 MiB L3 domain, with unchanged options,
verified native module-cache persistence, no overlapping local builds/tests,
and zero image readbacks/allocations. Driver cache state was uncontrolled.
All 15,136 measured fight-entry/live callbacks stayed below 16.683 ms,
including 9,600 verified live-round callbacks. Live run-mean comparisons
changed -0.56% cold and -0.20% warm, indistinguishable within noise. Fight
window maxima were 12.606/11.342 ms baseline/candidate cold and
10.769/10.876 ms warm. This is not a demonstrated speedup or presented-frame
latency measurement. The private passive MMU probe was outside `retro_run`;
the release build excludes that export.

One candidate initial callback took 223.298 ms before valid guest metadata
(first valid sample at callback 11). Retain it: complete boot smoothness is
not qualified. A separate eight-process early-boot ABBA repeat, four per
revision, did not reproduce it (67.129-69.713 ms maxima). Its cause remains
unresolved; these repeats do not erase the observation or prove driver-cold
recovery. No thresholds or accepted references were changed.

Full-resolution captures showed the same fight without missing geometry;
they were not byte-identical, so are not an exact-pixel equivalence claim.
The private frontend fixture was corrected before qualification to accommodate
8x output; earlier clipped captures are invalid. Separate save/two-load checks
restored identical RAM and reset resumed live play with correct audio cadence.
OpenGL content reload remains explicitly unsupported without a frontend
restart; the existing rejection was observed, not bypassed. Other device/
context replacement paths are not certified by this workload.

Require the known bad cold-play replay to fail and the candidate to pass;
retain the original failure and a same-hardware clean-binary comparison.
Show no new obvious compiler interruption relative to the verified smooth
reference, and nearly indistinguishable cache-empty versus warmed gameplay.
Keep value/image correctness, no skipped work, exact ownership, audio cadence,
reset/load/unload/device-change and cache rejection checks. Do not trade away
GPU execution performance for faster translation without measuring both.

This plan is complete as policy and a staged test architecture. The renderer
recovery and reference-hardware cold-play gate remain unfinished until that
evidence exists. No document or unit-test count replaces it.

## Supplied reference documents

The parallel-gx1 `AGENTS.md` draft reviewed on 2026-11-08 aligns on zero
draw-path compilation/waits, correct ready fallbacks, bounded workers,
zero-copy lends, stable identity, cold/warm comparisons and ratcheting budgets.
Xbox adds automated CI and keeps its current regression explicit. GX1's
cache-off default is not imported into the regressed Xbox implementation;
evaluate cache policy only after recovery and all cache modes are qualified.
Its rslc/backend-specific details are not Xbox NV2A implementation facts.
Reference: https://github.com/libretro/parallel-gx1/blob/master/AGENTS.md

- `parallel-xbox-shader-pso-handoff.pdf`, 2026-10-30: pp. 2-10 architecture,
  pp. 12-18 ownership, measurement, acceptance and proposed follow-ups.
- `parallel-xbox360-shader-renderer-optimization-handover-2026-11-08.pdf`:
  pp. 1, 6-10 and 12-13 scope, numerical controls, timing limits and ownership.
- `carmack_speed_of_light_optimization.pdf`: work-removal philosophy, not
  independently verified quotations or a hardware performance result.
- `parallel_cores_implementation_plan.pdf`: proposed mechanisms requiring
  the audit and corrections above, not authoritative descriptions of either core.

## Experimental programmable vertex readiness, 2026-11-14

The `XEMU_EXPERIMENTAL_READY_VSH` build flag prepares four completed GL
separable vertex programs before guest execution: smooth/flat shading times
ordinary/geometry-prefixed interfaces. It is disabled in release builds.
Selection currently covers programmable shaders with ordinary streamed vertex
attributes only. It reuses the allocation-free instruction validator; malformed
or unsupported input keeps the existing correct path. Fixed-function vertices,
other attribute encodings and the non-SSO path remain uncovered.

A device-generation owner holds the programs and one 2,192-byte code/control
UBO. The existing immutable binding owns an upload's source bytes. Repeated
bindings or byte-identical code reuse the current contents; eviction cancels
the witness before that cache node can be reused. No second microcode payload
snapshot is retained. `glBufferSubData` still permits driver copies, stalls or
allocations; this is not a global zero-copy or allocation-free claim.

Ordinary shader keys, source output, persistent-cache formats and savestate
layouts are unchanged. These native generic owners bypass persistent module
storage. Current native checks include the complete programmable numerical
corpus and negative controls, ordinary source-text equivalence, NGB restore
images and a DOA3 first fight. They do not establish general cold-play recovery.
The callback ABBA probe uses an uncontrolled driver cache and does not measure
GPU execution or displayed-frame latency. The full target above remains open.

Next integrate ready complete pipeline ownership and optional exact promotion.
Native GL pipelines are now shared by the exact three native stage-program
handles within one context generation. A ring of existing binding nodes owns
each pipeline; its indexed representative transfers before eviction, and the
last binding deletes it. This adds three pointers per existing binding and an
8 KiB index on 64-bit hosts, with no increased resource capacity or extra
payload storage. Lookup collisions compare all three stage handles. Different
guest microcode can reuse a completed pipeline when its selected programs are
identical; genuinely new stage tuples still create a native object. This does
not establish driver-lazy-compilation freedom. Fragment/geometry misses and
utility paths can still block required draws.
Do not promote permanent interpreter selection as a complete or performance-
qualified solution merely because its stage links moved to startup.


### Experimental finite geometry readiness

The opt-in GL build also prepares 16 finite geometry stages before guest execution. Winding rotations, provoking-vertex selection, depth perspective
and the atlas-stage tag travel as four packed integers. Topology, output
primitive type and interpolation remain native stage variants. The stages
reserve slots from the existing 51,200-slot module pool; no logical module
capacity is added. A partial startup failure deletes all native owners and
returns every reservation to the ordinary LRU. Normal teardown flushes shader
bindings before native owners, and owners before the backing module pool.
Ordinary geometry state/options, cache keys, serialized formats and emitted
specialized source bytes remain unchanged. Finished shader objects detach after
linking; native program owners retain their executables through the last
binding.

The geometry source builder changes its private final buffer in place to
lower indirect input reads to constant-indexed selectors. The equal-length
substitutions happen after all appends and before publication; no second
whole source payload is created. Driver compilation may still allocate or
copy internally. Runtime control packing/shadows are 16 bytes per native
stage, allocated with renderer metadata; this is not an allocation-free
renderer claim. A borrowed last-binding witness is cancelled before node
reuse. Controls are uploaded only when their normalized values change.

Native NVIDIA and llvmpipe correctness checks cover the full 12,960-state
matrix and 50,872,320 scalar outputs, including signed zero, subnormal,
degenerate, NaN and infinity inputs, with 38,880 ordinary source comparisons
across GL/Vulkan targets and an
independent emitted-color CPU oracle. llvmpipe also miscompiles indirect
inputs in some ordinary reference states. The test-only reference lowers
those reads independently with ternary selectors after checking original
source bytes against the pinned parent. Do not count the unlowered failing
reference or an arbitrary exception as a pass. Required bad depth, provoking,
winding and atlas controls must still fail. These GL checks do not qualify
Vulkan native readiness or general rendering/GPU execution performance.

The initial 94-stage prototype was correct but produced a 707.13 ms DOA3
startup callback. Retain that observed startup spike as a possible regression, not an accepted
baseline: it came from a functional diagnostic run, not clean timing
qualification.
Moving atlas selection into controls reduces the finite domain to 22 stages.
Functional checks restore byte-identical NGB images, reach 600 live DOA3 first-
fight callbacks with exact audio cadence, and pass GL reset/restore. DOA3 still
records roughly 468 ms startup callbacks in diagnostic runs. Startup and GPU
execution costs remain unqualified; this is not accepted stutter recovery. Fragment misses, complete pipeline
creation, fixed-function vertex coverage and utility paths remain separate
open work. No permanent generic-stage selection is performance-qualified
without GPU-cost evidence and optional complete exact-pipeline promotion.


The 22-stage clean ABBA callback probe (diagnostics/readbacks disabled) did
not establish a fight-window speedup: parent p99 10.815/10.181 ms, candidate
11.383/10.152 ms. All four runs preserve 600 live callbacks, audio cadence
and video delivery. Startup maxima regress consistently: parent 71.297/73.943
ms versus candidate 94.801/97.302 ms. Driver cache state is uncontrolled;
retain this rejection evidence rather than accepting the increase as normal.

The next experimental reduction shares quad and quad-strip stages: their
native layouts match, while packed control bit 4 selects the exact old emitted
order, plane association and odd-strip suppression. The finite set becomes
16 programs. The complete existing state/value corpus is retained, plus a
required strip-bit mutation and same-owner uniform-switch lifetime checks.
Quad provoking selection is always vertex 3, so it is emitted as a constant
rather than a uniform-dependent input selector. This is a work reduction,
not yet a qualified startup or gameplay latency improvement.


The 16-stage follow-up at c5c88f424b passes the complete native NVIDIA and
llvmpipe matrix, all six negative controls, and Windows/Linux ownership checks.
The corrected strip negative control reports a mismatched emitted-vertex count
explicitly instead of an ordinary assert; the positive corpus is unchanged.
Clean Windows default/experimental and Linux experimental builds pass. The
Linux private checkout required a corrected LF-only version stamp; the initial
malformed private stamp and build failure are retained in its logs.
NGB fresh-process restores and GL reset/restore retain the exact prior images.
DOA3 reaches 600 first-fight callbacks with exact audio cadence and no missing
video; the rendered image was checked visually, not claimed byte-equivalent.
Three padded boot audio callbacks remain. Functional tests overlapped builds,
so their roughly 475 ms maximum is retained but is not clean timing evidence.

The clean c5c88f424b ABBA probe uses four fresh processes, empty private app
caches, no build/benchmark overlap, no diagnostics and zero image readbacks or
readback allocations. At 8x, fight p99 is 10.554/11.379 ms for f3d5d52760 and
11.719/11.087 ms for the 16-stage candidate; maxima are 11.042/12.633 and
12.115/12.079 ms. These tails overlap run spread; all four 600-callback fight
windows have zero callback deadline misses. No GPU execution or displayed-frame
latency improvement is established. Startup maxima remain consistently higher:
parent 72.270/77.582 ms versus candidate 89.069/88.326 ms. This improves on
the 22-stage observation but still fails recovery against the parent. Driver
cache state remains uncontrolled, and total boot callback counts differ.
No stutter-recovery gate or acceptance budget has been relaxed. The feature
stays disabled; fragment misses, full native pipeline creation and Vulkan
readiness remain open violations. Fewer shader objects alone are not recovery.


### Experimental regular fragment source preparation

The source API generates two location-matched native GL 4.1 fragment variants,
smooth and flat, carrying regular texture/depth/clipping controls in uniforms.
Controls are eight 32-bit words of metadata, packed only after all supported
state checks succeed. Rejection leaves caller storage untouched. At the source-preparation checkpoint no native
fragment owners or sampler aliases were selected by gameplay; this is not
stutter recovery, complete pipeline readiness or an allocation-free renderer.

NONE, PROJECT2D, PROJECT3D, CUBEMAP, PASSTHRU and CLIPPLANE use typed 2D, 3D
and cube sampler parameters. Integration must assign twelve distinct units,
query unit capacity, borrow existing texture objects without a payload clone,
track alias invalidation/name reuse and disabled stages, and prepare both
native fragment links before readiness publication. Existing uniform caches
are indexed by native program, not guest binding; preserve that ownership.
Shadows, convolution, signed G8B8 and bump/dot modes remain uncovered.
Static all-zero final-combiner keys are rejected because their legacy output
is unset; dynamic zero-final keys have an explicitly zero output. There is
no prepared Vulkan fragment/PSO path in this API.

#### Explicit helper-liveness semantics and compatibility

The first 912-case mipmapped llvmpipe comparison failed at case 141. Raw texels,
coordinates and LOD queries agreed, but conditional volume sampling differed
and changed an alpha kill. Sampler unrolling, re-binding, control masking,
projection expansion and gradients computed inside the helper did not fix it.
Deferring ready-path kills fixed the isolated case, but the complete comparison
then failed at case 30 on both NVIDIA and llvmpipe against legacy eager kills.
Those runs remain failed evidence, not accepted baselines.

The GLSL specification makes derivatives after nonuniform discard undefined:
https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.60.html
The replacement must preserve helper execution in both the ordinary reference
and the ready path; merely blessing a different output is insufficient.

DEPTH_FORMAT_HELPER_LIVENESS (bit 9 in the existing depth-format word) explicitly
versions the new semantic domain. Its GLSL accumulates window, depth, clip-plane,
texture alpha and color-key kill conditions and discards after texture work.
No shader image stores or atomics are emitted, so killed invocations publish no
framebuffer/depth result. Native SPIR-V 1.5 uses a Function-local boolean and one
final OpKill; 1.6 retains its existing OpDemoteToHelperInvocation behavior.
Legacy bit-clear keys, source and seeds retain their previous behavior. Struct
size is unchanged, but the bit is part of exact/hash shader and linked-program
keys, so compiled legacy programs cannot be adopted for helper-preserving keys.
The source-preparation checkpoint did not capture the flag from guest state
or add it to seed metadata; the pending opt-in integration is described below. No
release default or cache default changes. Ready controls reject bit-clear keys.

The source domain has no conditional per-pixel texture dispatch other than its
kill predicates: sampler/type/mode controls are uniform. Delayed kills can
increase work for ultimately discarded pixels. GPU-cost and cold gameplay
qualification are still required before renderer adoption; no throughput or
latency improvement is claimed.

#### Numerical checks

The GL fixture compares 912 constructed helper-preserving ordinary/ready
programs per device: 291,840 exact color/depth values, all four texture stages,
zero through eight observable chained combiners, volume slices, cube faces,
zero/negative projective q, interpolation modes, three mip levels, six
minification modes, both magnification filters, addressing modes, LOD bias,
and reserved-bit poisoning. The two ready sources must remain invariant.
NaN payload differences are ignored only when both results are NaN.
An independent half-plane/window/constant-volume oracle requires exactly eight
live pixels and analytic color/depth on both paths. Per-device raster coverage
may differ; comparisons use that device's own reference, not a cross-vendor
raster-equivalence claim. Wrong depth/mode/cube/volume/clip/alpha/combiner
controls and removal of the reference helper-liveness flag must fail. Eleven
unsupported-state checks require atomic rejection without a source owner.

Native SPIR-V contracts add 128 explicit helper-preserving 1.5/1.6 modules:
1.5 must have exactly one kill after the final image operation; 1.6 must retain
byte-identical demotion code. The legacy 65,024-module corpus remains intact.
Four 512-case Vulkan numerical subsets exercise both versions against
helper-preserving GLSL; it supplements, never replaces, the full legacy suite.
Ordinary 73,728-source byte/lifetime checks remain required against pinned
396d8f1f0a. Readbacks, reference compilation and comparison storage are test-only
correctness work, not gameplay allocation or timing evidence. Public CI and
local checks do not qualify full native pipeline readiness or cold first play.


### Pending experimental native fragment integration

The follow-up worktree reserves two slots from the existing module pool for
native fragment programs and borrows 3D/cube texture names on units 4..11.
It remains experimental and unqualified for cold-play recovery. Guest GL capture adds the helper-liveness
flag only for covered states, after both programs, uniform locations and all
typed sampler routes are ready. Uncovered exact shaders retain legacy keys
and source instead of needlessly recompiling under different semantics. Default builds and Vulkan state capture do not enable the flag.
Legacy seed keys retain eager semantics; they do not silently become current
helper-preserving bindings. No seed migration is enabled yet.

Retirement invalidates only aliases naming the retiring native texture, before
the pool can delete/reuse it. Primary-unit helpers and flip resets preserve
reserved alias bindings; their surface entry points enforce guest units 0..3. Updates preserve the previous active texture
unit; disabled stages never borrow stale TextureBinding pointers. Shader
finalization follows binding/pipeline flush and clears aliases before texture
teardown. Per-native-program control shadows and binding witnesses are
invalidated before LRU node reuse. These ownership claims still need executable
lifetime/failure tests, native rendering, reset/restore/unload tests and cold
first-play measurements. Draw-time native pipeline composition remains an
explicit violation even when a covered fragment no longer compiles.


Current integration evidence (still unqualified for recovery): the actual
native-owner orchestration passes 29 partial-startup failures and eleven
mutation controls on Windows and Linux, including exact slot return, shader
object retirement, binding witness reuse, typed aliases, same-name/CPU-slot
reuse, and primary-versus-reserved-unit independence. Native GL diagnostic NGB
restores match the prior image exactly with 77,766 prepared-fragment binds.
Preserving reserved units reduces alias calls from 10,016 to 12 in the matching
restored window; this is a work reduction, not a latency claim. Initial copied
harness scripts accidentally retained old DLL paths; those runs are invalid
integration evidence. Corrected drivers require the expected commit log and
positive READY_PSH native-path counters, and retain exact binary hashes.

Fresh-boot DOA3 plus 600 live fight callbacks completes with 1,721,350 covered
fragment binds and 12 alias calls. Both the earlier and latest integration images were visually
inspected and show correct fighters, stage textures, shadows and HUD; different
poses/camera positions do not establish pixel equivalence between fight runs. Actual OpenGL and
Vulkan 360-callback restore/reset/restore/unload checks match the prior images;
Vulkan reload also remains covered, while GL reload remains unsupported.
Functional maxima of 477.75 and 492.87 ms are retained as failures/unqualified
stutter evidence. Diagnostics, readbacks and uncontrolled driver caches do not
qualify timings. Fourteen native pipeline objects still get created during the
DOA3 run; startup/full-pipeline readiness, clean matched-parent/cold-warm timing,
GPU execution costs, Linux full builds and default-build regression checks
remain required before promotion.


The exact-parent e1b7b12420 release ABBA probe initially appears faster by
callback time (fight p99 6.893/7.323 ms versus 11.321/10.193 ms), but this is
REJECTED as a speedup. A private harness observes the published instruction
clock and DOA3 USA's actual CDevice Present/completion counters, using the
verified device pointer at 0x1c3390 and frame fields +0x2b60/+0x2518. Over 600
live callbacks, both builds advance 9,993,316,667 ns; the parent advances 599
Presents/completions and the candidate only 82. A video callback per run and
valid end images do not prove preserved game-frame progress. Core clock reads
are acquire loads of published time; guest observations are bounded borrowed
reads while the CPU is gated, not a RAM snapshot or image readback.

The next source probe removes dynamic indexing of the mutable combiner bank
from prepared fragments, testing GPU execution cost as the cause. Constant
selector branches preserve every component write and arithmetic order. The
legacy emitter still appends its identical immutable helper text in one call;
prepared source is assembled from borrowed constant spans into its final
existing owner, without a whole-source clone. These probes remain experimental and
unqualified for release until the full cold-play requirements pass. Do not
promote e1b7 to libretro or describe its callback result as recovery.


Follow-up execution probes keep the full interpreter while adding constant-bank
selection, packed modulation, SUM-with-known-zero products and E/F-independent
final descriptors. The product probe passes 1,164 cases / 372,480 color/depth
values on NVIDIA and llvmpipe, including nine deliberately broken controls.
Actual Present throughput is 100.635 FPS (246 Presents/completions), compared
with 119.81 FPS for a347 and 53.83 FPS for the initial e1b7 integration. These
are unpaced, uncontrolled-driver-cache rejection probes, not recovery evidence.
The product probe's 1,149.932 ms startup spike remains recorded. Final-descriptor
coverage adds 144 cases without dropping the original 912 or rich/product cases.
An initial EF negative control was inadequate; the strengthened mutation must
falsely accept both EF-dependent input and nonzero EF fields to expose the
missing dependency. Retain the failed test attempt, not a manufactured pass.

Eighteen fully linked/validated prepared native pipeline tuples now borrow
binding slots from the existing 51,200-entry pool. Interpolation and geometry
prefix conventions determine the valid combinations; unsupported tuples keep
the correct existing path. Anchors use the same exact native-tuple owner index
and borrower rings, survive normal binding eviction, and retire after cached
borrowers and before backing slots/native stages. Thirty-six creation/validation
failure points return every reserved slot, and unavailable stages do likewise.
Mocked native APIs verify 36,000 acquisitions without creation and six mandatory
bad controls. This establishes ownership, not physical-GPU compilation latency.

A fresh DOA3 diagnostic run validates all eighteen tuples before guest execution
but still creates eleven uncovered pipelines. Opt-in creation-site stage
diagnostics in a subsequent run identify two uncovered fragments and seven
fixed-function vertex stages; varying game progress changes counts, so these
are not a matched-workload reduction claim. The new first-fight image was
visually inspected; fighters, textures, shadows and HUD are present. NGB's
restored image remains byte-exact and both GL/Vulkan reset/restore checks match
the prior output. Driver caches remain uncontrolled, all these runtime checks
are functional, and GL reload remains unsupported. Default builds do not select
this experimental architecture. Full ready coverage, Vulkan readiness, clean
parent/cold-warm timing and GPU execution cost remain unqualified.


The final-descriptor fast path now uses literal register operands for its three
recognized programs; it retains input maps, sum clamps, mix arithmetic and
zero additions. The complete updated runner passes 1,308 cases / 418,560 exact
color/depth values on both NVIDIA and llvmpipe, plus all ten mandatory negative
controls. The original 912 cases remain intact. Real MString/GString ownership
and ASan/UBSan retain the 73,728-source / 614,644,992-byte legacy comparison;
256 additional prepared builds retain only the final owner and release all
storage, with a fifth mandatory deliberate ready-source leak rejected.

Clean experimental release ABBA runs after this change advance 599 guest
Presents/completions in every 600-live-callback window, with the same 9.993 s
of guest time. Parent throughput is 122.699 / 124.600 guest Presents/sec versus
119.392 / 120.390 for the candidate. Fight callback p99 is 9.614 / 9.634 ms
versus 10.018 / 9.979 ms; no fight callbacks exceed 16.683 ms. The modest
remaining gap is not declared recovered or an accepted regression. A separate
first-use run's 1,105.093 ms frame-zero startup spike remains recorded; later
uncontrolled-driver-cache runs are not substituted for genuinely cold startup.

A matched, warmed native GPU execution fixture uses the same final descriptor,
textures, 1,048,576-pixel target and source-independent driver for the a347 and
current prepared generators. Twelve alternating batches of sixteen draws,
preceded by four warm draws and glFinish, use GL_TIME_ELAPSED without timed
readbacks. Prepared 0/1/4/8-stage medians fall from 126.794/133.124/148.860/175.220
microseconds to 16.640/22.334/51.226/91.494. Four-stage volume/cube medians fall
from 148.534/148.564 to 51.124/51.170. This is a simple execution fixture, not a
retail distribution or cold-play claim; the exact generated shader simplifies
heavily and remains about 12.8 microseconds. Generic shader GPU overhead remains
an open issue even after this measured improvement.

Clean Windows default/experimental/diagnostic and Linux default/experimental
builds pass. Default GL/Vulkan lifecycle output matches the prior captures.
The a347 public CI rerun passes unchanged thresholds after its initial compiler
stage timing was inconclusive; this is evidence for that source checkpoint,
not a pass for the current commit or physical-GPU cold-play qualification.
The feature remains disabled by default and is not merged to libretro.
