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

Texture families not listed below still use the correct GLSL/compiler path.
Static and live combiners are native for the supported texture families.
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


## Projected 2D and coordinate stages

All four units can now mix NONE, PROJECT2D, PASSTHRU and CLIPPLANE stages with
static combiners. PROJECT2D uses the existing combined image/sampler descriptor
at `tex_binding + unit`; no texture, sampler or descriptor ownership changes.
Normalized coordinates preserve the reference `xy / max(q, 1e-15)` expression.
Rectangle coordinates first divide by `textureSize / texScale`, then by the
same clamped Q. ImageQuery capability and the scalar std140 texScale array are
emitted only for rectangle sampling. Implicit derivatives, filtering, mip
selection and address modes remain the existing sampler's responsibility.

Passthrough returns the incoming coordinate unchanged. Clip-plane stages perform
all four configured sign comparisons/discards and return zero, matching GLSL.
Stage-three point sprites use PointCoord with Z/Q equal to one; other units
retain their interpolated coordinates. R0 alpha starts from T0 alpha when stage
zero is active. Per-unit static alpha kill or the live alphaKillMask applies
after sampling, before combiners; passthrough and clip-plane do not acquire
samplers or alpha-kill behavior.

The base eleven fields retain their offsets. Rectangle texScale adds a 64-byte
scalar array at offset 496. Live alphaKillMask adds one uint at offset 560 with
that array, or 496 without it. Final UBO extent is 496, 512, 560 or 576 bytes.
Unused extensions produce no descriptors/names; the same two final allocations
remain. The stack record holds SSA IDs only. No guest coordinate, texture,
sampler, uniform or descriptor pointer escapes count/write. Existing native
references, texture owners and descriptor/command retirement cover all last
consumers; no new resource creation or file I/O is introduced.

Cubemaps, 3D/dependent/bump/dot/shadow sampling and special signed/integer
formats still use the correct compiler path. Border adjustment, color key and
the existing convolution path are native. Bindings that would overlap the UBO
or exceed the bounded four-unit range
reject allocation-free. Full practical direct fragment coverage remains open.

CPU coverage expands to 8,192 modules and twenty-five unsupported cases. The
4,096 previous untextured modules are byte-identical to the parent. GPU coverage
adds nonconstant three-level texture data, nearest/linear texel/mip filters,
repeat/mirror/edge/border modes, positive/tiny/zero/negative Q, scaled rectangle
coordinates, mixed coordinate/clip/sample stages, static combiners and live
alpha controls. Dedicated point-list draws exercise PointCoord. Separate
written/discarded/sampled/point pixel counters require visible coverage, rather
than allowing an all-discarded texture matrix to pass. These are correctness
and work-count probes, not GPU execution timings or cold-play qualification.

The complete 10,240-case matrix matched 819,200 scalar results per device on
llvmpipe with validation and RTX 5090 without installed validation layers.
The physical probe observed 55,355 written pixels, 108,485 discarded pixels,
13,176 written pixels in sampled recipes and 8,128 written point pixels. The
software reference and native path both wrote one additional sampled pixel;
per-device native/reference comparisons remain exact. Cross-device filter/alpha
results are not the comparison oracle. It emitted
48,191,438 native words versus 73,546,449 reference words and avoided 83,029,381
GLSL source bytes across those fixtures. All 49 contract suites, 26 GL/VK game
runs, four lifecycle cases and five exact image pairs passed. These work counts
and correctness checks do not qualify cold-play or GPU execution performance.


## Live register combiners

Dynamic combiner recipes now emit native SPIR-V too. The existing combCtl,
combFinal0/1, combRgbIn/Out and combAlphaIn/Out uniforms drive one counted stage
loop and a sixteen-slot shader-private register bank. Source and destination
slots remain masked to four bits; zero writes are suppressed, constant slots
fetch existing uniforms, and all RGB/alpha operands are evaluated before any
writes. Blue-to-alpha and aliased destinations retain reference store order.

The dynamic reference's scale/bias tables have a different expression shape
from the static mapper. Native emission follows the dynamic multiply/add order,
including reserved mappings, rather than substituting static algebra. One shared
source-fetch helper avoids repeating its constant/bank branches at every input.
MSB/LSB mux selection remains lazy, avoiding undefined float-to-uint conversion
when the MSB path is selected. Final sums, complement/clamp controls, E/F products
and final input reads retain their ordering; all-zero final controls explicitly
produce zero, as the dynamic reference does.

Seven existing uniform fields append to the unchanged texture layout: three
uint controls, alignment padding, then four eight-element std140 uint arrays.
They add 528 bytes, for a final extent of 1024, 1040, 1088 or 1104 bytes. No new
binding, sampler, image view, buffer allocation or upload path is introduced.
The private register bank and immutable scale/bias tables are shader values,
matching the reference, rather than new host or GPU resource owners.

The immutable cache/job recipe and bounded precise-ID record retain their
synchronous count/write lends. Exactly two final allocations remain, and native
source adoption transfers their exact pointers into existing reference owners.
No guest/uniform payload snapshot, retained lend, compiler AST, intermediate
GLSL/word buffer, cache schema/default change or file I/O is added. The previous
8,192 static modules are byte-identical to the parent.

CPU coverage expands to 12,288 modules and twenty-five allocation-free rejected
recipes, including ignored static combiner-field poisoning, two shader loops,
all appended layout bounds and final owner independence. GPU coverage adds
6,144 live recipes spanning all sixteen register slots, mapping/store aliases,
zero through eight stages, both mux modes, unique/shared constants, final
sum/EF controls, all-zero finals and the supported texture/point families.
Invalid live stage counts beyond the reference's eight-element arrays are not
qualified. The CPU emitter ignores the unused static count in dynamic mode;
the reference fixture retains its canonical zero count because its parser still
walks that field before choosing the dynamic generator.

Correctness, work counts and cold-play/driver-cache qualification remain
separate. This extends translation coverage; it does not establish ready PSOs,
zero draw-path compilation/waits, GPU execution savings or stutter recovery.

The full 16,896-case matrix matched 1,351,680 scalar outputs per device on
llvmpipe with validation and RTX 5090 without installed validation layers.
An extra 512 cases cover infinities, NaNs, negative zero, subnormals, non-binary
fractions and disabled alpha conversion with exceptional live colors. The
reference and native path match on each device; differing NaN payloads remain
permitted only when both results are NaN. Aggregate fixture output used
90,524,558 native words versus 120,500,425 reference words and avoided
145,042,821 GLSL bytes. These are work counts, not game/GPU speedup claims.
All 49 contract suites, 26 GL/VK game checks, four lifecycle cases and five
exact parent/candidate image pairs passed.

The test-only `PSH_COMPILER_TIMINGS=1` probe separates host pipeline creation
from submit-to-fence wall time by recipe/path. It is not linked into the core,
does not record presentation latency or GPU timestamps, and never weakens the
output/coverage checks. Its 512-case physical attribution probe passed; timings
remain unqualified because driver caches are warmed/uncontrolled and CPU/software
correctness checks overlap. No latency baseline or recovery claim follows.


## Border adjustment, color key and convolution

PROJECT2D stages can now use their existing border-adjustment flag, all four
color-key modes and both existing convolution flags with static or live
combiners. New instructions reference the same per-unit combined samplers and
views. No new descriptor, texture/buffer allocation, host API call or resource
retirement path is introduced.

Border adjustment follows `(coord.xyz * logicalSize + vec3(4)) * invRealSize`
before rectangle normalization/projection, leaving Q unchanged. Only sampled
border stages append the existing borderLogicalSize/borderInvRealSize arrays.
Color key uses the reference float-to-uint rounding, ARGB packing and mask
comparison. It supports no change, alpha kill, color/alpha kill and discard.
Texture alpha kill executes before key mutation; killing alpha through the key
does not incorrectly trigger a second texture alpha-kill test.

The convolution path preserves one counted nine-tap loop per affected unit,
row-major offsets and sequential weighted accumulation. It divides by raw Q,
as the reference convolution does, then adds offsets measured in base-level
texels. This is deliberately distinct from ordinary PROJECT2D's positive-Q
clamp. The immutable offset/weight arrays contain shader constants and share
one declaration across all units. Gaussian and quincunx flags retain the
current GLSL generator's same 3x3 Gaussian kernel; this does not fix its existing
quincunx FIXME or claim a different hardware filter.

Border arrays are vec3[4], each with std140 stride 16 and a 64-byte extent.
Color-key/mask arrays are uint[4] with the same stride/extent. Consumed pairs
append after the existing layout, up to 256 extra bytes and a 1360-byte final
extent. Array names/metadata remain in the same single final allocation, with
no new host allocation count. Private convolution tables, the shader register
bank and bounded emission records contain SSA IDs/constants, not retained guest
pointers, texture/uniform snapshots or copied shader payloads. Existing source,
module, texture and descriptor owners still cover each last consumer.

CPU coverage expands to 16,384 modules, with exact two-allocation checks,
appended layouts/bounds, one sample instruction per unit and one nine-tap loop
per convolution unit. All 12,288 earlier modules are byte-identical to the
parent. Added GPU fixtures exercise all units, static/live combiners, rectangle
scaling, non-binary border coefficients, key masks/matches/misses, alpha-kill
ordering, point sprites and mixed four-unit filtering. Mask scheduling is
independent of key mode so discard is visibly exercised. Output comparison,
watchdog and coverage requirements are unchanged.

Other sampler dimensions/modes, special signed/integer formats and shadow
sampling remain on the correct compiler path. Full practical direct coverage,
cold-play smoothness and draw-critical PSO compilation remain open.

The completed 18,944-case matrix matched 1,515,520 scalar results per device on
llvmpipe with validation and RTX 5090 without installed validation layers.
Aggregate fixture output used 101,864,648 native words versus 133,667,676
reference words and avoided 162,456,309 GLSL source bytes. All 49 contract
suites, 26 OpenGL/Vulkan game checks, four lifecycle cases and five exact image
pairs passed. These correctness/work counts do not establish GPU execution or
cold-play latency improvements; driver-cache and timing qualification remain
open.
