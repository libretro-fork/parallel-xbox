# Cold-first-play recovery and regression prevention

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
The prototype still creates pipeline objects for raw-code binding identities;
fragment/geometry misses and utility paths can still block required draws.
Do not promote permanent interpreter selection as a complete or performance-
qualified solution merely because its stage links moved to startup.
