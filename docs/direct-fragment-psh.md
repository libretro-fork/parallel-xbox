# Direct Vulkan fragment translation

`pgraph/spv/psh.c` translates supported static NV2A register-combiner
recipes directly into final SPIR-V. Vulkan source preparation tries this path
before generating GLSL. OpenGL continues to use the existing generator.

The first supported family has all four texture modes set to NONE, zero to
eight static combiner stages, and an enabled final combiner. It includes all
input/output mappings, dot products, MSB/LSB mux selection, unique/shared stage
constants, final E/F products, complement/clamped sums, blue-to-alpha writes,
and aliased register destinations. RGB and alpha expressions are evaluated
before either set of destinations is committed. Flat/smooth and two-sided
colors, interpolated fog, static/dynamic alpha testing and all comparison
functions preserve the production GLSL behavior.

The common fragment work is native too: inclusive/exclusive window clips use
the reference early-exit loop; Kahan barycentric determinants, overflow retry,
perspective/depth reconstruction, slope/offset, range clipping/clamping and
integer/float depth encoding preserve expression order and precise boundaries.
SPIR-V 1.5 uses Kill; 1.6 uses DemoteToHelperInvocation and continues helper work.
The dynamic alpha-disable branch skips conversion just as the reference does.

## Ownership and allocation

The cache entry or generation job owns the immutable PshState throughout the
synchronous count/write calls. The emitter never retains that pointer. A bounded
128-entry stack record stores precise-operation SSA IDs, not shader words or
source payloads; the final write borrows that record only until it returns.

Emission allocates exactly the final word buffer and the final metadata/name
buffer. The existing ShaderSource adoption transfers these exact pointers into
native reference owners; existing publication/retirement handles their last
CPU/GPU consumers. There is no GLSL string, compiler AST, intermediate word
buffer, key/geometry/uniform snapshot, file I/O, new binding, readback or
per-draw bookkeeping. Existing indispensable final allocations remain an open
allocation optimization, rather than being described as allocation-free.

The final std140 UBO has eleven existing named fields and a 496-byte extent.
Existing uniform lookup/upload uses the emitted offsets and bounds; no changes
to uniform values or uploads are required. Pipeline/recipe identity and cache
schemas are unchanged: equivalent GLSL-generated modules remain valid. Native
module words still identify the generated driver shader/pipeline normally.

## Coverage and remaining work

Textured and dynamic combiner shaders still use the correct GLSL/compiler path.
Unsupported texture/depth modifiers, invalid registers/destinations/stage counts,
and reference-undefined disabled-final-combiner recipes reject before allocating.
No draw is dropped and no placeholder is substituted. Extending those families
is required for full practical direct fragment coverage.

The public CPU test validates 4,096 deterministic modules, both SPIR-V targets,
owned layout/name bounds, two final allocations, ignored-field independence,
and sixteen allocation-free unsupported recipes. The software/physical GPU
probe links the production GLSL generator as its independent reference. Its
4,096 cases compare color, depth and discard at sixteen pixels, including
interpolated inputs, all stage counts, aliasing, exceptional planes, overflowing
coordinates, zero/negative W, surface scaling and partial clips. Outputs are
bit-identical (both-NaN outputs permit differing NaN payloads); tolerance is not
relaxed. CI retains both CPU and software-GPU logs.

The initial complete matrix matched 327,680 scalar outputs per device on
llvmpipe with validation and RTX 5090 without installed validation layers.
All 49 local contract suites and four additional gate/I-O suites passed. The
rebuilt core completed 26 OpenGL/Vulkan game checks, four lifecycle cases and
five exact parent/candidate image pairs. Across the 4,096 GPU recipes, native
output used 18,385,797 words versus 28,140,717 reference words and avoided
32,234,036 GLSL source bytes. These are fixture work counts, not game speedups.
Game image/lifecycle evidence is recorded separately with its build ID and
qualification limits. Uncontrolled driver
caches and overlapping correctness runs do not establish latency gains. CPU
translation savings do not establish GPU execution savings, ready PSOs, zero
draw-critical compile waits, or cold-play recovery; those remain BAD/OPEN.
