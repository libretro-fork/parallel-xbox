# Cold-first-play recovery and regression prevention

## Observed Vulkan render-pass scheduling, 2026-11-18 (qualification checkpoint)

A private passive miss capture of `9632470d1b` reproduced all 51 cold and
10 own-seeds-warm ordinary pipeline misses without dropped records. Both runs
completed 600 fight callbacks, 599 guest presents/completions and the same guest
time domain, with no fight C-allocator calls or shader/pipeline creation. These
are work and identity observations, not presentation-latency recovery evidence;
private driver-cache state remains uncontrolled. C-allocation counts cover
wrapped direct/IAT calls, not C++/custom allocators or OS/driver storage.

The cold run had no game seed states. Moving the seed service earlier cannot
remove those 51 unknown-state misses. All ten warm misses were exact members of
the 100-state seed bank, but their required color-only or color-plus-depth pass
was not represented in the pump at the miss. The first pump slot instead held
a native depth-only pass introduced by startup utility work. A shader seed is
not a complete ready pipeline: the render-pass compatibility identity matters.

The pump now records ordinary draw and explicitly requested shader-prewarm pass
formats in its existing four slots. It does not admit a pass merely because a
utility created its native owner. Genuine ordinary depth-only rendering remains
eligible; this is consumer tracking, not a format blacklist. Admission copies
the existing eight-byte format identity, deduplicates exact matches, declines
a fifth distinct pass and allocates no retry storage. Empty seed banks may keep
that metadata without scheduling jobs. Retry storage is created only when an
unready state actually needs retrying, and teardown permits registered slots
that were never pumped. The existing four-state/32-readiness-check budgets,
completed-job drain, queue ownership and correct synchronous fallback remain.

The observation is explicitly post-bind. Its passive query reads tracking and
seen state without calling `vk_spec_phase` or `pgraph_vk_seed_spv_ready_hash`,
which drain worker results and would perturb readiness. Exact key, shader and
pass bytes are retained in bounded diagnostic-only storage; snapshots and
profiling are absent from release builds. The private builder's argument-name
collision interrupted its final cleanup check after source restoration; byte
backups and exact HEAD/status were checked and a coherent clean rebuild was
completed before either capture. That failed script run remains recorded.

Native Windows and WSL contracts pass the original 19,824 cases on current
and historical source, with 18 current and seven historical fault controls.
The supplementary cases cover utility exclusion, real depth-only draws, late
admission, capacity, disabled capabilities and registered-but-unpumped owners.
The sibling owner fixture retains its 32,768 cases and ten historical controls;
a typed registry observer adds two controls for exact pass forwarding. The
first CI attempt exposed that missing fixture dependency; its failure is kept,
and the strict compiler and original corpus are retained. Other extractors
were audited. An older, unused prewarm-input oracle also fails on the parent
and remains separately unqualified rather than rewritten into a new pass.
Numerical CI, cold/warm API work, clean latency, image and lifecycle
qualification remain required before delivery.
This scheduling change does not provide an already-ready path for an unseen
shader family or the first encounter with a new pass. Neither zero cold misses
nor full shader-stutter recovery is claimed; the broader regression stays BAD/open.

## Bounded opt-in delivery, 2026-11-18

The delivery uses a restart-only OpenGL Prepared Shaders core option, disabled by
default. The real frontend table, pending value, first-GL latch and all
three startup admission blocks are exercised by five platform/build variants,
100 wiring cases and nine fault controls on native Windows and WSL. The ordinary path remains available with full cache capacity; opt-in
reserves the existing finite bank and initializes validated stages/tuples on
supported SSO contexts. Neither mode changes guest clocks, audio or frame
progress policy. Unsupported states and failed preparation retain correct
ordinary rendering. Restart RetroArch to apply a different selection.

Global default enablement was rejected for this delivery. Twelve clean DOA3
boot-through-fight runs retain all 7,200 fight callbacks and show no fight
callback over 16.683 ms. Cold p99 medians are 10.246 ms ordinary and 9.861 ms
prepared; own-persistent-warm medians are 10.195 and 10.133 ms. Cameras/workloads
vary across fresh boots and physical driver-cache state is uncontrolled, so
these are not matched-scene or presentation-latency recovery results.

Separate three-pair 8x restored-state runs retain every first restored callback.
NGB median maxima are 102.110 ms ordinary versus 171.602 ms prepared; DOA3 arena
is 87.894 versus 599.456 ms. Some later callback distributions improve, but those
reproducible first-use/restore spikes are not discarded as warmup or accepted
as global default recovery. Their driver/allocation attribution remains open.

The private timestamp probe remains UNQUALIFIED: a 128-pair diagnostic ring lost
128 candidate samples, and ordinary attribute-pointer displacement can produce
different basevertex descriptors from different pre-restore host history. No
sample loss, frame shifting, reordering or extra key normalization was accepted.
A missing-finalizer measurement was also retained as failed instrumentation,
not converted into a rendering failure or a performance pass. This partial data
cannot establish permanent-interpreter GPU execution nonregression.

The option makes the tested coverage accessible in normal libretro builds;
it does not claim complete coverage, freedom from driver lazy work or a recovered
cold-play baseline. BAD/open status and all remaining Vulkan and allocation work
are preserved. Continue GPU cost and first-use attribution after this bounded
main-branch delivery rather than expanding the bank before users can select it.

## Rejected global-default candidate checkpoint, 2026-11-18

The earlier global-default candidate enabled the existing prepared stages and
complete pipeline tuples in normal builds. SSO capability and complete validated owner
publication remain mandatory; unsupported state, missing interfaces and failed
optional preparation keep the ordinary correct path. The legacy build-symbol
names remain internal compatibility names, not runtime options. Developer-only
`XEMU_DISABLE_READY_GL` suppresses implicit build enablement for comparison.
This section supersedes the default-off statements in historical checkpoints.

Programmable startup now verifies the actual 2,192-byte code buffer before
publishing readiness, including nonzero program/buffer names and GL errors.
The caller's binding is restored, partial owners retire, and finite own-error
flags are reported and consumed after a clean-entry check. An incoming error is
reported separately before creating owners. The original owner corpus, five
selection/upload/invalidation faults and eviction ordering are retained. A
separate startup fixture covers eighteen failure cases and thirteen deliberate
faults under native/WSL UBSan. The default preprocessor contract covers twelve
build variants, ten historical/partial-advertisement cases and five faults.

The same finite error boundary now covers geometry, fragment, FF and complete
pipeline preparation. Zero program names are rejected before link queries;
resource, interface and publication errors run existing owner/LRU rollback and
report/consume at most eight own error flags after a clean-entry precondition.
An incoming error is reported separately and declines optional startup. All
original owner cases and mutation controls remain; additional OOM+INVALID_VALUE,
prior-error and publication controls pass on native Windows and WSL.

Integrated fragment/shadow, full 42,512-case vertex/interpreter and procedural
mip suites pass on NVIDIA and llvmpipe, including their original rejection
controls. The finite FF compound logs retain all four complete domains and
22 fault markers per driver; their host-interrupted outer wrapper completion
status remains explicitly unqualified, rather than invented as exit zero.
The separate full historical CI replay passed after its one compiler-stage
sample was initially inconclusive (-0.2%, other stages passed); unchanged
rerun thresholds/corpora and the failed report are retained. This does not
substitute for the exact default candidate's runtime measurements or CI.

That global-default candidate was not promoted. Its delivery checks were bounded:
finish the integrated numerical suites, repeat the existing twelve clean runs
against the corrected ordinary reference, and capture cold/warm native work in
the exact default build. Permanent programmable-interpreter selection requires
matched-scene execution-cost evidence; moving links to startup alone is not a
performance qualification. Preserve every startup callback, the BAD/open status,
unsupported-state fallback, Vulkan misses and observed GL buffer orphaning.

## Corrected-reference integration, 2026-11-17

The ordinary GL helper correction is integrated into the prepared renderer.
The prior one-pixel NGB difference was independently traced to undefined texture
derivatives after an early emulated depth discard in the ordinary reference.
See [the analytic, GPU and retail evidence](gl-helper-mip-selection.md).
Both experimental flags remain off. The integrated candidate still requires its
own complete corrected-reference comparisons; earlier work counts and timings
are not silently promoted to evidence for this new runtime.

## Pre-integration replay and qualification, 2026-11-17

An older full replay exposed two additional historical mismatches: demanding
FF-bank files from main revisions before the feature was introduced, and
requiring VFS7 negotiation from the earlier VFS6 public ABI. Select the FF
contracts explicitly by source capability. A partial/advertised implementation,
missing stage contract or missing fixture remains a hard error. The standalone
scope regression checks 258 presence/advertisement/missing-contract cases.
Historical absence is recorded as historical, not a successful FF test; those
revisions still run the unchanged original shader numerical/compiler suites.
VFS expectations come from the public ABI's V7 identity definition, not from
the tested negotiation implementation. Keep every offered version and all
6,390 positional/short/EOF/decline/gap cases on both v6 and v7 source trees.
Native Windows and Linux sanitizer checks pass for both real revisions.

Twelve serial clean DOA3 headless runs retain boot, menu, fight entry and all
600 fight callbacks per run. Cold fight p99 medians are 9.716 ms for main and
10.262 ms for the prepared candidate; the fresh-boot camera/workload differs,
so this is no matched-scene or display-latency qualification. No fight callback
exceeds the frame budget. Startup callback cost is real and retained: roughly
62-64 ms on main and 156-161 ms with the prepared bank. Physical driver cache
state is uncontrolled; do not claim cold-driver-cache recovery.

A longer, separately captured NGB restore reveals one reproducible pixel
at (614,389), after 600 callbacks, changing from RGB (76,64,54) on main to
(70,57,50) with the experiment. Three process pairs reproduce it. Private
stage-selection controls and projection controls attribute it to the explicit
helper-liveness projection, including ordinary shaders when prepared-stage
selection is disabled. Full-ready vertex/geometry gating does not resolve it.
Private depth precision/invariance/reciprocal variants also do not resolve it;
none were imported. Source bytes were restored and coherent clean objects
rebuilt after every probe. At that captured revision the difference remained OPEN, and the candidate was
not byte-equivalent at that longer checkpoint. The correction above now explains
the old reference error; the integrated runtime still needs its own image run. Earlier 100-frame restore
matches and full numerical corpora do not certify that broader claim.

The pre-integration test/documentation changes used experimental runtime
6192cd81b1. Both flags remain disabled by default, and physical cold-play
smoothness is still BAD/regressed and unqualified. Vulkan draw misses and
21,000 GL array-buffer orphan calls per captured fight remain open work.

## Prepared fragment owner fixture, 2026-11-17

The first replay caught an unused-function error under -Werror: the complete
fragment owner include gained family adoption, but its isolated owner fixture
never called that helper. Exercise covered adoption, unavailable owners and
unsupported states while preserving the exact metadata key. Keep all 29
startup failures and eleven existing ownership/alias/teardown mutations.
Native Windows and Linux CPU fixtures pass for current and original 4ebf06d6d6
sources. Replay uses the current owner fixture driver against each immutable
revision, so the fix also verifies the originally failing revision without
editing its runtime. No runtime or numerical comparison changes are involved.
The complete CSV/texgen numerical and six fault-control rerun passed after the
prior Windows temporary-file cleanup failure; retain that original failure
separately. Full renderer CI and cold/warm latency qualification remain open.


## Ready-gate replay contract, 2026-11-17

The ready-family fixture now extracts the actual gated queue-submission block
for every revision. Earlier revisions verify 131,072 legacy submissions and
two rejecting controls; prepared revisions verify ready adoption, retained
unsupported submissions, exact family metadata and five rejecting controls.
This avoids demanding a helper from a revision before it existed. Missing
new fixtures on a selected prepared revision remain failures. Runtime bytes
are unchanged from 6192cd81b1; its private cold and own-seeds-warm captures
show zero post-startup compiler calls through 600 fight callbacks, with the
existing cadence checks. This is work attribution, not latency qualification.
The shader-stutter regression and Vulkan readiness remain open.

## Ready-fragment family adoption experiment, 2026-11-17

The prepared-bank DOA3 cold and own-persistent-warm captures at 39720e5a24
have zero draw-scoped shader/program compilation, linking or pipeline creation
from boot through 600 live fight callbacks. Both preserve 599 guest Presents,
599 completions and audio cadence. NGB and DOA3 external restores in fresh
processes produce byte-identical parent/candidate images. These are private
work-count and correctness results, not latency or recovery qualification;
driver caches remain uncontrolled. Fight C malloc/calloc/realloc/free wrappers
observe zero calls; 21,000 array-buffer orphan calls and driver allocations
remain open. Vulkan's synchronous uncovered PSO paths also remain open.

A further private capture attributes all four post-first-frame compiler
submissions to gated dynamic-fragment families, not draw misses. Their keys
predate the helper-liveness projection applied by the draw and therefore do
not match the prepared fragment bank. When that bank is fully native-ready
and the same projected state passes its coverage predicate, notify adoption
using the restored exact family key instead of generating/compiling another
program. Unsupported states and unavailable owners retain the existing path.
This changes no shader source, uniform ABI, persistent key or cache capacity.
It remains under the experimental renderer flag, disabled by default.

The source-extracted regression exercises 131,072 available/unavailable,
legacy/projected, unsupported-mode and exact-key cases and five deliberate
faults. Existing job lends, source queues, dynamic vertex pairs and resident
submission controls remain required. Native cold/warm recapture of this
adoption change, full CI and physical-GPU tail comparisons remain pending.
Current cold-first-play smoothness remains **BAD/regressed and unqualified**.


## Unlit controls in the existing startup bank, 2026-11-17

A private cold-boot DOA3 capture identified nine actual draw-scoped compiler
calls, all through module-cache creation: seven literal fixed-function vertex
keys and two fragment keys using fixed-depth shadow comparison. Three vertex
keys are unlit, with fog already register-driven. The new predicate admits
that semantic class through the existing 56 stages and 252 native tuples;
it does not add programs, pipelines, cache capacity or descriptor copies.
Literal texgen coordinates must be supported enums. Live normalization,
specular flags, texgen and texture-matrix enables already upload through the
ordinary CSV0_C/texgen uniform ABI. Keys and persistent schemas stay exact.

Keep the complete original 33,232-case FF domain and its ordinary reference
source in the new separable-GL fixture. Count 816 eligible cases with 1,331,712
exact scalar checks and explicitly count the remaining outside-domain cases.
Require point/skin/source and specular/matrix/fog uniform fault controls. These
tests cover the new unlit class, not all fixed-function rendering. Existing
full vertex, ownership, source-identity and fragment corpora remain required.

The broader private prototype also converted literal fog. It passed native
NVIDIA output checks but failed twelve exact fog channels on llvmpipe in the
[4160,5120) batch. That conversion is rejected. The production predicate still
requires register-driven fog; lit and literal-fog classes retain the correct
existing fallback, and full coverage remains open. Native separable programs
are checked explicitly rather than inferring production precision from
monolithic-only output. No recovery or latency claim follows from numerical
equivalence or fewer uncovered state classes.

## Ordinary fixed-function startup bank experiment, 2026-11-17

`XEMU_EXPERIMENTAL_READY_FF` requires the existing experimental prepared
renderer flag `XEMU_EXPERIMENTAL_READY_VSH`; both remain disabled by default.
The original class requires fixed-function vertices, register-driven fog, CSV0_C
and texgen, and no packed, swizzled or uniform attributes. Seven skin modes,
point parameters, interpolation and the stage-interface prefix require 56
distinct **ordinary unchanged GLSL** sources. No universal lighting rewrite
or new uniform ABI is introduced. Vertex z-perspective does not affect these
sources; its geometry behavior remains in the geometry owner. Persistent
keys, schema and capacities remain unchanged.

The constructor writes directly into 56 final reserved module-key owners.
It resolves their native programs and uniforms, then creates and validates
252 complete VS/GS/FS tuples. These anchors borrow slots from the existing
binding pool and use its native pipeline-owner index. Only completion of
every tuple publishes vertex coverage. Preparation failure releases every
partial native owner and returns all reserved slots; unsupported states keep
the existing correct synchronous fallback. Ordinary borrowers retire before
anchors, which retire before stage owners. Existing pool capacities remain
50*1024 entries; the reservation reduces ordinary available entries instead
of adding a second payload cache.

CPU tests compare 14,336 ordinary source strings, reject unsupported inputs,
check all 56 stages and 252 tuples, exercise 618 partial/unavailable failures
and 50,400 borrow/release operations, and reject eight ownership/publication faults.
Native RTX 5090 and software GL tests each prepare and retire two owner
generations with all 252 actual complete tuples validated. Native tuple
validation is not a numerical draw or gameplay test. Source identity avoids
changing vertex math but cannot establish retail readiness counts, image
equivalence through the integrated renderer or a latency improvement.

A private prior DOA3 seed capture has 19 covered keys out of 49 fixed-function
keys (81 total); these are unique descriptors, not draw or compilation counts.
The other fixed-function classes, unsupported attributes and Vulkan remain
uncovered. First-use source generation still allocates during startup; native
driver allocation/copy behavior remains external. Gameplay allocations,
fresh-process restoration, cold first fights, matched parent/warm tails and
draw-critical compile counts require separate integrated validation. Current
cold-play smoothness remains **BAD/unqualified**.

## Vertex guard dependency repair, 2026-11-17

The CPU worker-partition fixture copied the actual guard after the scheduler
change but omitted its newly extracted `fixture_case_owned` helper, causing
an undefined-reference failure in CI. Compile both actual bodies and apply
the existing lost-slot mutation to whichever body contains the modulus.
Keep all 42,512 original cases, the independent balance/ownership oracle,
worker counts one through four and the older inline-guard path. Clear inherited
round environment variables for this one-round contract; the separate native
multi-round/bitmap contract remains required. No runtime or numerical corpus
changes, timing qualification or cold-play recovery are implied.

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

### Fragment fault-worker scheduling (2026-11-17)

The ten existing fragment negative controls now search the same 1,308 cases in
at most four independent contexts under one shared 300-second watchdog per
control. A completed worker must report its exact range, case and scalar counts;
a failing worker qualifies only with exit 1 and one in-range numerical mismatch
whose old/new values differ. Crashes, malformed witnesses, all-pass runs and
expired watchdogs fail. On a witness or failure, kill and join remaining children.
CPU-only fixtures exercise valid publication, absent/duplicate/out-of-range or
identical-value witnesses, wrong exit, timeout and partial-spawn retirement.
This changes scheduling only, with no renderer or corpus change. Shader/PSO
cold-play recovery remains BAD/unqualified; this is not gameplay timing evidence.

### Dynamic light-range ordering (2026-11-17)

Broad literal-lighting conversion to the existing prepared CSV0_C source failed
exact comparisons on RTX 5090 and llvmpipe, and remains excluded. An additional
private domain retained all original lighting inputs while projecting dynamic
fog onto both reference and candidate. This uncovered a concrete cutoff mismatch:
static local/spot lights enter only when distance <= range; the dynamic loop's
old distance > range rejection admits unordered comparisons. On NVIDIA, simply
inverting <= still produces the wrong NaN result. Explicit isnan checks on both
operands retain the ordered inclusive cutoff.

The independent GPU predicate fixture compiles the actual emitted dynamic guard,
retains the ordinary static predicate, and compares 144 IEEE input pairs against
CPU truth using classification plus less-than/equality. It covers NaNs, infinity,
signed zeros, finite boundaries and small/large finite values: 576 exact channels
per positive path. Five incorrect guards must produce numerical mismatches.
This fixes one semantic prerequisite for prepared lighting; it does not certify
all lit stages, eliminate the four remaining cold misses or prove a speedup.
Shader/PSO cold-play recovery remains BAD/unqualified.

The range-guard fixture passed both actual predicates (1,152 exact channels)
and all five numerical fault controls on RTX 5090 and llvmpipe. The complete
existing 42,512-case GL interpreter corpus retained 69,379,584 comparisons per
GPU. Its nine llvmpipe controls and eight applicable native controls also passed;
the existing literal-fold-specific control remains llvmpipe-only, with LIT-result
required on both. This does not certify the broader active-light conversion:
the supplementary all-material domain still fails exact arithmetic checks.
Default-core cold/warm gameplay, compiler/driver cost and remote CI qualification
remain open for this candidate. No latency improvement is claimed.

The GL binary-cache generator revision advances from specular-coefficients-1 to
ordered-light-range-2, because the ordinary generated stage semantics changed.
Old linked-program and separable-stage binaries reject through the existing
version check; keys, seed recipes and cache format/layout are unchanged. Vulkan
stores no corresponding generated SPIR-V binary cache here; its driver cache
identifies newly supplied shader words by their content.

The direct SPIR-V dynamic-light emitter uses unordered greater-than for rejection,
matching the GLSL NaN guards and static ordered inclusive acceptance. This changes
one comparison opcode, without growing emitted instruction or word counts. Both
backends must change together: retaining the old ordered greater-than opcode would
preserve the same unordered-input bug and break corrected cross-backend checks.

Final normal-build qualification for this correctness fix: the two affected
4,096-case direct-SPIR-V batches passed on both RTX 5090 and llvmpipe, each with
13,369,344 exact scalar checks across both batches, zero differences and zero
validation errors. Restoring the old SPIR-V comparison in a private one-case
control produced 96 numerical differences with validation still enabled.
All 39,056 standalone direct vertex modules passed spirv-val as well.

Fresh-process NGB and DOA3 restores matched parent image bytes exactly in both
OpenGL and Vulkan after rebuilding the default core with the final code.
Eight default-core DOA3 runs covered parent/candidate cold and own-cache
persistent-warm starts on each renderer. Every run preserved 600 fight callbacks,
599 guest Presents/completions and correct audio/video cadence, with zero image
readback allocations. The warm runs used only caches from their own cold runs.
Unrelated native builds were active, so callback timings were deliberately not
analyzed and isolated performance remains unqualified. This is a range/caching
correctness fix, not shader-stutter recovery; the BAD/open recovery status and
experimental shader-bank defaults are unchanged. Remote CI remains pending.

### XXH3 fixture interception after the RetroArch resync (2026-11-17)

The refreshed public XXH3 header defines a function-like dispatch macro. The
pipeline-ahead fixture and five other hash-count/provenance fixtures now undefine
the imported macro immediately before installing their own scoped interceptor.
Their real-hash helpers remain above that boundary, so helper calls still use
the public implementation and the production sections still reach the checked
wrappers. Strict -Werror, case counts, collision checks and fault controls are
unchanged. No libretro-common file or runtime code changes. Cold-play recovery
and its existing failed/unqualified performance status remain open.

### Fixed-depth prepared GL shadows (2026-11-17, experimental)

The prepared fragment source now covers shadow flag 1 on 2D PROJECT2D and
PROJECT3D stages with all eight comparison functions. It samples the existing
RGBA depth alias using sampler2D, honors rectangle normalization, selects D24
before float metadata, and matches ordinary D16/D24/511.9375 depth scaling and
reference clamping. Border adjustment is unused by ordinary shadow sampling and
is cleared from these controls. Unsupported flags, volumes, cube shadows and
invalid compare enums reject before any controls are published. Both native
interpolation programs remain startup-owned; no sampler type, reservation,
persistent cache schema, payload snapshot or default option changes.

The original 1,308-case / 418,560-scalar fragment corpus and all ten faults remain.
A separate independent CPU-truth oracle checks 1,544 shadow probes / 12,352 exact
channels through both ordinary and prepared programs, including every stage,
projection mode, comparison, rectangle coordinates and depth metadata priority.
Three additional faults must expose wrong comparison, projective reference or
rectangle normalization numerically. Existing owner/alias retirement and partial
startup failure tests remain required. This is experimental coverage only:
physical cold/warm gameplay, GPU execution cost, Vulkan and full draw readiness
remain unqualified. The cold-play regression remains BAD/open.

Validation for the fixed-depth shadow change (`c5eba42218`): RTX 5090 and
llvmpipe both passed the original 418,560 exact scalar checks, independent
12,352-channel shadow oracle and all thirteen injected faults. Native and Linux
owner/alias tests retained all eleven failure controls. Two native owner
generations each prepared 56 vertex stages and validated 252 full native tuples.
NGB and DOA3 fresh-process external restores matched parent image bytes exactly.

A private cold DOA3 boot through 600 fight callbacks captured four draw-scoped
CompileShader sites, down from the unlit extension's six. All four raw key hashes
are unchanged lit vertex states; exactly two fixed-depth fragment keys disappeared,
with no new miss keys. The fresh-process persistent-warm run retained one boot
draw compilation. Both runs had zero fight compilation/program/pipeline creation
calls, no C malloc/calloc/realloc/free in those fight callbacks, and advanced 599
guest Presents and completions with correct audio/video cadence. This does not
cover OS/driver allocation: 21,000 array-buffer orphan uploads remain in each
fight sample. Private hooks were removed and clean coherent GL objects rebuilt.

This is work attribution, not a latency improvement or recovery qualification.
Each run used a fresh private app profile; warm persistent caches came only from
its own cold run. Driver cache state is uncontrolled and fight cameras are not
paired. Private stack/key capture and allocator instrumentation alter timing.
Four cold lit-stage misses, GPU shader cost, startup cost, repeatable latency
and Vulkan ready coverage remain open. No default experimental flag was enabled.

### Material lighting with no active lights (2026-11-17, experimental)

The existing 56-stage prepared FF bank now admits literal CSV0_C lighting when
all four material sources are MATERIAL and all eight static lights are OFF.
Live light-mode controls remain excluded because a zeroed key is not proof that
its lightMode uniform is zero. Fog must already be dynamic; literal texgen enums
are validated as before. No generated stage, owner reservation, cache schema,
uniform ABI or default changes. Active and mixed-source lighting retain fallback.

A supplementary dynamic-fog/material/no-light projection is applied to BOTH
ordinary reference and prepared candidate while retaining all original case
bodies and exact comparisons. The 33,232-case domain ledger contains 2,464
eligible cases / 4,021,248 scalar checks; other cases are explicitly outside this
additional class. Lighting-master, material-source and light-mode faults must
produce numerical differences. The original unlit corpus and its five faults
remain required. CPU checks reject every active light slot, dynamic light mode,
each non-material source, invalid texgen and all existing unsupported boundaries.
Broad literal lighting conversion is still rejected, not reclassified as passing.
GPU latency, whole coverage and cold-play recovery remain BAD/unqualified.

For `abacd323e7`, RTX 5090 and llvmpipe passed the supplementary 4,021,248
exact checks and all three faults. The original native unlit corpus retained
1,331,712 exact checks and all five faults. CPU source projection retained
14,336 byte-identical source comparisons on current and historical headers;
owner tests retained eight faults, partial failures and the same reservations.
Historical candidates without the advertised capability retain their original
full domains; advertised coverage with a missing supplementary fixture fails.

The private DOA3 cold-boot trace recorded three draw-critical CompileShader
sites rather than four. Their raw key hashes are the same three active-lit
states as the parent; exactly one static all-OFF lit key disappeared, with no
new key. Cold draw-pipeline creations fell from six to five. A fresh-process
persistent-warm run retained one draw compilation and one draw-pipeline creation.
Both runs preserved 600 fight callbacks / 599 guest Presents and completions,
correct audio/video cadence, and zero fight shader/program/pipeline creation
or C malloc/calloc/realloc/free. The 21,000 array-buffer orphan uploads remain.
NGB and DOA3 external restores matched parent image bytes exactly. Private hooks
were removed and coherent GL objects rebuilt before the image checks.

These are correctness and work-count results only. Software-GPU validation ran
concurrently, private instrumentation changes timing, driver-cache state is not
controlled, and fight cameras are not paired. No frame-time or global allocation
claim follows. Three cold active-lit misses, GPU cost, cold/warm latency recovery
and Vulkan coverage remain open; experimental defaults remain disabled.

### Literal material-lighting startup family (2026-11-17, experimental)

Keeping the lighting controls literal avoids the exact arithmetic differences
from converting material selectors into uniform-controlled selects. The new
family uses the ordinary generator unchanged: lighting on; normalization, local
eye and two-sided lighting off; all four color sources material; specular,
separate specular and ignore-specular-alpha on. Light modes and texgen remain
register-driven, and fog must already be dynamic. All seven skin modes, both
point-parameter modes, interpolation variants and interface prefixes are
prepared. Other control settings retain the correct existing path.

The two families now occupy 112 stage entries and 504 native tuple entries
borrowed from the existing 50*1024 module and binding pools. Pool capacities,
cache keys, generator revision, uniform ABI and experimental defaults do not
change. Every tuple must validate before either family is published; partial
startup failure returns every borrowed slot. Existing generic owners retain
priority for states they already cover. Queue and owner retirement rules are
unchanged; no new runtime payload copies or worker waits are introduced.

Before integration, independent supplementary probes retained all original
33,232 FF case bodies while projecting the admitted literal controls and
dynamic fog onto both reference and candidate. NVIDIA and llvmpipe each passed
2,464 eligible recipes and 4,021,248 exact scalar checks. Converting those same
controls into the older generic CSV source instead produced 3,171 NVIDIA
differences and was rejected. No thresholds or original corpora were changed.

The integrated fixture uses the actual classifier and canonical initializer,
independent light/control packing and required numerical faults. Owner tests
cover every stage creation/link and tuple creation/validation failure, both
unavailable prerequisites, repeated borrowing and teardown. Native tuple,
current-corpus, cold/warm gameplay, fresh-process restore and latency
qualification must finish before delivery. The broader shader/PSO cold-play
regression remains BAD/open; Vulkan and other lighting controls remain open.

Initial integrated native checks passed: 28,672 source comparisons across
112 distinct ordinary sources; 1,234 partial/unavailable rollback cases; two
owner generations validating and retiring all 504 tuples each; and 4,021,248
exact numerical channels with eight required faults rejected. The eye fault
uses original unsaturated-curve cases 20016:20032; the later constant-curve
interval did not exercise eye dependence and is not accepted as a witness.
Integrated software-GPU and gameplay qualification remain pending.

### Dynamic CSV with literal texgen (2026-11-17, experimental)

After the literal material family eliminated the three observed GL draw
compilations, a private cold DOA3 run still constructed two pipelines in draws:
one during boot and one at fight entry before the counted live callbacks.
Both had ready geometry and fragment stages, but their FF vertex recipes had
dynamic CSV controls and literal texgen. The old eligibility predicate required
both CSV and texgen to be dynamic, so these vertices used prewarmed exact modules
and then constructed a native tuple in the draw. The diagnostic ff field means
fixed-function, not readiness; a stage-module hit alone does not prove a ready
complete tuple. This was a coverage gap, not evidence to alter publication
gates or ignore the two calls as startup.

The existing register-driven family now accepts valid literal texgen/matrix
controls with independently packed uniforms. Invalid literal modes still reject.
No program, tuple, reservation, key, generator revision or uniform ABI is added:
the bank remains 112 ordinary stages and 504 fully validated tuples, with the
experimental flags off by default. Literal fog remains outside this conversion.

The supplementary corpus retains all original FF case bodies and the original
raw CSV/light inputs, projects dynamic fog onto both paths, and independently
packs the literal texgen controls. Private NVIDIA and llvmpipe probes each
passed 400 new-domain recipes and 652,800 exact scalar comparisons. The
integrated fixture uses the real classifier and requires numerical faults for
point parameters, skinning, CSV, light modes, texgen and matrix enable. Original
corpora, budgets and all older fault controls remain required. Integrated GPU,
owner/lifecycle, cold/warm gameplay and latency qualification are still pending;
shader/PSO cold-play recovery remains BAD/open.

The integrated native classifier fixture passed all 652,800 positive channels
and all six numerical faults. The source identity/unsupported-boundary and
1,234-case owner rollback tests passed as well. Integrated software-GPU,
cold/warm gameplay and performance qualification remain pending.
