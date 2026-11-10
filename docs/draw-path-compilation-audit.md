# Draw-path compilation audit

Initial source audit, 2026-11-09, based on 101e59c875 plus bounded pre-raster
module preparation. Status: **not compliant; cold play remains regressed**. This is a
source inventory, not measured event attribution or a complete call-graph
proof. A worker queue or resident shader module does not prove PSO readiness.

| Path | Finding | Work remaining |
| --- | --- | --- |
| GL `texture.c`: compressed-volume upload | The fixed decoder formerly compiled/linked on first use. It now initializes before texture uploads; unsupported or failed initialization uses the existing exact CPU decoder. Native VTC skips unused preparation. | Keep GPU value, failure, owner and no-upload-creation checks in CI. This does not address general shader misses. |
| GL `shaders.c`: `pgraph_gl_bind_shaders` | A missing ready dynamic sibling still reaches `generate_shaders` for the real binding. Separable module generation and monolithic linking can therefore remain synchronous. | Establish complete correct ready sibling coverage before removing the synchronous correct path. Include fixed/programmed vertex and geometry variants, not only fragment readiness. |
| GL `shaders.c`: flip/prewarm service | Preparation can still perform driver work on the rendering/frontend critical path. Moving work out of the bind function alone is insufficient. | Correlate creation and completion-status queries with callback IDs; distinguish source generation, driver creation and adoption. |
| Vulkan seed pump | Available recipes now queue complete PSOs, with or without GPL, when all register/input dimensions are dynamic. Keys live in existing job slots and modules remain referenced until worker completion. | Recipes-only coverage is distinct from empty first-ever startup. The embedded dictionary currently has 14 Chihiro titles and no DOA3 entry. Unknown states and newly encountered render-pass combinations still need ready fallback coverage. Module adoption on the rendering thread also remains to audit. |
| Vulkan `draw.c`: `create_pipeline` | A miss can call `vkCreateGraphicsPipelines` directly. With GPL, missing halves are created by `gpl_get_pre`/`gpl_get_frag`, then the final PSO is linked synchronously. | Ready full-PSO coverage is required in both GPL and no-GPL configurations. Ready halves do not satisfy the rule. |
| Vulkan upload-time prewarm | Vertex/geometry compilation uses eight immutable request owners on the existing shader worker; only request pointers cross its handoff. The render thread adopts ready words through a scoped cache initializer without a CPU-compiler fallback, restores the ordinary initializer, and queues library creation on the pipeline worker. Saturation defers optional preparation; cancellation releases owners after native quiescence. | Host shader-module creation/adoption still runs on the rendering thread. A draw can arrive before its library is ready and use its existing synchronous correct path; this is not unknown-state fallback coverage or a no-stutter claim. |
| Vulkan `draw.c`: `create_clear_pipeline` | Full-color and depth/stencil attachment clears now select their render pass without creating/binding a PSO. Partial-channel color clears still use a shaded triangle and can create a pipeline. | Keep first-clear/query/slot/following-draw tests and GPU color/depth/stencil checks. Build correct ready partial-clear coverage next; do not skip a clear on a miss. |
| Vulkan `display.c` / `pvideo.c` | Display and PVIDEO conversion pipelines are created from display initialization. | Preserve initialization ordering and device-owner lifetime; verify no later recreation through event counters. |
| Vulkan `surface-compute.c` / `unswizzle.c` | Depth pack/unpack variants and swizzle/depth-swizzle pipelines are created from their initialization functions. | Preserve ordering and retirement; initialization placement alone is not whole-core compliance. |

The next substantial target is the uncovered dynamic sibling/full-PSO path.
Inventory its remaining static dimensions and demonstrate identical output
before moving fields into uniforms or dynamic state. Do not replace it with
an approximate compute renderer, guessed pipeline, skipped draw, draw-side
queue wait or unbounded boot precompile set. Any new worker job must own its
immutable inputs until completion and publish results to the cache owner.

Upload requests own at most two exact module keys and one compact raster/layout
snapshot in each of eight stable slots (4,960 bytes per slot on the tested
64-bit builds, plus eight request pointers). Keys are constructed directly in
those slots; the handoff copies only pointers. Existing memo/module-cache key
copies remain necessary persistent owners. Up to sixteen referenced word or
module owners can remain pending; there is no second SPIR-V/geometry buffer.
The existing compiler worker, full-state queues and pipeline queue retain
their thread counts and capacities; compiler startup can now occur at upload.

Pointer publication is release/acquire. The worker's READY publication ends
its final key/metadata reads, and the render thread then adopts word references
through a scoped initializer and restores the ordinary initializer. Pipeline
backpressure retains module references in ADOPTED slots without recompiling or
waiting. The native PGRAPH owner is cleared before quiescence drains results;
request cleanup follows the existing inflight wait, so no old device generation
or queue lend survives teardown. Counter rollover and immediate slot reuse are
covered independently of gameplay. Host module creation/adoption remains on
the render thread; unknown draw misses still have their synchronous correct path.

GPL library module identities are monotonically assigned by the render-thread
cache owner, rather than derived from opaque driver handles. Handle values are
not a persistent module identity ([Khronos object model](https://docs.vulkan.org/spec/latest/chapters/fundamentals.html#fundamentals-objectmodel)).
Module destruction therefore cannot give a replacement module the identity of
an older library. The counter belongs to the renderer; its library tables are
destroyed before that renderer is released. This changes only renderer-local
library lookup identities, so persistent shader/PSO formats need no version bump.
Module metadata gains one 64-bit ID and the renderer gains one counter; module
allocation counts, shader payload storage and reference lifetimes are unchanged.
An identical shader recreated as a new module also receives a fresh ID and may
need another library. Safely reusing equivalent recreations remains an open
optimization; opaque-handle aliasing cannot supply that reuse correctly.

Use diagnostic builds to count creation/link and compile-wait events by kind
and callback, including utility and flip work. Release builds must contain
none of those diagnostics. Pair separate image/lifecycle runs with clean
cold/warm timing runs. Keep fresh-process restore tests distinct. Uncontrolled
driver caches and the missing accepted cold-play reference remain unqualified;
none of this inventory establishes recovery or a frame-time improvement.

GPL digest matches now require exact canonical metadata equality in both
lookup paths, pending-upload deduplication and worker-result adoption. Stack
keys are borrowed only during synchronous lookup/store; queued keys are built
directly in the existing final job union and remain owned until publication
and adoption. Cache growth borrows no entry pointers: indices survive moving
arrays. Each accepted library retains one indispensable metadata snapshot in
its final array owner, released after pipeline-worker quiescence. No shader,
geometry, SPIR-V or file payload is cloned.

On the tested 64-bit ABI, entries occupy 592 bytes for static pre-raster input,
136 for dynamic input and 184 for fragments (including the 16-byte header).
The dynamic prefix omits 456 bytes proven zero by canonical construction.
This increases metadata memory and adds exact comparisons to library lookup;
it is a collision-correctness tradeoff, not an established latency win. Full
ready-PSO hits bypass this lookup. The job union reuses existing key storage;
alignment may change its total size. Persistent cache schemas are unchanged.

Graphics pipeline layouts now have one renderer-generation owner: the empty
clear layout and one exact descriptor/push-range layout per possible uniform
attribute count. Initialization creates two layouts when push constants are
unavailable, otherwise eighteen (0 through 16 vec4 attributes plus clear).
The 64-bit handle table costs 144 bytes; this bounded initialization work is
not PSO compilation or speculative shader precompilation. All create infos
match the former per-use layout shapes, including an empty descriptor-free
clear layout. No key/schema or emitted shader changes are required.

Draw jobs, queued jobs and cached bindings borrow immutable layout handles.
Queue completion/cancellation and cache eviction no longer destroy those
shared owners. Pipeline jobs quiesce, pipeline cache entries retire, native
GPU slots settle and GPL libraries are released before the layout table is
destroyed; shader teardown releases the descriptor-set layout afterward.
No payload snapshots, additional resource references, queues, look-up
allocations or new waits are introduced. This removes draw/upload-time
vkCreatePipelineLayout calls; uncovered vkCreateGraphicsPipelines and final
GPL linking remain synchronous violations. Cold-play recovery stays open.

Upload-prewarm submission now coalesces identical requests while an existing
slot owns the same immutable pre-raster keys and pipeline snapshot. Hashes
filter comparisons; complete key bytes, count and complete pipeline metadata
must match. The candidate constructs keys directly in a free stable slot,
then borrows the caller's pipeline only through synchronous comparison.
Duplicates take no second pipeline snapshot, queue publication, compiler-memo
lookup or adoption. Different render passes/raster/input/dynamic/push-range
state stay separate. Byte equality is deliberately conservative: padding or
unused-field differences can miss a coalescing opportunity, never justify
reusing distinct work. Pending keys and snapshots are immutable through their
last worker/adoption consumer; no source, word or module pointer is borrowed
from another slot. The existing native owner/generation boundary and bounded
queue capacity are unchanged. A full queue still defers optional work before
constructing keys, even for a duplicate; no wait or spin is introduced.

This does not move vkCreateShaderModule off the render thread or cover an
unknown PSO. Blind native-module creation on the compiler worker would also
create objects for cache hits; eliminating that duplication and preserving
native module-cache ownership is a prerequisite for a safe future handoff.

Upload preparation now probes the render-thread module LRU by hash and exact
full keys without invoking its initializer/compiler. Entry pointers are
borrowed only during the scan; all required modules are pinned after every
match is proven, then passed directly to native library preparation. Partial,
null or colliding entries acquire nothing and keep the existing worker miss
path. Ready hits and available pipeline slots need no new pipeline snapshot or
CPU compiler job. Pipeline pressure retains references and one bounded state
snapshot in the existing ADOPTED request slot, safe across cache eviction.
Request sizes, input callbacks, shader generation and cache schemas are
unchanged. Canonical key construction still has its existing bounded input
snapshots; this does not claim a wholly zero-copy input path.

The process-global PGRAPH program digest gate remains an open readiness issue:
it cannot prove acceptance or readiness in a new renderer or metadata context.
An experiment removed it but caused tens of thousands of additional state/key
snapshots and optional library lookups. DOA3 and Crazy Taxi 3 preparation jobs
increased and CT3 had a slower cold capture; those uncontrolled results remain
unqualified and retained, not a performance pass. The delivered resident-reuse
change preserves callback frequency. Retry/generation repair needs a cheaper
input-lend/canonical-key design before broadening preparation coverage.

The retained JSRF GPL-enabled count captures reduce CPU preparation queue
publications from 59 to 8 with fresh core caches and 59 to 7 warm, with the
same 59 upload callbacks. Pipeline snapshots fall from 12272 to 1664 bytes
in each capture (the warm case includes one resident pressure snapshot).
These are preparation/snapshot counts, not actual compiler invocations or
a total-copy/latency win. DOA3 and CT3 did not show the same cold job reduction.
Parent and candidate both exhibit large uncontrolled first-use hitches. An
opt-in matched-harness probe measured the new lookup below 2 microseconds,
and a parent draw-path fragment-library creation span of 261 milliseconds;
not every callback hitch is explained or attributed between compilation,
driver cache I/O and scheduling. Remaining synchronous misses stay open.

Pipeline misses now have an additional exact-ready sibling path, including
already-seen shader states whose new render-pass/static key has no PSO. The
shader-hash seen/LANDED marker is not treated as proof for another draw key.
Only an already-initialized complete ShaderBinding and complete exact PSO
may be selected; readiness probes never invoke a cache initializer. Missing
bindings, missing PSOs, partial native modules and collisions keep the old
correct synchronous miss. This does not establish unknown-state coverage.

Projection borrows the vertex/geometry prefix in the existing draw key and
snapshots/restores only fragment metadata (two 264-byte PSH values on the
tested ABI). On success, an optional full job snapshots the 3116-byte exact
PipelineKey once into its existing final slot and pins native modules before
any cache selection. This is an indispensable queued identity snapshot,
including its program metadata, not a claim of wholly zero-copy keys; no
SPIR-V or geometry resource payload is cloned. A full queue or matching
in-flight job takes no additional key snapshot and never waits. Cache nodes
can be evicted while the job owns its key/modules. Ready cache-hit promotion
and uniform refresh finish before descriptor upload and actual draw binding.

The existing host program_data_dirty flag requests real shader-state capture
on the next draw after fallback. This retries queue saturation and adopts the
exact completed PSO even when no guest register is written; it avoids retaining
a recyclable cache-node pointer between draws. It can repeat bounded input
state construction while specialization is pending, an explicit CPU/copy cost
to measure against removed synchronous pipeline work. No new persistent retry
storage or steady ready-hit branch is added. Remaining first-use, utility,
missing-sibling and other synchronous creations/links remain open violations.

An empty full-key reservation may outlive the draw while a worker owns its
independent immutable key. Such a cache entry has never been bound: eviction
must not apply an in-use GPU-pipeline assertion or enqueue empty GPU trash.
Actual pipelines retain the native fence/slot retirement path and shared
layouts remain renderer-owned. This distinction is covered in the recording
and delayed-retirement regression fixture.

Initial DOA3 first-fight, JSRF attract and Crazy Taxi 3 attract Vulkan cold/warm
repeats did not select this new path: none had the required resident sibling
ShaderBinding. The deterministic fixture exercises selection, queue saturation,
owner eviction and completed adoption, but those game runs do not establish a
reduction in draw-critical PSO work. Ready-PSO coverage without a materialized
shader binding remains a separate open gap. Driver caches were uncontrolled and
RetroArch was running during this correctness batch; its timings are unqualified.

A missing binding can now be materialized after the exact complete fallback
PSO is proven ready. Its current vertex/geometry prefix and geometry UI-clamp
input must match, and its fragment module must be an exact native-cache hit.
No module initializer, compiler, queue drain or wait runs here. All module
references are acquired before a typed native-LRU initializer publishes the
binding and prepares its uniform locations. An existing incomplete exact entry
is rejected, avoiding an unconsumed-reference leak on a second cache lookup.
A full shader cache defers this optional work rather than evicting the active
binding; ordinary ready binding hits still work with a full cache.

The synchronous input is a 32-byte pointer lend on the tested ABI, consumed
before return. The final existing cache entry owns one 2620-byte ShaderState
snapshot. Its one-off 2348-byte fragment lookup key includes canonical zero
union padding and a 264-byte fragment-state copy; no vertex program, SPIR-V,
geometry data or file payload is cloned. Vertex/geometry module owners are
borrowed only during lookup and then referenced through native cache lifetime.
The complete PSO is probed before materializing any binding. The native-LRU
fixture checks publication, source poisoning, module-cache eviction, active
binding eviction, partial entries, queue-independent capacity and collisions.

The next diagnostic separately probes the exact projected PSO before asking
for a binding. DOA3 first fights, JSRF attract and Crazy Taxi 3 attract all had
zero exact fallback PSOs ready at their cold/warm full-PSO misses. This refines
the preceding observation: missing bindings were not the only coverage gap.
Materialization therefore has native-LRU fixture coverage but no naturally
selected fallback in these initial six game runs. Compiler/PSO work reduction,
rendered-image equivalence of a new selection and stutter recovery are not
established by them. Their timings also overlapped contract tests and a release
rebuild; driver-cache state remains uncontrolled. Complete fallback scheduling
for the actual encountered render-pass/static keys remains open.

Further diagnostics found a specific render-pass gap: the flip-time pump
only prepared the attachments left bound at the flip. In a repeated DOA3 run,
the cache contained exact dynamic sibling shader states for later color-only
draws, but only their depth-attached PSOs. The native render-pass table already
owned both identities. Preparation now selects one of those existing owners
round-robin each flip, within the unchanged four-job/32-check budget and
four-pass progress capacity. No draw-side scan, shader snapshot, new queue or
extra reservation is added. A four-byte renderer cursor and the existing local
eight-byte format snapshot identify the next pass; the native-array lend ends
before job filling can touch that array. Jobs retain their existing final-slot
keys, module references and renderer-owned render-pass/layout lends.
The cursor fills existing alignment padding on the tested 64-bit ABI: the
progress owner remains 128 bytes. The native-header fixture checks this size,
as well as empty-bank/no-option work, delayed readiness, queue pressure,
relocated owners, four-pass capacity and independent color/depth inputs.

Pipeline job filling now derives depth-state presence and color attachment
count from that job's exact render-pass key. Reading the currently bound
surfaces would incorrectly configure a job for a previously encountered pass.
All other static-state prerequisites remain unchanged: this seed service still
requires the existing fully dynamic register/vertex-input coverage. An empty
state bank queues nothing; completion remains independent of seed eligibility.

In the candidate's second warm DOA3 repeat, 676 draws selected the ready sibling
while one specialization was queued asynchronously. These are 676 draws served
by one ready path, not 676 avoided compilations. The parent selected none in
its two warm repeats. Full draw-creation counts were 15/14 for the parent and
13/10 for the candidate; differing encountered/learned states and uncontrolled
driver caches prevent treating that difference as a controlled latency result.
The initial diagnostic/count runs also overlapped CPU contract tests/builds.
Cold first play remains open: a genuinely empty bank still has no known states
to prepare. The existing retry dirty flag can repeat bounded shader-state
capture while the one job is pending; that cost is retained, not labelled
zero-copy. No shader-word, geometry or file payload is copied by this change.

Pending sibling retries can now borrow a full shader identity from their
published native worker slot instead of reconstructing ShaderState on every
unchanged draw. The renderer retains only an unsigned ticket, family-generation
snapshot and retry flag. Before reading the slot, validate optimization enable,
generation and the unsigned [drained, published) interval, including counter
wrap; reject non-full jobs. Worker completion does not modify keys, but render
thread draining/reuse ends this lend. The last CPU consumers are the existing
guest dirty-state guard and an exact non-initializing shader-binding probe.
Cache promotion uses that ready binding's own state, never the ring pointer.
No initializer, drain, wait, ShaderState snapshot or payload handoff occurs in
the lend selection. Native module references and final-slot job keys continue
to cover their existing worker/GPU consumers.

Guest writes, a changed family generation, disabled optimization, eviction,
completion/reuse or missing owners take the ordinary real-state capture path.
Full-queue and externally queued specialization cases retain the earlier retry
path. Uniform refresh still runs after shader selection. Flush invalidates both
the host lend and host shader-binding view, rather than depending on a serialized
guest dirty bit a subsequent load can restore to false. This requires one real
shader re-selection after a flush and does not retain guest or cache-node pointers.
The inactive render path adds a predicted-false retry check; this is not claimed
as literally zero instruction overhead or as a whole-renderer zero-copy path.

The initial second warm DOA3 candidate repeat reported 675 successful lends,
676 ready-fallback draws and one queued specialization; the parent also served
676 fallback draws. Selection thus avoided 675 whole-state captures, accounting
for 1,768,500 bytes of captured ShaderState metadata on the tested ABI, without
replacing them with another payload clone. One retry used the conservative
recapture path; the aggregate probe does not attribute its invalidation cause.
The ordinary draw-key snapshot and other guest-driven captures still remain.
These work counts came before the final flush invalidation refinement; that
refinement does not change the scoped selection and receives separate runtime
lifecycle checks. Driver caches and guest state sets are uncontrolled, so these
counts do not establish a frame-time or cold-play recovery claim.

The new fixture runs the production dirty-state guard across 21 guest-input
classes, ring expiry/reuse/wrap, generation/feature changes, exact collisions,
poisoning the ring after the final query, and restored-false guest dirty bits.
Its sanitizer caught two pre-existing signed bit-31 expressions used by the
guard. Their unsigned literals preserve the 32-bit register values and key
schema; sanitizer suppression is not used. Remaining synchronous misses and
fresh-process/image qualification gaps stay open.

The shared shader dirty guard now compares CONTROL_0/CONTROL_3 writes on
their shader-consumed fields instead of treating every write as a new shader
identity. Depth-function/write and color-write changes still reach their normal
pipeline/dynamic-state paths; they no longer require constructing an identical
ShaderState. Keep alpha enable/function/reference, Z perspective, fog enable/mode,
shade mode and provoking vertex. Keep ZENABLE because it selects the upscaled
UI-clamp family. No ShaderState/PipelineKey field or key/schema version changes.

Both renderers also flag CONTROL_0 when crossing native/upscaled resolution
without changing the register. A separate native/upscaled class snapshot keeps
that host-input invalidation. The existing nine-word host snapshot becomes
twelve words: two masked controls and that class, twelve added bytes per device.
The snapshot remains owned by PGRAPH, is not migrated or queued, and does not
clone shader/geometry/file payloads. It adds three scalar snapshots at the
existing register-map clear; no allocation, mapping or resource ownership changes.
Legacy dynamic siblings retain whole-register write wakeups until every async
completion route has independent retry scheduling. Pending native worker-key
lends keep their existing exact binding, generation and guest-input guards.

The regression oracle extracts consumed VSH, geometry, fragment and UI-selector
expressions, audits control dependencies, and exercises all register bits across
randomized values. It covers unchanged writes, relevant changes, native/upscaled
transitions and retained legacy-sibling wakeups. In the final guarded cold Vulkan
DOA3 first-fight run, no pending key lends were active and 80,402 checks returned
clean despite control writes. Those calls avoid the old whole-register-triggered
2,620-byte capture path: 210,653,240 bytes of captured-state output by ABI size.
This is work accounting, not measured bus traffic or a frame-time claim. Ordinary
guest program/texture changes and remaining shader-driven captures still occur.
Driver caches, varying encountered states and overlapping private CPU diagnostics
leave latency and cold-play recovery unqualified. The transient unguarded build
and its profiles are retained separately from final guarded runtime evidence.

GL flush now also invalidates its borrowed host shader-binding view, matching
the Vulkan load invalidation. Uniform-source invalidation alone did not ensure
restored program words selected a fresh shader. Cached programs retain their
native owners; no object is deleted or copied by clearing this view. The next
draw reselects actual restored state without waiting for an unrelated control
write. This GL-only refinement receives separate three-game lifecycle checks
and a final GL image comparison; the Vulkan rendering path is unchanged.

### Redundant OpenGL register-map reset

OpenGL's shader-selection path cleared the dirty-register map twice: once
after installing the selected cache binding and once after binding its host
program/pipeline. Keep the first reset, including its consumed-register
snapshot, and remove the second. The intervening statements only update a
profile counter and call `glUseProgram`/`glBindProgramPipeline`; none writes
guest PGRAPH registers. The uniform update remains after the reset and still
runs on the unchanged-binding fast path. Vulkan is byte-identical.

Each selection now avoids a second 256-byte bitmap clear and twelve scalar
snapshot stores/associated register reads. This is deterministic work removal,
not a measured bandwidth or frame-time gain. No shader/pipeline keys, snapshots
retained by queues, cache ownership, allocations, file I/O or borrowed lifetimes
change. Unknown draw-path synchronous compilation remains an open violation;
this cleanup does not establish cold-play recovery.

Validation: 42 contract suites, a clean Windows release rebuild and eighteen
serial runtime runs completed. DOA3 includes parent/candidate cold/warm first
fights on both renderers; JSRF and Crazy Taxi 3 include candidate cold/warm
attract runs on both. Eight lifecycle cases passed repeated RAM-exact loads,
reset and unload. Timing runs performed no image readbacks. The separate
OpenGL parent/candidate first-fight images were byte-identical. Driver caches
remain uncontrolled, fresh-process restoration remains untested and no
latency or cold-play recovery claim follows from these results.

Ninja Gaiden Black slot 0 adds twelve parent/candidate runs: each renderer
gets fresh-process restoration with empty and populated core caches plus a
separate scene capture. Both restores succeeded per run, exact restored-phase
audio cadence passed, and parent/candidate images were byte-identical on
each renderer. The fixture's RZIP-v2 data was decoded in bounded 128-KiB
chunks; a read-only mapping owns the validated MEM block through repeated
unserialize and is unmapped only after its last consumer. No second full
state payload buffer is made. No fixture RAM sidecar was available, so
external-state RAM equivalence remains unqualified. The initial harness
audio-phase mismatch is retained separately; the final harness reads the
serialized `libretro-frame` phase without relaxing cadence checks. These
loaded-state runs do not replace first-boot qualification.

### Completed OpenGL job versus resident demand stage

Demand can populate a stage while the CPU generation worker still owns its
job. Previously the completed job unconditionally submitted another driver
compile/link before discovering the resident module during adoption. Query
the exact native module-cache key on the render thread before submission.
Only a non-null separable program with resolved locations is ready; pending
links, empty entries and hash collisions do not qualify. This query does not
poll GL status, wait, evict or acquire a new resource owner. A ready stage
receives the same gate/specialization/vertex adoption notification as a
completed link; seeds still do not switch live families.

The acquired COMPLETE job slot owns its immutable key and MString until
notification, or until GL consumes the source and the asynchronous slot
owns its final key. Only then unref the source and retire the job. The old
temporary full-key snapshot disappears: a ready reuse copies no key, and a
new link copies once directly to its final existing async slot. No pointer
to the recycled job escapes. No new storage, file I/O, reservations or
queue capacity is added. The extra exact-cache probe is per completed
preparation job, not per draw. Report source-level copy removal separately
from actual memory traffic and frame latency.

The regression fixture compiles the production cache predicate, adoption
and submission helpers with actual module-key/job/async record layouts.
It exercises all job and destination slots, stage kinds, gates, seeds,
empty sources, pending/null modules and forced hash collisions. Callbacks
require the source job to remain owned through the last consumer; retiring
a job poisons its key to check the destination's independent final owner.
This removes redundant preparation work, not all unknown first-use draw
compilation. The cold-play regression remains open.

Validation completed 43 contract suites and 30 serial runtime runs across
DOA3 first fights, JSRF/Crazy Taxi 3 attract sequences and NGB slot 0, with
both renderers and separate image checks. Eight main-corpus lifecycle cases
passed RAM-exact repeated loads/reset; NGB adds twelve fresh-process
restore/repeat/image cases without a fixture RAM sidecar. DOA3's OpenGL
parent/candidate images and NGB's per-renderer images were byte-identical.
Across the eight non-image OpenGL candidate runs, 430 completed jobs reused
82 exact ready stages and submitted 348 new links. The private probe counts
only nonempty source reuse as an avoided link. These are observed reuse
counts, not a raw parent/candidate total difference or a latency claim.
No image readbacks were used in timing runs. Driver caches remain
uncontrolled, and cold-play recovery stays unqualified.

### Flip-time OpenGL specialization readiness and local wakeup

When a correct dynamic sibling draws, lend the existing uninitialized exact
shader-cache entry to a one-recipe flip-time service. The native cache owns
the immutable ShaderState; eviction clears the lend before zeroing/recycling
its slot. Load/uniform-source invalidation and a fresh shader selection also
cancel it. Record the family generation in existing binding metadata and
reject changed generations, wide-line inputs or disabled SSO/first-meet mode.
Renderer teardown uses native cache flushing; no queue retains the pointer.

The service checks exact geometry/vertex/fragment module keys, including
geometry prefix/UI-clamp inputs. Only non-null programs with completed links
and resolved locations qualify. It performs no status query, compile, link,
cache-miss generation, file I/O or compile wait. Both caches belong to the
render thread, so resident lookups cannot evict stages before the existing
pipeline setup consumes them. Restore the live pipeline after setup, then
invalidate only that live binding's existing generation stamp. The next draw
recaptures actual guest state through the existing guard even if no register
changed; it never blindly adopts the prepared recipe after a guest change.
Unknown/uncovered states keep their correct synchronous path and remain
explicit violations of the no-draw-compilation target.

Cost and ownership: one eight-byte borrowed host pointer, no new heap
allocation, retained ShaderState/microcode snapshot, payload clone or queue
reservation. Pending recipes use 7,044 bytes of temporary native query keys
on the stack; existing pipeline setup constructs its own bounded keys when
ready. At most one exact binding is prepared per flip. With no recipe, only
the pointer guard runs; the draw fast path gains no extra readiness branch
or global generation invalidation. Existing GL pipeline-object creation
moves between frames for covered recipes. Driver/host allocation behavior
is not eliminated or claimed allocation-free; an unused speculative object
can remain under the existing cache owner. This remains open work under the
no-mid-game-allocation target, not permission to hide costs by moving them.

The regression fixture executes the production pump, module-key construction,
exact readiness predicate and load/eviction callbacks with the native LRU.
It checks poisoned recycled slots, load cancellation, generation wrap,
host-option/geometry changes, incomplete/null/colliding stages, live-pipeline
restoration, one-time localized wakeup and absence of direct allocation or
wait calls in the pump. Runtime diagnostics must separately count covered
flip creations, remaining draw creations, pending-link demands and module
cache misses during preparation; driver-cold stutter recovery remains open.

Validation completed 44 contract suites, final Windows/Linux readiness and
load guards, and thirty serial harness runs across DOA3, JSRF, Crazy Taxi 3
and NGB slot 0 on both renderers. Eight main lifecycle cases passed repeated
RAM-exact loads/reset; NGB repeated external restores retain their missing
RAM-sidecar qualification. DOA3 OpenGL and NGB per-renderer parent/candidate
images were byte-identical. Eight non-image OpenGL candidate runs prepared
20 pipeline objects between frames, with zero pending-link demands and zero
module-cache initializations inside that preparation path. Those runs still
recorded 791 draw-path pipeline-object creations and 277 pending-link demands;
the target is not met. Counts describe observed API work/placement, not proof
that every prepared object was subsequently used or that driver first-use
compilation is eliminated. An external RetroArch process was present during
the batch, and driver caches were uncontrolled. Timings are retained as
unqualified; no isolation, latency, allocation-free or stutter-recovery pass
is claimed.

### Queued dynamic cover recipes share the existing flip-time lend

Previously only an exact specialization behind an initialized dynamic
fallback entered the flip-time binding service. A queued, uninitialized
dynamic cover itself had no recipe lend; its stages could land between
frames and still require draw-path pipeline setup on its next encounter.
Track that cover's existing native cache entry while the correct exact
shader draws. When all exact stage keys are ready, the same service can
prepare it before a future miss needs it. Keep the existing synchronous
correctness paths for uncovered states, including the immediate ready-cover
path; this does not remove the only available correct draw path.

Both recipe types share one pointer and the same at-most-one-per-flip
budget. No host field, retained state snapshot, compiler job, source clone,
queue capacity or explicit core allocation is added. Replacing the current
lend ends that borrow without releasing its native owner. Existing
eviction/load/mode/generation/geometry cancellation and exact readiness
checks apply unchanged. More recipes can now consume the existing bounded
query and GL object-creation budget; this is neither zero CPU work nor
allocation-free driver behavior, and unused prepared objects can remain
under the native cache owner.

Only an exact specialization requests a localized real-state reselection.
A prepared cover does not invalidate an already-correct live exact shader;
future shader changes discover the cover through normal exact-key lookup.
This avoids introducing a redundant whole ShaderState capture merely to
announce cover readiness. The lifetime fixture now exercises both static
and dynamic recipes through the production tracking helper, including lost
generation/owner controls and an unnecessary-cover-wakeup negative control.
Keep placement/reuse counts separate from latency and driver-cache claims.

Validation completed 44 contract suites and the expanded 7,680-case
Windows/Linux lifetime/readiness fixture. The main matrix completed 31
runs (including one retained DOA3 image repeat). Eight non-image GL
candidate runs prepared ten queued covers and twenty exact specializations;
preparation recorded no pending-link demand or module-cache initialization.
The same runs still recorded 778 draw-path pipeline-object creations and
277 pending-link demands. These observed counts do not prove latency or
that every prepared object was subsequently used.

NGB per-renderer images matched exactly. Both DOA3 cold-boot candidate
captures differed from the parent by 2,834,883 bytes; visible boat/bird
positions differ, but no cause is established. Preserve both mismatches
as unqualified. Supplementary attempts exposed an old, misleadingly named
"first fight" fixture that actually showed the title screen; exclude it
from fight qualification. Two supplementary Vulkan-labelled invocations
used GL due to an incorrectly passed harness switch, and one corrected
Vulkan parent attempt lacked a capture. All remain retained/unqualified.

A replacement checkpoint was saved from the live-fight marker at live
frame 30 (state 1, scene 10, phase 1) and visually verified. Its four
fresh-process parent/candidate restores produced byte-identical fight
images on both GL and Vulkan. Capture serialized directly into the final
146,290,728-byte API buffer without a second RAM-sidecar snapshot; restores
borrowed a read-only file mapping through their last unserialize consumer.
No external-checkpoint RAM-equivalence claim is made without a sidecar.
Those matched loaded-state images do not erase the cold-boot mismatches.
Driver caches remain uncontrolled. Process censuses saw no RetroArch at
batch boundaries, but do not prove continuous isolation; timing, mid-game
allocation freedom and cold-play recovery remain unqualified.

### Adopted stages prefer an already-ready cover over draw-time setup

An adopted exact stage set previously bypassed the dynamic-cover search and
built its missing pipeline on the draw path. Probe for an already-initialized
cover under its complete native key first. Require a non-null pipeline, exact
hash and full-byte equality, then promote the existing native cache entry.
The synchronous hit cannot allocate/initialize or evict an entry. Continue
correct cover draws while the existing flip-time lend finishes the exact
pipeline; no optional compiler job or owned recipe snapshot is added by
this route. If no ready cover matches, keep the existing correct miss path.

The probe projects only PSH fields in the unpublished draw-local ShaderState.
Save 264 bytes of metadata on the stack and restore every byte, including
padding, before any return. Vertex microcode and geometry stay in the original
owned capture and are only read for hashing/comparison. This is a bounded
528-byte metadata save/restore, not a claim of literally zero copies; it
avoids introducing another 2,620-byte whole-state/vertex-program snapshot.
Never pass a published cache key, worker key or guest view to this mutable
local-only projection. Keys, hash algorithm and cache/schema versions remain
unchanged. The regression test uses native XXH3 and LRU behavior and compares
against the conventional fully materialized canonical key.

Keep the existing ADOPTED scheduling marker while repeated covered draws
wait for exact flip-time setup. Clearing it early would allow the old path
to enqueue another full recipe snapshot. Once an exact pipeline is ready,
retire its queued/adopted marker; CPU and GL jobs keep their independent
owned slots through consumption. This also prevents a flip-prepared pipeline
from leaving an adopted marker permanently occupied because its next draw
finds an initialized binding and bypasses the adoption branch. No job is
cancelled or released by clearing this scheduling-only marker. Generation,
eviction, load, geometry and pipeline-restoration guards remain in force.

No new core heap allocation, GPU API call, cache scan, retained payload,
queue capacity or host field is added by the draw-time probe. It searches
one native hash bin only on the existing adopted-stage/missing-pipeline
path. The older synchronous uncovered and utility paths remain violations;
ready-cover reuse alone does not prove cold-play or allocation-free recovery.

Progress qualification: an initial experiment allowed later shader selections
to displace the exact recipe repeatedly. A DOA3 run recorded 79,779 repeated
ready-cover selections and failed both strict RAM restores by one byte. Keep
that experiment and its logs unqualified; no cause for the RAM difference
is established and the zero-byte threshold is unchanged. The corrected
tracking contract preserves a valid exact reservation through the flip.
Other recipes cannot displace it; if a ready-cover request cannot reserve
the slot, it takes the existing correct path. Stale generation/geometry
reservations can be replaced and queued covers do not block exact work.
Fresh selections discard cover lends but preserve exact reservations;
native eviction/load/mode cancellation still applies. Preserve a shared
fragment marker while its reservation is pending, avoiding another queued
full-state snapshot. This adds no queue, host field or reservation capacity.
The native-key tests cover occupied, stale-generation, changed-geometry and
cover-preemption cases, as well as the original readiness/ownership guards.

Only fully ready exact stages qualify for the persistent reservation. Its
state is encoded in an existing scheduling marker, with no additional
storage. Late adoption notifications must not downgrade that marker. Loss
of stage readiness cancels it instead of waiting indefinitely; eviction,
load and changed generation/geometry/mode clear it before reuse. Ordinary
non-reserved recipes keep their previous replacement behavior. The live
draw still recaptures actual guest state and a conflicting reservation takes
the existing correctness path. The additional readiness gate constructs
the same bounded 7,044-byte native module-query keys; those temporary key
materializations are not labelled zero-copy. The PSH cover projection
itself adds no vertex/geometry clone or retained payload.

Corrected validation: 45 contract suites, 13,312 native-hash/projection/
reservation cases and 23,040 native-LRU/lifetime cases passed, with final
Windows/Linux sanitizer guards. Thirty corrected matrix runs across DOA3,
JSRF, Crazy Taxi 3 and NGB completed, plus four fresh-process restores of
the verified live-fight checkpoint. All eight main lifecycle cases passed
both zero-byte RAM comparisons and reset; no timing run used image readbacks.
NGB per-renderer and verified DOA3 fight parent/candidate images matched
exactly. The cold-boot DOA3 comparison differs by 16,068,439 bytes, with
visible background/boat/bird/shadow positions differing; no cause is
established and that comparison remains unqualified rather than discarded.

Eight non-image corrected GL candidate runs recorded 633 draw pipeline-object
creations, 163 flip preparations (154 exact, nine cover), and 277 pending-link
demands. Preparation recorded zero pending-link demands or module-cache
initializations. The ready-cover route was selected 21,570 times; this
includes repeated draws and is not a count of avoided unique pipelines.
For the paired DOA3 warm run, observed draw creations were 59 parent versus
27 candidate. Encountered-state/scheduling differences and uncontrolled
driver caches prevent a causal total or latency claim. Boundary process
censuses saw no RetroArch, without proving continuous isolation. The earlier
failed experiment remains retained separately; its one-byte RAM discrepancy
has no established cause and is not reclassified as a pass. Mid-game host/
driver allocations, uncovered draw creation and cold-play recovery remain
open; no baseline or acceptance threshold was raised.

## Fallback lookup without a full-state clone (2026-11-10)

The remaining GL fallback-selection branch formerly cloned a complete
`ShaderState` merely to canonicalize PSH metadata. It now projects the draw's
unpublished local capture in place and saves/restores only its `PshState`.
The tested ABI reduces the temporary from 2,620 bytes to 264 bytes, with
528 explicit save/restore bytes instead of a 2,620-byte full-state clone.
This bounded metadata snapshot is not described as zero-copy. Vertex
microcode and geometry stay in the original capture. Existing exact hash,
full-key comparison, scheduling, shader generation and pipeline decisions
are unchanged; synchronous uncovered creations remain violations.

The capture is owned by the bind call. Lookup borrows it synchronously;
canonical queue submission takes its existing indispensable final-owner
snapshot before returning. Every PSH byte, including padding, is restored
before an exact queue submission and before leaving the branch, including
failed canonicalization. No stack pointer or new resource reference escapes.
No new heap allocation, queue capacity, cache schema, file I/O or persistent
owner is introduced. Driver allocations and cold-play recovery remain open.

The extracted production branch is exercised against the real canonicalizer
and native hash across 4,096 combinations of state bytes, mode, readiness,
queue markers and exact/cover selection. The checks compare full exact and
canonical keys at each handoff, including padding and unchanged vertex/
geometry bytes. Missing early restoration and submitting the canonical key
as an exact recipe are required failing mutations. This test runs in CI;
removing the full-state clone alone does not establish a latency improvement.

Validation: 46 contract suites passed, plus Windows production-branch and
specialization-preparation sanitizer/mutation controls. Thirty fresh matrix
runs across DOA3, JSRF, Crazy Taxi 3 and NGB completed on GL/Vulkan, plus
four fresh-process verified DOA3 fight restores. All eight fresh lifecycle
cases passed both exact RAM comparisons and reset; image equality passed
for NGB slot 0 and the verified DOA3 loaded fight on both renderers.

Retain the initial stopped parent-only experiment: both strict restores
reported one differing RAM byte, before any candidate run. Its cause is not
established; the fresh passes do not reclassify that failure. Cold-boot DOA3
images differ by 12,205,656 bytes and remain unqualified. The matrix began
with another RetroArch process running; timings are unqualified, even though
it closed later. Driver caches are uncontrolled, no performance threshold
was relaxed, and neither latency improvement nor cold-play recovery is claimed.

## Completed Vulkan results before normal draining (2026-11-10)

A draw miss may encounter a worker-complete result that the ordinary queue
service has not yet adopted. Previously only the cache was queried, so the
draw could create a duplicate library or whole pipeline. The miss paths now
query the worker's release-published completion frontier with acquire ordering.
Only completed, non-null results with matching job type, hash and entire native
key are eligible. Unfinished work is never polled, waited on or promoted.

The scan is bounded by the existing 64-slot ring, including unsigned rollover.
Keys are borrowed synchronously from immutable occupied slots. A library handle
moves into its ordinary renderer-owned table; a whole pipeline moves into the
already selected exact-key cache entry. The native entry's existing key copy
is reused. The slot's result is cleared after transfer, but its module owners
and queue counters remain untouched until the normal ordered drain. That
service releases module references exactly once without destroying a moved
handle. Renderer-owned layouts keep their existing quiescence lifetime.
No unrelated full-cache draining or eviction is introduced into these queries.

No shader, geometry, SPIR-V or file payload is copied. Library metadata retains
its existing one final-owner snapshot and possible native table growth, so this
is not an allocation-free gameplay claim. No queue, capacity, worker, key/schema,
resource reference or explicit allocation call is added. Missing/unfinished
states keep their correct existing synchronous creation path; this is a bounded
readiness fix, not complete unknown-state coverage or stutter recovery.

Validation covers 47 required suites (successful prefix plus resumed remainder;
initial fixture-build errors retained) and final Windows/Linux sanitizer/mutation
checks of 4,608 native-key/type/frontier/owner/drain cases, including full 64-slot
rings and unsigned wraparound. The identity and ready-sibling fixtures explicitly
model an empty completed queue; the new transfer test exercises the real helper
bodies. The extra legacy prewarm-inputs fixture fails to build on the unchanged
parent too, due to stale constructor/key interfaces, and remains unqualified.

Thirty main matrix runs across DOA3, JSRF, Crazy Taxi 3 and NGB, six additional
NGB GPL/8x runs, and four verified DOA3 fight restores completed. All eight strict
lifecycle cases passed both exact RAM comparisons and reset. NGB image pairs
match on GL, native Vulkan and GPL/8x Vulkan; verified loaded-fight DOA3 pairs
match on both renderers. The separate cold-boot DOA3 pair differs by 12,990,068
bytes and remains unqualified. No acceptance threshold was relaxed.

The focused NGB GPL/8x trace exercised one completed whole-PSO transfer during
warm play and no library transfers in this sample. That is one real readiness
hit, not a stutter-recovery or general throughput result. Most first-use misses
remain uncovered. Driver caches are uncontrolled; boundary process checks saw
no concurrent RetroArch for these runs but do not prove continuous isolation.
Instrumented timing comparisons remain unqualified. Diagnostic metadata uses
bounded private storage and is absent from the clean release build.
