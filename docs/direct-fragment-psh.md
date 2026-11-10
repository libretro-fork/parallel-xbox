# Direct Vulkan fragment translation

`pgraph/spv/psh.c` translates supported static and live NV2A register-combiner
recipes directly into final SPIR-V. Vulkan source preparation tries this path
before generating GLSL. OpenGL continues to use the existing generator.

Current texture coverage includes projected 2D/volume and cube reads, dependent
and bump/luminance stages, dot/reflection/depth chains, raw unsigned colors,
fixed/packed/F16 depth,
UI clamps, signed G8B8 filtering and mip reconstruction. Unsupported shapes
retain the compiler. The sections below record each family's implementation
and validation scope; native translation does not certify pipeline readiness.

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

The base std140 UBO has eleven existing named fields and a 496-byte extent.
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

Dependent/bump/dot/shadow sampling and special signed/integer formats still
use the correct compiler path. Projected 3D and cube families are described
below. Border adjustment, color key and
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

Remaining dependent/bump/dot modes, special signed/integer formats and shadow
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


## Per-unit 3D and cube sampling

Each unit now declares its actual 2D, 3D or cube sampler type while keeping the
existing `tex_binding + unit` combined-image/sampler descriptor. Multiple kinds
can coexist in one module. Image views and per-unit samplers remain owned and
retired by the existing texture/descriptor paths; no resource creation, copy,
new binding, CPU coordinate snapshot or additional final allocation is added.

PROJECT2D with a volume sampler uses the reference projected vec4(X,Y,0,Q).
PROJECT3D supports 2D and volume samplers and preserves the reference
textureProj instruction and supplied coordinate width. Rectangle PROJECT3D
coordinates retain the reference vec4(normalizedXY,0,Q) construction. These
paths do not substitute ordinary PROJECT2D's positive-Q clamp.

CUBEMAP samples XYZ directly for a cube view. With a 2D view it uses the exact
reference cube-to-2D mapping: strict X major, then strict Y major, otherwise Z,
including ties and sign choices. Division stays inside the chosen axis branch.
PROJECT2D with a cube view preserves projected XY normalization and the shared
normalize(vec3(1, projectedY, -projectedX)) helper. Shared helpers avoid repeating
their branches/arithmetic for four units. Border adjustment and rectangle scale
are consumed only where the reference uses them; CUBEMAP's unused flags do not
append uniforms. Alpha kill and color keys retain their previous ordering.

Cube-to-2D and projected-cube functions each have one declaration per module,
regardless of unit count. Their parameters/results and the new sampler/type
records are SSA IDs, not pointers into guest data or retained host payloads.
Count/write borrow the same immutable recipe and bounded precise-ID record;
exact final word/layout pointers still move through existing native references.
No shader/cache schema or default changes are needed: prior compiled modules
remain equivalent. All 16,384 previously supported modules are byte-identical.

Rectangle volume combinations retain fallback because the reference rectangle
helper is two-dimensional; invalid dimensions, cube convolution and other
unsupported encodings reject allocation-free. Remaining dependent/bump/dot,
shadow and special signed/integer formats are still open. No ready-PSO or
cold-play recovery claim follows from the expanded translation coverage.

CPU coverage expands to 20,480 modules and twenty-five rejected recipes. New
checks inspect actual image dimensions, projected/sample/query instruction
counts, single helper declarations, mixed/all-same sampler families, ownership
and layout bounds. GPU fixtures add marked three-level volume and six-face cube
images, all units, six face directions, strict-axis ties, zero directions,
positive/negative/zero/tiny Q, rectangle scale, border/key/alpha controls, points
and mixed/all-same four-unit samplers. Exceptional-coordinate observations do not establish portable behavior for
invalid coordinates. Comparison is against production GLSL on each same device,
rather than a cross-device face/color oracle.

The completed 23,040-case GPU matrix matched 1,843,200 scalar outputs per device
on llvmpipe with validation and RTX 5090 without installed validation layers.
Aggregate fixture output used 121,653,110 native words versus 156,850,296
reference words and avoided 193,643,809 GLSL bytes. Both rectangle-volume modes
were also compiled through the production reference as rejection controls;
they fail with vector-size errors, consistent with the native exclusion.
All 49 contract suites, 26 GL/VK game checks, four lifecycle cases and five
exact image pairs passed. Timing/driver-cache qualification remains open;
these work counts do not establish GPU execution or cold-play recovery.


## Dependent reads and bump environment maps

Dependent AR/GB reads now consume the selected preceding stage's processed
texture result, including alpha/key changes. Bump environment and bump
luminance use the reference B/G sign conversion and GPU mat2 operation.
The existing mat2[4] upload is represented by eight padded vec2 columns;
no host matrix transpose or second matrix buffer is introduced. Rectangle
normalization follows the bump offset. Volume bump sampling preserves the
reference's original Z coordinate. Luminance scales all four sampled channels
before alpha kill and color keys. One shared sign-conversion helper serves
all bump units, with division inside the selected branch.

Only consumed bump uniforms append after the existing layout: 128 bytes for
matrix columns and, for luminance, two 64-byte scalar arrays. The maximum
combined layout is 1616 bytes and 27 members. Existing word/layout owners,
image views, sampler bindings, uniform uploads and retirement are unchanged.
The immutable key is borrowed synchronously through count/write; bounded SSA
IDs contain no payload pointers. Two final allocations remain indispensable;
this is not allocation-free shader preparation. No geometry, texture, key or
file payload is cloned, and no new lend outlives its existing owner.

Unit zero and self/forward/out-of-range dependencies reject allocation-free.
Dependent AR/GB rectangle, border and volume combinations retain fallback;
bump rectangle-volume does too. Bump border flags remain ignored as in the
reference. Signed G8B8 overrides, dot families, shadow sampling and special
signed/integer formats remain open. Reference translation equivalence is not
a claim to correct existing hardware-model limitations.

CPU coverage adds 4096 recipes for dependent reads, static/live combiners,
point coordinates, rectangle/volume selection, key/alpha controls, matrix
layout, exact final allocation counts and post-key-erasure result ownership.
Eight additional rejection controls preserve allocation-free fallback. GPU
cases include chained dependencies, all valid predecessor selections,
processed predecessor keys, zero/passthrough/projected/cube producers and
bump matrices with nontrivial off-diagonal columns. The full comparison matrix
now contains 27136 recipes against the production GLSL implementation.

Existing synchronous PSO misses and uncontrolled driver-cache qualification
remain open. Removing these GLSL intermediates does not establish draw-path
pipeline readiness, GPU execution improvement or cold-first-play recovery.


## Dot-product chains, reflection and depth replacement

The native emitter now translates DOTPRODUCT, DOT_ST, DOT_ZW, DOT_RFLCT_DIFF,
DOT_RFLCT_SPEC, DOT_STR_3D and DOT_STR_CUBE. A four-entry SSA-ID array records
dot results in stage order. Invalid unit placement, missing preceding dot
results, self/forward input reads and out-of-range mappings reject before
allocation. Diffuse reflection additionally reads the next unit's varying
and mapping; a reference to a not-yet-defined texture result remains rejected.
That varying is declared even when the following texture stage is disabled,
including PointCoord when point sprites provide it.

Each consumed signed dot mapping has one shared function. Sign helpers are
shared too, including the existing bump sign3 helper and its function type.
HILO mapping preserves the reference's unsigned A/R and G/B packing. The
three hemisphere mappings still return RGB as in the reference's FIXME;
this does not implement missing hardware behavior or define invalid float
to integer conversions. No dot tables, input payloads or source keys are
cloned. Types, function IDs and dot results live only in the synchronous
bounded Context; the final word and layout allocations retain their owners.

DOT_ZW replaces window-space depth with preceding/current dot, using float
max for a zero denominator. The original depth range test is deferred; each
replacement applies the same range/precise-clamp behavior. Polygon offset
therefore does not survive replacement. Chained replacement fixtures exercise
two range tests and sampled-dot predecessors. Sampling retains the existing
2D/volume/cube resources, rectangle scaling, border adjustment, cube face/tie
rules, reflection arithmetic and alpha/key order. DOT_ST border adjustment
and rectangle volume combinations remain excluded. No new uniform groups,
bindings, host resources or payload storage are introduced.

CPU coverage adds 4096 dot recipes and sixteen allocation-free rejection
controls (28672 final-owner recipes, forty-nine rejects in total). GPU coverage
adds 4096 production-GLSL comparisons for all eight mappings, stage placement,
processed and zero predecessors, sampled and depth-replacement chains,
rectangle/volume/cube kinds, reflection singularities, point coordinates,
static/live combiners and replacement clipping. The full matrix has 31232
recipes. Signed/depth-format sampling, shadow comparisons, UI clamp and the
reference-unimplemented BRDF/constant-specular modes remain open. Expanded
translation is not evidence of ready PSOs or cold-first-play recovery.


Integer-packed HILO lookup chains explicitly encode a zero implicit-LOD bias.
The operand-free form matched arithmetic/UV probes but diverged in RTX mip
selection. Explicit gradients and unconditional bias introduced other
mismatches and remain rejected diagnostic variants. The mapping-specific zero
bias preserves implicit derivatives and sampler filtering; regression coverage
checks its exact zero operand and keeps other dot mappings operand-free. All
failed probes remain retained; this is not a tolerance or coverage relaxation.


## Constant depth-texture comparisons

For 2D PROJECT2D/PROJECT3D shadow stages, NEVER and ALWAYS return zero/one
before any image access in the reference. The native path now emits that
result directly and runs the same alpha-kill and color-key processing before
later stages consume it. A result-bearing texture stage is distinguished from
an actual image-sampling stage. Constant-only units have no sampler, texture
coordinate, image/query instructions or texture/border uniforms. Other units
retain their original bindings, owners, sampling and uniform uploads; no
texture conversion, payload snapshot, geometry copy or retained lend is added.
The frontend may still perform its existing texture binding work; absence of
shader access is not a claim that all resource work or PSO creation disappears.

The final word and metadata owners still require two allocations. Keys are
borrowed synchronously through count/write. Constant format, rectangle and
border fields are proven unused without removing them from persistent keys.
Color-key and live alpha/combiner uniforms remain present when consumed. A
following varying needed by diffuse reflection is declared even when its own
texture result is constant. Other comparisons, signed formats, unsupported
sampler dimensions and malformed states keep allocation-free rejection and
compiler fallback; no cache schema or default is changed.

CPU coverage adds 4096 final-owner recipes and sixteen rejection controls:
32768 modules and sixty-five rejects overall. The new constant-only recipes
check exact allocation counts, no image/sample/query IR, no texture/border
uniforms, layout bounds and result ownership after recipe erasure. GPU tests
add 4096 constant cases plus 2048 mixed bump/luminance/dependent-read chains,
including packed/F16 flags, rectangles, all key modes, static/live alpha kill,
point sprites and static/live combiners. A further 512 cases read the following varying for diffuse reflection while
that following unit produces a constant depth result, including point
coordinates and every dot mapping. The full matrix has 37888 recipes.

Read-only mapped inspection of completed private corpus seeds found native
recipe acceptance rising from 174 to 182 of 186 for NGB, 30 to 34 of 41 for
DOA3, 112 to 120 of 120 for JSRF and 271 to 279 of 279 for CT3. These are
recorded/seeded recipes, not draw frequencies, pipeline readiness or frame-time
measurements. NGB's four remaining packed-depth recipes and DOA3's seven UI
clamp recipes still require further native coverage. General fixed/packed/F16
shadow sampling and signed/mip formats remain open work. Cold play and
draw-critical pipeline compilation remain BAD/OPEN and unqualified.


## Sampled fixed-depth comparisons and packed X8Y24

PROJECT2D/PROJECT3D fixed-depth shadow stages now sample existing 2D float or
unsigned views directly. The extra sampler kind is an unsigned 2D image at the
same unit binding, with bounded image/type IDs and no new renderer resources,
image conversions, payload buffers or retained views. A shared uint-vector type
serves packed reads and color keys. No uniform groups or cache schema change.
Two final allocations remain; immutable recipes are borrowed synchronously.

Projection uses the reference's raw Q, including rectangle normalization before
the projected sample. Shadow stages ignore border/convolution processing as in
the reference. Packed depth extracts the high 24 bits and divides by 16777215;
PROJECT3D retains the normalization/scale roundtrip. Float view markers select
16777215, 511.9375 or 65535 at runtime, then clamp original Z/Q to that range.
All six comparisons preserve the reference order (sampled depth OP reference Z)
and unordered not-equal behavior. PROJECT2D compares sampled depth against zero.
Alpha kill and color keys still run on the resulting four-channel Boolean.
F16 gather/decode/filter flags, UI clamp, signed formats and unsupported shapes
continue through the compiler rather than receiving an approximate substitute.

CPU coverage adds 4096 final-owner/typed-image/projection/layout/ignored-field
recipes and sixteen rejected inputs: 36864 validated modules and eighty-one
allocation-free rejects overall. Prior 32768 native modules remain byte-identical.
GPU fixtures use aliased views of the existing float image for depth markers,
a one-time 84-byte mapped staging upload for marked packed texels, and a shadow
coordinate vertex variant for range/boundary comparisons. Prior vertex variants
remain unchanged and new views/image are created only in the new batches.
These fixtures add no resource work to the core. Packed image samplers use
nearest minification, magnification and mip selection. Tests cover all six
comparisons, stage slots, rectangles, Q cases, static/gradient references,
point coordinates, alpha/key controls and static/live combiners. Mixed cases
consume float depth, unsigned depth, ordinary 2D/volume and cube results in one
final combiner, exercising all four sampler kinds together. The full matrix is
44032 recipes. Counts and invalid-coordinate observations are not portable
hardware behavior or GPU execution/cold-play measurements.

Read-only mapped recipe inspection of completed phase120 profiles accepts all
187 NGB, 121 JSRF and 281 CT3 recipes, plus 34 of 41 DOA3 recipes. These are
recorded/seeded keys, not draw frequencies or ready PSOs. DOA3's seven UI-clamp
recipes, general F16/signed/mip coverage and existing draw-path pipeline misses
remain open. Driver caches and cold-first-play qualification remain unqualified.


## UI rectangle clamp coverage

UI-clamp recipes use the existing geometry stage's negative-zero slope marker
and plane-position UV bounds. A structured selection bypasses barycentric
reconstruction for marked rectangles and merges zero weights, preserving the
reference's depth arithmetic after that merge. Other pixels follow the original
overflow recovery, perspective normalization, depth offset and clipping path.
Only the first PROJECT2D stage is clamped, using the shared one-based stage
selector. A missing projected stage remains an allocation-free unsupported
recipe. Dot/future coordinate reads share that clamp helper; Z/W stay intact.

The marker and bounds remain in their existing varying owners. The generator
borrows immutable keys synchronously and stores only SSA IDs in bounded stack
state. Exactly two final word/layout allocations remain. There are no geometry
snapshots, new descriptors, renderer resources, intermediate GLSL or retained
lends. Non-UI recipes emit no new instructions or constants.

CPU fixtures add 1024 marker/type/depth/rectangle/point/final-owner recipes;
the overall validator matrix contains 37888 modules and eighty-one rejected
recipes. GPU fixtures add 4096 cases, covering marked and unmarked inputs,
all four projected-stage slots, Q/filter/address/rectangle/point variations,
static and live combiners, alpha/key finishing and all depth formats. The
full matrix contains 48128 cases. These are correctness/work-count fixtures,
not pipeline-readiness, GPU execution-time or cold-first-play certification.
Driver cache state remains uncontrolled; the stutter regression stays BAD/OPEN.


## Decode-before-filter F16 depth

F16 PROJECT2D/PROJECT3D shadow stages use the same float views and descriptors.
The existing negative texScale tag selects encoded guest-RAM texels; positive
scale selects already-normalized GPU surface depth. Rectangles normalize with
the absolute scale. Point reads sample explicit level zero at texel centers;
linear reads gather at quad centers in wzxy order, decode each value before
bilinear mixing, and preserve zero texels. Differing min/mag flags use the
reference's textureQueryLod.y threshold. PROJECT3D compares against clamped
original Z/Q in the F16 range without a second normalization/scale roundtrip.
PROJECT2D compares against zero. Alpha kill and color keys remain last.

The builder reuses the color-key/packed unsigned-vector type, one Boolean
vector type and one decode constant only when required. Existing texScale
storage is included for normalized F16 recipes too; no new uniform group or
cache schema is introduced. Keys are lent synchronously through count/write;
all arithmetic temporaries are bounded SSA IDs. Two final allocations remain,
without intermediate GLSL, host texel conversions, payload buffers, descriptor
snapshots or retained lends. Signed/mip formats and invalid F16 shapes retain
the compiler. A single stage tagged as both F16 and packed unsigned remains
unsupported; mixed stages may use both view kinds.

CPU fixtures add 4096 typed-image/query/filter/decode/ignored-bit/final-owner
cases and sixteen allocation-free rejections, for 41984 modules and ninety-seven
rejections overall. Former valid F16 negatives move into positive coverage;
negative controls now use unsupported flag patterns. GPU fixtures add 4096
single-unit and 2048 mixed encoded/normalized-F16, packed-depth, cube/volume and
UI cases. Encoded R16 test texels are filled directly into a bounded 42-byte
mapped upload, fenced before release; this test-only resource is never added
to the core. The full matrix contains 54272 cases. Work counts and recorded
recipe acceptance are not pipeline readiness or stutter-recovery evidence.
Uncontrolled driver caches keep cold-play qualification BAD/OPEN.


## Signed G8B8 reconstruction

Eligible 2D projected signed-byte stages reconstruct both source bytes through
the existing sign3 helper. Single-level point and gathered linear/mixed filters
preserve sampler addressing and decode each tap before interpolation. Mip
reconstruction uses the reference LOD query/zero override/lower-clamp decisions,
floor/ceil levels, explicit texel addressing, mixed point/linear selection and
trilinear mixing. Shared and per-axis edge fields, negative-period mirrors,
legacy lower-mip bitplanes and the existing extended minimum/address encoding
are consumed directly from the borrowed key. The cache schema stays unchanged.

Signed pairs remain separate SSA IDs while dependent bump/luminance stages
read the original processed texture values. Signed bump axes use those pairs;
combiner channels are clamped/remapped only after all texture stages finish,
before R0-alpha initialization. This preserves the reference byte mappings,
color-key/alpha order and UI-clamped original coordinates. Existing sign helpers
are shared with bump stages. New stack fields contain at most four coordinate,
sample and signed-pair IDs; no payload, texture, geometry or descriptor snapshot
is made. Keys are lent synchronously through count/write. Exactly two final
allocations and the existing image/uniform/refcount owners remain.

CPU fixtures add 4096 point/gather/mip/address/extended-minimum/owner cases and
sixteen allocation-free shape rejections, for 46080 modules and113 rejections.
GPU fixtures add4096 single-stage and2048 two-signed-source/bump-luminance/
cube-volume/UI cases. Both ordinary and final constant banks are populated;
the first signed RGB sample must be finite and strictly between zero and one,
so a missing final constant cannot turn equality checks into all-white output.
A test-only 256-square RG8 pyramid supplies spatially
varying boundary bytes across nine levels; its174762-byte mapped upload is
filled directly and fenced before release. Component swizzles reuse the same
two source bytes without RGBA payload expansion. No such resource is added to
the core. The full matrix contains60416 cases. Value/work-count evidence does
not establish pipeline readiness, GPU execution speed or stutter recovery;
uncontrolled driver caches keep cold-play qualification BAD/OPEN.


## Raw unsigned integer color

Raw integer PROJECT2D/PROJECT3D reads reuse the existing unsigned 2D view and
vector type. Numeric conversion happens directly after each sample; no packed
shadow extraction or depth normalization is applied. Nine-tap convolution
converts each integer sample before weighting, using the same ordered loop,
offsets and weights. Projected Q, rectangle normalization, border adjustment,
alpha kill and color keys preserve the production reference behavior. The
reference's PROJECT2D volume path ignores its packed-format flag and retains
the existing float 3D sampling path. Conflicting signed-byte/F16 formats and
unsupported coordinate/sampler shapes remain allocation-free fallbacks.

Count/write borrows the same immutable key synchronously and emits directly
into final owned words/layouts. No new CPU payload, uniform group, descriptor,
image, geometry copy, file I/O or retained lend is introduced. Existing image
and shader retirement owners remain unchanged. Integer-only recipes omit the
unused float image type; the positive-Q constant is added only when consumed.
Exactly two final allocations remain.

CPU coverage adds4096 raw and1024 ignored-volume-flag cases, plus16 rejected
shapes:51200 validated modules and129 allocation-free rejections overall.
GPU fixtures add4096 raw/convolution/ignored-volume cases and2048 mixed raw,
signed G8B8, bump/luminance, fixed/F16 depth, cube/volume, UI and MSB-mux cases.
The existing unsigned image is reused without new uploads. Stage and final
constant banks are explicitly populated; integer values are scaled below
saturation so equality checks expose conversion/filtering differences. The
full matrix contains66560 cases. These value/work-count checks do not certify
pipeline readiness, GPU execution speed or cold-play smoothness. Driver caches
remain uncontrolled and the stutter regression stays BAD/OPEN.


## Unused texture metadata

Signed-mip metadata is ignored when no signed texture consumes it. Format
flags are ignored on NONE, PASSTHRU, CLIPPLANE, DOTPRODUCT and DOT_ZW stages,
which do not sample an image. Live sampler, signed-format, passthrough-border,
dot-chain and global point/rectangle checks remain unchanged. Invalid signed
flags still use the compiler fallback. Previously negative cases whose fields
are now proven unused move to positive coverage; their negative controls now
request conflicting signed formats instead.

This changes eligibility only. The immutable input key remains borrowed and
unmodified; it is neither cloned nor canonicalized. The emitter, final two
allocations, layouts, resource owners and retirement order are unchanged.
4096 cases across eight stage families prove identical production GLSL text,
native words and owned layouts with and without the extra metadata. The same
cases retain two final allocations and validate as Vulkan SPIR-V. GPU fixtures
add4096 sampled, samplerless, dot-depth, dependent, static/live and point cases
using existing views and buffers. The public matrix contains55296 CPU modules,
129 allocation-free rejections and70656 GPU cases. No new shader instructions,
resource copies, file I/O, retained pointers or release diagnostics are added.
Pipeline readiness and cold-first-play smoothness remain separate BAD/OPEN work.

## Native UI geometry companion

The fragment UI clamp has a native triangle/quad geometry companion. It reads
original input positions and selected texture coordinates, validates finite
axis-aligned rectangles, constant Z/W/positive Q, first qualifying edge gradients
and quad affine tolerance, then computes fractional-origin pixel-center UV
bounds. Valid rectangles bypass plane calculation and retain the negative-zero
marker. Invalid candidates retain the precise plane calculation. Fill, wire and
point output use the same shared vertex UI override as production GLSL. Odd
quad-strip invocations remain empty.

Native options carry the existing one-based UI stage; persisted geometry keys
and GLSL option layouts are unchanged. UI-disabled modules remain byte-identical.
Existing GPU position/texture SSA values are reused during emission. There are
no geometry snapshots, new resources, uniforms, descriptors or retained borrowed
pointers. Generation retains one final word allocation and a bounded stack-only
annotation list. Classifier arithmetic does not enter the precise-plane list.
Stages outside1..4 retain allocation-free fallback; narrow two-input lines keep
their existing fallback.

CPU tests cover native/reference generation, stage bounds and allocations.
GPU tests capture every geometry output against production GLSL, including exact
signed-zero markers, fill/line/point, all four texture stages, both SPIR-V versions,
smooth/flat colors, both depth conventions, strip parity and rotated triangles.
Fixtures include flipped/swapped axes, fractional origins, different Q/Z/W,
nonfinite inputs, short extents, nonaffine UVs and large coordinates. These checks
do not establish PSO readiness, GPU execution speed or cold-play recovery; the
regression remains BAD/OPEN.


## Fixed display programs

The fixed native/byte scanout fragment variants are precompiled with the in-tree
compiler as immutable SPIR-V1.5/1.6. CPU regression tests require exact module
words and push metadata for both compiler prefix modes. Shader source is retained
in display-glsl.h solely for tests/regeneration; runtime display initialization
neither parses nor constructs GLSL, allocates word buffers, nor clones payloads.
Four variants occupy64560 read-only bytes. Each shader module still has one final
owner with aligned mutable128-byte push storage. Static names/descriptors belong
to the core image through module destruction; word lends end after synchronous
vkCreateShaderModule. Dynamic shaders keep their existing reference owners.
Unused host gamma reflection storage is omitted; actual GPU LUT, descriptors,
publication and retirement remain unchanged. Driver internal copies/allocations
are unmeasured. Startup translation removal does not establish draw-path PSO
readiness, GPU execution speed or cold-play recovery; the regression stays open.

## Integer three-dot lookups

DOT_STR_3D with a two-dimensional X8Y24 view uses an unsigned color lookup in
the production Vulkan shader. Numeric texels convert to floats after sampling;
they do not undergo shadow-depth normalization or projective division. The
packed sampler type takes priority over shadow flags. With a float 2D view,
shadow flags do not enable a depth comparison either. Shadow bit 1 still
selects absolute rectangle scale, including for negative scales. Border adjustment runs
before reducing the three-dot coordinate to XY, then rectangle normalization.
Both sampler types retain the explicit zero HILO bias workaround. Unsupported
coordinate/sampler shapes retain fallback.

Native emission reuses the unsigned view and sampler slot with one vector
unsigned-to-float conversion. Float recipes keep their original words. The key
remains an immutable synchronous lend; no canonicalization, texel copy, snapshot,
new resource, retained pointer or ownership change is added. The module and
layout retain their two final allocations and existing retirement rules.

Public coverage adds 4096 owner/validator cases and 16 allocation-free rejection
controls (59392 CPU modules, 145 rejected shapes), plus 4096 GPU recipes. They
exercise all dot mappings, integer/float views, ignored shadow/compare/filter
metadata, border/rectangle reads, points, keys, alpha kill and static/live
combiners. Final constant banks are populated explicitly; a signal assertion
requires a nontrivial scaled integer result. The public GPU matrix contains
74752 recipes. Work reductions and reference equality do not establish GPU
execution speed, draw-path PSO readiness or cold-play smoothness. Driver caches
remain uncontrolled and the cold-play regression remains BAD/OPEN.
