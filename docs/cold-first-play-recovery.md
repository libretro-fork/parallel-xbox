# Cold-first-play recovery and regression prevention

## Derived vertex fixture domain repair, 2026-11-17

The first balanced direct-vertex scheduler exposed two inherited-fixture bugs
in CI: the CPU source-owner runner tried to launch a sibling scheduling script
from its temporary directory, and extended dynamic interpreter case 42,512
hit the direct oracle's fixed bitmap bound. Keep the explicit direct-domain
marker false in CPU ownership, and remove the direct-only case-note call from
derived interpreter and CPU bodies. Their complete existing ranges, exact
comparisons, sanitizers and deliberate fault controls remain unchanged. The
direct Vulkan oracle retains its required exact bitmap and retirement checks.
This is test machinery only; it does not fix the remaining compiler timing
noise, fragment watchdog failures or qualify shader-stutter recovery.

## Balanced software-vertex scheduling experiment, 2026-11-16

Job 133624500023 passed the first 7,168-case batch with exact values, then
timed out in the unchanged [7168,14096) batch at its shared 300-second
deadline. A private no-draw key probe found zero exact native-pipeline hits
in a 32-entry cache for that range, so a fixture pipeline cache was rejected
as unhelpful; that probe is not numerical validation.

The scheduling experiment spreads adjacent sixteen-case groups across the
existing sixteen rounds instead of concentrating whole expensive families
in one batch. Each round still has one shared 300-second watchdog and at
most four workers. Total nominal batch budget remains 16 times 300 seconds;
no numeric case, generator, uniform setup, draw, readback, independent oracle
or comparison rule changes. Exact bitmaps verify every one of 42,512 cases
and 69,379,584 scalar comparisons, with no missing or duplicate ownership.
The derived programmable GL/interpreter fixtures retain their own runner
and domains. Required CPU controls cover edge ranges, 1/2/3/4 workers and
1/2/4/16 rounds, four broken ownership rules and eight manifest/process
faults. Windows and Linux CPU controls pass, including explicit live-child kills
and terminal waits. The full validation-enabled software Vulkan run passes
all sixteen rounds with two allowed CPUs and disk shader caching disabled:
42,512 cases, 69,379,584 exact scalar checks and zero validation errors.
The native numerical case bodies remain byte-identical to the parent.
Derived GL and Vulkan interpreter extraction smokes check their separate
domains; selection is explicit metadata, not a human-readable trace label.
These are correctness and budget checks, not renderer speedups; CI and
physical cold-play qualification remain unqualified.


## Compiler timing sample experiment, 2026-11-16

Runs 43959933516 and 43960072862 retained identical compiler source hashes,
module sizes and instruction counts, with zero per-shader regressions, but
failed timing qualification because aggregate or individual stage samples
varied beyond the existing 5% noise limit. These failures remain unqualified.
The experiment increases positive comparison iterations from five to fifty
per input/target while retaining every selected module, both targets, two
ABBA blocks, stage and per-module ratchets, and the existing 180-second
process watchdog. Negative-control timing stays separate with its original
five iterations. No gate threshold, runtime source or cache capacity changes.
The local fifty-iteration comparison passes all stages on 138 selected
modules, with no code growth or isolated regressions; the unchanged injected
slow-compiler control is rejected. This sampling change still requires CI
evidence; one local comparison is not a general variance guarantee.


## Ready-fragment CI scheduling repair, 2026-11-16

The old 1,308-case fragment fixture gets the same bounded context scheduling
used by the separate newer experimental branch, without taking that branch's
larger generator or corpus. It retains 418,560 exact color/depth comparisons,
the independent half-plane/window/volume oracle and all ten serial fault
controls. Per-context ranges/check counts must sum exactly, with no gaps or
duplicates; required CPU-only faults verify metadata, the shared watchdog and
child retirement. The old serial CI timeout is not treated as a numerical
pass, and software-vertex timeout work remains separate and open.

The complete llvmpipe run passes on two allowed CPUs with all 418,560 exact
comparisons and all ten shader-fault controls. Runtime sources and generators
are byte-identical to the parent. The Mesa driver-cache initial state was not
verified, so this is numerical coverage, not cold-cache timing qualification.


## GL disk-cache mode selection, 2026-11-16

The separable-program renderer never consumes legacy linked-program binaries.
It now skips that reload worker and preserves the legacy reload list instead
of overwriting it with hashes of programs it never saves. Monolithic rendering
retains its existing loader and writer. Disabled caching creates no reload
worker. Stage-module caches and shader seeds are unchanged, as are renderer
ownership, worker join/drain, cache flushing and completion notification.
The redundant startup cache-directory wrapper is removed: the existing atomic
cache writer creates both its family and bucket directories when needed.

The production-function fixture retains all 18,000 publication schedules,
64 mapped/capped round trips and truncation cases. Four mode/cache combinations
check worker creation, legacy list I/O, mappings, seed maintenance and completion.
Mandatory faults reject an eager SSO reader, overwriting the legacy list and
skipping maintenance. Sanitized software and native validation pass, including
the existing immutable-cache writer, module-export, stage-lifetime and bounded
prewarm fixtures. Clean default Windows builds introduce no experimental flag
or release diagnostics.

A separate warm-cache DOA3 first-fight diagnostic starts with the same 109-entry
legacy list and stage-module cache. The parent starts one legacy worker and
attempts 109 unused linked-program loads; the candidate starts none and attempts
none. Both prewarm the same 81 stage-module files. Release DOA3 first-fight images
match byte-for-byte, as do NGB images after two fresh-process external restores;
audio/video callback cadence remains correct. The candidate preserves the legacy
list bytes. Those diagnostic/image runs overlap an unrelated build and include
instrumentation or readback, so their latency is not qualified. An isolated default-release ABBA comparison then completed with no competing
build/test or readback: all four runs preserve 599 guest presents/completions
and 600 first-fight callbacks without a 16.683 ms deadline miss. Parent rates
are 119.707/121.669 guest FPS versus candidate 121.107/123.313; fight p99 values
are 11.500/9.584 ms versus 10.174/9.922 ms. Parent fight maxima are
12.288/10.439 ms versus 11.901/13.117 ms. The observed spread does not establish
a frame-time improvement or a reproducible regression. Whole-boot maxima remain
64.610/64.772 ms versus 63.664/63.511 ms; those spikes are retained, not discarded
as warmup. Private driver-cache state remains uncontrolled. This is a matched
warm-cache component check, not physical-GPU cold-play recovery. It removes
unconsumed work, not the remaining draw-critical shader compilation.

## GL query allocation audit, 2026-11-16

Pooled GL reports now retain at most their existing eight inline query names.
Only actual use creates a name; the 1,024-slot pool is unchanged and no names
are generated eagerly. The retained prefix therefore has an upper bound of
8,192 names. A queued report owns its prefix until its results are consumed or
explicitly discarded. CLEAR ends any open interval before returning its owner;
starting a new GL query resets result availability. All cached names, including
ones unused by the last interval, retire before the renderer's report pool.
Overflow vectors/names and pool-exhaustion fallback objects retain their
existing allocation/release behavior. This is not allocation-free rendering.

The sanitizer fixture keeps the original schedules and adds fallback/reuse
coverage with fresh values per query interval. Its 15,069 intervals create
8,907 native query names versus the parent's 15,069. In the first 1,500 pooled
schedules, the names grow only to the actual inline high-water mark of eight.
Three mandatory faults reject lost cache state, aliased live/queued query
names and leaked cached names. The runner still checks the parent with its
original ownership behavior; CI requires the new reuse contract explicitly
for the candidate.

A separate real-GL fixture verifies 1,146 exact report writes against CPU
sample-count expectations on NVIDIA and llvmpipe. It covers changing results,
all scale factors, CLEAR with an open interval followed by reuse, overflow,
genuine pool exhaustion, queued/active teardown and reinitialization. It creates
237 native names versus the parent's 669 and rejects the same three faults.
Driver calls are counted only in this fixture. GPU completion waits and its
small shaders are test-only; no release diagnostic or new gameplay wait is
introduced. These are work-count and correctness results, not proof of reduced
driver allocations, faster frames, or shader/PSO cold-play recovery.

Clean default Windows parent/candidate builds differ only in `reports.o`; all
other 543 objects match byte-for-byte. The report remains 72 bytes with every
existing field offset unchanged. Separate diagnostic DOA3 first-fight and NGB
external-restore runs create no visibility queries, and OutRun 2's 3,600-run
title-screen test serves seventeen empty reports but creates no queries.
NGB and OutRun 2 images match their parents byte-for-byte. These retail samples
therefore establish no query-reuse performance benefit and do not attribute
their shader stutters to this path. An isolated release ABBA comparison
completed before the next unrelated build began: parent guest-present rates
were 120.527/120.239 FPS and candidate rates 121.715/119.890 FPS. Each run
completed 599 guest presents; all 600 first-fight callbacks met the 16.683 ms
deadline. Fight p99 values were 10.114/10.587 ms for the parent and
9.942/9.966 ms for the candidate. This establishes no measurable speedup;
private driver-cache state remains uncontrolled. The core's existing frontend GL
context-replacement/reload limitations are unchanged.

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


### Prepared fragment register exits (2026-11-14 checkpoint)

The prepared fragment writers keep their original constant-index switches,
but each case returns after its component write instead of joining through a
`break`. The default case also returns. This changes no selected register,
component, arithmetic expression, key, uniform ABI, native owner, pool size or
renderer default. `XEMU_EXPERIMENTAL_READY_VSH` remains disabled by default.

The return-only source is SHA-256
`25d23392478013187e653bf8732657bf967294a0b3be142ed1fbffd599935ad6`.
The full numerical suite passed on the NVIDIA RTX 5090 (621.01) and Mesa
llvmpipe (25.2.8, LLVM 20.1.2): 1,308 cases, 418,560 exact color/depth checks,
two invariant prepared fragment sources, the independent helper-lane oracle,
and all ten required negative controls. The existing 300-second per-process
watchdog and complete corpus were retained. These are functional results,
not clean compiler or gameplay timing measurements.

ASan/UBSan source-owner checks also passed: 73,728 legacy sources and
614,644,992 source bytes stayed identical to the pinned historical reference;
256 prepared builds retained only the returned source owner and released it.
All five owner-failure controls were rejected. Ordinary and Vulkan generated
source behavior is unchanged.

Hardware latency for this return-only change is **unqualified**. An isolated
experimental release build exists, with only `psh.o` differing and all 543
other objects identical to its rebuilt parent. A separate local graphics
build prevented the final isolated performance measurement. No gameplay
speedup or stutter recovery is claimed, and remote CI remains necessary.

The larger grouped-write and balanced-read rewrites were tested separately
and are excluded from this checkpoint. Their numerical passes and faster
warm GPU fixtures did not establish gameplay recovery. In particular, the
balanced-read experiment recorded an 803.689 ms fight callback at live frame
155 (run 4,392), with seven fight deadline misses. That callback advanced
16,683,333 ns of guest time and exactly one guest Present/completion; the
stall was not hidden by dropping a guest frame. The separate local build
started after that run and does not explain it. Keep this adverse evidence
and the uncontrolled driver-cache/run-to-run limits; a later warm result does
not erase it.

Full prepared coverage, remaining synchronous misses, native first-use
behavior on both renderers, cold/warm tail recovery and remaining gameplay
allocations are still open. The overall cold-first-play regression stays
**BAD/unqualified**.
