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

Use diagnostic builds to count creation/link and compile-wait events by kind
and callback, including utility and flip work. Release builds must contain
none of those diagnostics. Pair separate image/lifecycle runs with clean
cold/warm timing runs. Keep fresh-process restore tests distinct. Uncontrolled
driver caches and the missing accepted cold-play reference remain unqualified;
none of this inventory establishes recovery or a frame-time improvement.
