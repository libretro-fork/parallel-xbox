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
