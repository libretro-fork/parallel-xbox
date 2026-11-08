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
- The qualifier receives all revision/reference pairs and the accepted SHA.
  It must implement the cache/scene matrix above, return nonzero for a new
  hitch, invalid progression, absent fixtures or inconclusive run, and put
  only sanitized metadata/timings in `public-report/`. Its implementation,
  fixtures and physical GPU runs are remaining recovery work, not a shipped
  guarantee from this workflow hook.

Until hardware qualification exists and recovery passes, CPU/software-GPU
green checks do not authorize describing cold play as fixed. Documentation
and CI changes leave the existing runtime regression explicitly open.

## Completion evidence

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
