# Direct vertex emission

Vulkan module preparation tries NV2A programmable microcode -> SPIR-V and
a bounded fixed-function subset before generating GLSL. Geometry emitters
and the established GLSL compiler remain available. This is a translation-work reduction, not a claim that
first-use pipeline stutter is fixed or that draws never compile pipelines.

## Coverage and fallback

The emitter handles the fourteen MAC and eight ILU encodings, paired writes,
register/output masks, R12/oPos aliasing, constant addressing, uniform and vertex
inputs, compressed inputs, BGRA swizzles, dynamic/disabled fog, all six valid
static fog modes and the Vulkan vertex postlude. Unsupported encodings, constant
writes, unsupported fixed-function states, invalid static fog modes and point-size
states without the required FP32 device support return to the existing compiler
without allocating an unsuccessful direct result. The selector is
conservative; it does not replace unknown work with a placeholder.

Initial RTX 5090 comparisons exposed exceptional RSQ/LOG point-size differences.
Explicit arithmetic control flow reduced sixteen differences to eight, but did
not resolve them. Mirroring point stores to a regular output also failed. A
controlled experiment requesting SignedZeroInfNanPreserve(32) for both modules
removed every difference, with no arithmetic rewrite. The failed logs remain
retained; this is not evidence of a driver defect.

Vulkan permits NotInf/NotNaN assumptions when the entry point does not request
preservation: [floating-point environment](https://docs.vulkan.org/spec/latest/appendices/spirvenv.html).
Direct point shaders now request that environment only when the physical device
reports shaderSignedZeroInfNanPreserveFloat32 under core Vulkan 1.2 or later.
Both owned and frontend-shared device setup query the property once, before
workers start, using stack storage. Older APIs or unsupported properties retain
the existing compiler fallback. Unlit fixed-function modules also require this
mode; programmable modules without point sizes do not acquire it.
The mode constrains driver optimization for exceptional values; it is a
correctness requirement, not a promise of cheaper GPU arithmetic or lower frame
latency. Existing first-use PSO stalls remain open.

## Ownership and work

The existing module-cache entry or immutable worker job owns the VshState/key.
Emission borrows that exact state synchronously through a count pass and a write
pass, ending the lend before returning. No guest pointer, temporary string or
mutable FIFO view escapes. There is no intermediate text, compiler AST, growing
word buffer or byte-for-byte module clone.

A supported miss allocates final words once, final UBO metadata once, and final
push metadata only when required (two or three allocations). This is **not** an
allocation-free gameplay claim. Metadata names share their descriptor allocation.
The source-take helper moves those exact owners into existing ShaderSource and
ShaderLayouts references and clears the caller's result. Words remain valid
through module creation; layouts can outlive words through the module's separate
reference. Existing memo publication, cancellation, generation and retirement
ordering are unchanged. No steady-state draw bookkeeping or file I/O is added.

The direct UBO contains vertex-stage parameters. Existing uniform
locations and packed uploads consume the emitted layout, rather than assuming
GLSL compiler offsets. Compact inline attributes retain their existing packing.

## Compatibility decision

State-recipe keys and serialized cache schemas are unchanged. Previously
supported direct modules retain their prior words. Point and supported fixed-function modules explicitly define their floating-
point environment; the worker memo includes the new target property. Module and
pipeline owners remain per-device and retain their generation retirement rules.
Disk seeds contain state recipes, and driver caches identify the resulting shader
words themselves. There is no forced cache invalidation. Warm in-process caches
may bypass translation; a warm driver cache is a separate compilation milestone.

## Regression coverage and qualification

- `test-spv-vsh.py`: 34,960 deterministic SPIR-V modules, final-owner allocation
  counts, poisoned source keys, thirty-nine allocation-free unsupported paths, UBSan and
  spirv-val. Both SPIR-V 1.5 and 1.6 targets are covered. Extracted native device
  setup is checked across 512 API/property/generation cases, including stale
  target reset and zero queries on older APIs.
- `test-spv-vsh-gpu.py`: 36,368 programs / 59,352,576 scalar outputs compared with
  the production GLSL generator/compiler using Vulkan transform feedback. It
  captures all user outputs, position and point size before clipping; numerical
  differences fail (NaN payload identity is not required). It exercises opcode
  pairs, masks, UBO/push inputs, compressed/BGRA inputs, R12 aliasing, relative
  constants, dynamic/static fog and postlude flags. Static tests poison fogCtl
  independently and exercise NaN/Infinity/zero/subnormal distances and parameters.
  Point shaders are compared against production GLSL under the same defined
  FP32 preservation mode, with masked writes and zero/negative/finite/Inf/NaN
  scale values. The reference test adds only capability/execution-mode
  declarations; its arithmetic and stores are unchanged. Unsupported devices
  are checked as selector rejections. Software Vulkan requires validation; an
  explicitly requested physical-GPU run without installed validation layers reports that
  limitation. Missing transform feedback is unqualified.
- Existing worker/module tests additionally exercise 1,024 source-owner moves,
  optional layout owners, prefix views, exact retained pointers/bytes and layout
  lifetime after word release. Existing concurrent publication/refcount tests
  remain enabled.
- GitHub CI runs the CPU ownership/validator suite and software Vulkan output
  comparisons. Physical-GPU gameplay remains a separate requirement.

Local runs on llvmpipe and RTX 5090 passed the supported GPU comparisons.
Windows lacked Vulkan validation layers; llvmpipe validation reported no errors.
An illustrative synthetic translation sample used about 803 ms CPU for GLSL
text plus compilation versus 27 ms direct; this is not a gameplay latency result
or an accepted performance baseline. Final logs retain per-run work and CPU-clock
counts. Gameplay/image/lifecycle evidence is recorded separately in validation
artifacts. Driver caches remain uncontrolled; cold-play smoothness stays BAD/OPEN.

The static-fog extension adds no allocations, retained payload or file I/O.
Its six modes select arithmetic during emission, without loading fogCtl or
emitting a runtime mode switch. Existing layouts stay unchanged. A parent
comparison confirmed all 448 previously accepted fixture modules remain
byte-identical; 672 static-fog fixtures are newly accepted. Static fog semantics
were already part of VshState keys, so existing GLSL modules remain valid and
there is no cache-schema/key change or forced invalidation.

The point-size extension uses the same count/write key lend and final word and
layout allocations. Its float-control declarations add six words, not another
buffer. All 1,120 prior fixture modules remain byte-identical even when the new
host property is true. Worker memo identity includes this target property,
preventing reuse across changed compilation environments; target changes occur
only at quiescent device setup. Native module/pipeline owners remain per-device.
There is no serialized recipe key/schema change. Existing disk seeds store state
recipes, while driver caches identify the new shader words themselves; neither
requires forced invalidation. Legacy non-point modules retain their prior words.

## Unlit fixed-function subset

The direct subset supports static and live CSV0_C, material/base shading and
static and dynamic directional, local and spot lights, as detailed below.
It supports static/live texture matrices and coordinate-generation modes, all
skinning modes and register/attenuated points as detailed below. Unsupported
states outside the supported encodings retain the GLSL path. The existing FP32 property gate applies;
no new device query is added.

The composite matrix already contains model-view when skinning is off. Emit
vector-times-matrix directly from the existing constant array, then perform the
fixed-function perspective divide, viewport offset, four-bit screen rounding,
raw-position exports and depth normalization. Unlike programmable screen-space
results, fixed-function homogeneous z is not multiplied by w a second time.
Colors pass through the common NaN/clamp/specular-alpha postlude. Static texture
matrices consume the existing per-stage constant registers. Fixed point-register
sizes use max(1, pointSizeReg), preserving the reference distinction from the
programmable <=0 fallback. No host matrix snapshot or conversion is introduced.

Allocation counts and layouts remain unchanged. Matrix types/IDs/constants are
emitted only for this new subset; all 2,240 prior fixture modules are byte-
identical to the parent. Fixed-function emission never scans programmable
microcode, even with a poisoned payload and invalid unused program length.
Tests also vary unused normalization/light flags without changing native words.
GPU comparisons link the production fixed-function generator, rather than a
stand-in, and cover asymmetric/perspective matrices, all sixteen texture-matrix
masks, viewport offsets, input formats and exceptional input/point-scale values.
This is new direct-translation coverage, not a cold-play recovery claim.

## Static fixed-function fog

All thirty valid static source/curve combinations are direct. Distance comes
from specular alpha, explicit fog coordinate, or radial/planar/absolute-planar
model-view position, with all six static curves. Planar distance uses
 dot(fogPlane.xyz, modelViewPosition.xyz) + fogPlane.w, not a homogeneous dot.
The shared postlude applies exceptional-value rules and bounds; no runtime
fogCtl/foggen switch is emitted for static modes.

Initial physical-GPU comparisons found eight one-ULP differences and kept the
six transformed-distance quadratic combinations on GLSL. The later dynamic-fog
probe reproduced this class of failure and isolated equal input distances but
different products before exp2. Explicit multiplication-order controls now
recover those six combinations. Failed logs remain retained, and no tolerance
was relaxed. Invalid positive/negative static foggen values remain allocation-
free misses; unused values are ignored with static fog disabled.

Model-view and fog-plane values are loaded from the existing constant array;
there is no new uniform member, matrix snapshot, buffer or retained lend. The
fixed-function key is borrowed only through the two emission passes. All 2,752
prior fixture modules remain byte-identical. The supported CPU/GPU comparisons
include all distance sources, input formats, texture-matrix masks, asymmetric
transforms, exceptional parameters and poisoned fogCtl values. This expands
translation coverage without changing recipe keys, cache schemas or stutter
qualification.

## Static linear texgen

Each of the sixteen coordinates can independently pass through the original
texture attribute, use dot(plane, originalPosition), or use dot(plane,
modelViewPosition). The model-view position is the full homogeneous vector;
there is no normalization or perspective divide before the plane dot. Generated
coordinates then undergo the enabled static texture matrix, including projective
Q. Existing constant-array registers supply all planes and matrices.

One model-view result is shared across eye-linear coordinates and any fog source
that already requires it. Original texture attributes are loaded only when a
component passes through. These are shader value/ID operations, not CPU matrix
or vertex copies. The immutable key is borrowed through count/write emission;
word/layout allocations and native publication/retirement remain unchanged.

Tests add 1,296 fixtures covering all 81 four-coordinate ternary patterns per
stage and all sixteen texture-matrix masks, plus supported fog combinations,
input formats and exceptional inputs. Matrix-operation counts verify that the
model-view result is shared. All 3,712 earlier fixtures retain byte-identical
modules. Negative/unknown modes, invalid static sphere/reflection components,
local/spot/dynamic lighting and dynamic CSV0_C retain GLSL fallback.
No key schema or driver-cache defaults change, and this does not establish
cold-play recovery.

## Static normal-map texgen

Unlit, unskinned static texgen can also select the transformed normal for S, T
and R independently, mixed with disabled and linear coordinates. Transform
vec4(normal.xyz, 0) by the existing inverse-model-view matrix, take xyz, and
normalize only when the programmed normalization flag requires it. Share this
result across all stages and generated components before texture matrices.
Static normal-map Q remains unsupported; dynamic and sphere/reflection cases use
the viewer-aware path below.

The shader reads input/constant owners already used by the renderer. No CPU
matrix materialization, additional buffer, retained pointer, allocation or payload
copy is introduced. Normalization is a consumed key field for these states;
normal-free states still ignore it. All 5,008 previous fixture modules remain
byte-identical. Additional fixtures span 192 mixed coordinate patterns and all
sixteen texture-matrix masks, raw/normalized normals, compressed inputs and
exceptional values. Operation-count assertions require one shared inverse-model-
view transform and at most one normalization, regardless of coordinate count.
This expands direct translation; it does not establish cold-play recovery.

## Static sphere and reflection texgen

Sphere mapping supports S/T and reflection mapping supports S/T/R, mixed with
normal, linear and passthrough sources. Both consume the live TEXGEN_REF viewer
bit in texgenMode[0], independent of lighting LOCALVIEWER. The direct layout adds
the existing four-element uint array only when these modes require it, after any
inline attributes (64 bytes including std140 padding). Previous layouts and
all 8,080 prior fixture modules remain unchanged. Existing tracked uniform
updates fill this member; no new dirty marker, cache key, schema or per-draw
owner is introduced.

A single viewer branch selects the fixed forward direction or normalizes the
model-view position. Share that eye result, the inverse-model-view normal and
one Reflect instruction across coordinates/stages. Sphere coordinates also
share 1 / (2 * length(reflection + vec3(0,0,1))), then apply component * scale
+ 0.5 before optional texture matrices. No arithmetic identity or approximation
replaces the reference formula.

An initial eager Normalize plus Select version produced 500 scalar differences
on RTX 5090; software Vulkan matched. Matching the reference's lazy conditional
removed the differences in the focused physical-GPU rerun. The exact driver
optimization responsible is unresolved. Retain the failed logs and strict
comparisons; do not infer a driver defect or relax numerical tolerances.

Additional fixtures cover 128 mixed coordinate patterns across all sixteen
texture-matrix masks, both live viewer settings, normalized/raw and compressed
normals, inline inputs, exceptional values and unrelated live mode-bit noise.
Contracts check one viewer read, one shared reflection and shared transforms.
There are still two or three final-owner allocations per supported translation;
only final metadata size and shader words change. No intermediate module clone,
CPU matrix snapshot, retained lend or file I/O is added. Sphere R/Q and
normal/reflection Q retain allocation-free fallback, along with existing
local/spot/dynamic lighting and dynamic CSV0_C exclusions. This is expanded
translation coverage, not proof that cold-play PSO stutter has recovered.

## Fixed-function point attenuation

Unlit fixed-function points can use the eight live pointParams values without
GLSL. Compute length(originalPosition * modelViewMat0) as a homogeneous vec4
length; xyz distance or distance after the composite transform is not equivalent.
Reuse a model-view result already required by fog or texgen. Keep the reference
polynomial evaluation order, explicit 1 / sqrt, offset, scale and clamp, including
the 63.875 minimum/maximum size cap. The common postlude applies pointScale once.
No reciprocal-square-root approximation replaces the reference arithmetic.

Only affected layouts append the existing float pointParams[8] array, with
std140 stride 16 (128 bytes), after inline attributes and any texgenMode array.
Existing tracked updates populate its final block. Indices 4/5 are not consumed
by the shader; pointSizeReg is ignored for attenuated points. Names remain inside
the final layout allocation, with two or three total final-owner allocations.
No additional heap allocation, matrix snapshot, payload clone or retained lend
is introduced. Previous module words and every prior layout member retain their
exact name, dimensions, stride and offset.

An additional 1,024 fixtures combine all texture-matrix masks, supported static
fog/texgen, both viewer settings, input formats and normalized/raw normals with
point attenuation. GPU inputs vary every parameter across zero, signed zero,
positive/negative finite, Infinity, NaN and tiny values, including unused 4/5;
point scales/registers are independently varied. Hardware FP32 preservation
remains required, and local/spot/dynamic lighting and dynamic CSV0_C retain GLSL fallback. No cache schema or file-I/O path changes.
These checks establish translation equivalence, not cold-play recovery.

## Fixed-function skinning

All six weighted modes can emit directly while lighting is off: two, three or
four matrices with individually supplied weights, or a generated final weight
for two/three/four matrices. The generated value starts at one and subtracts each
supplied weight in order; weights are neither clamped nor renormalized. Keep the
reference zero-initialized accumulation and sequential transform * weight adds.
Position and normal transforms share the same scalar weight IDs, then normalize
the accumulated normal only if requested. Bone matrices are read directly from
the existing constant array (eight registers per model-view/inverse pair).

Skinned model-view position feeds fog, eye-linear coordinates and viewer-aware
texgen, then the composite matrix produces clip position. Object-linear texgen
still consumes the original input position. Point attenuation deliberately
continues to use originalPosition * modelViewMat0, not the blended position.
These distinctions have separate GPU comparisons and matrix-count assertions.
No geometry/matrix snapshots, new layouts, allocations or retained pointers are
introduced. Existing immutable key and constant-buffer owners keep their current
publication and last-consumer lifetimes. All 11,152 prior fixture modules and
uniform members remain identical; negative and out-of-range modes are allocation-
free fallback misses.

An additional 1,536 fixtures span all six modes, all sixteen texture-matrix
masks, static fog/texgen, viewer settings, normalized/raw/compressed inputs and
point attenuation enabled/disabled. GPU weights include basis vectors, decimal
fractions, negative/above-one sums and exceptional values, in vertex and inline
storage. The GPU driver runs the full 19,984-fixture corpus in three bounded
batches, preserving every case while retaining a separate 300-second watchdog
per batch. Work counts and CPU-clock totals remain separate from gameplay
latency evidence; local/spot/dynamic lighting and dynamic CSV0_C remain open.

## Dynamic fixed-function texgen

Unlit texgen families can read all sixteen coordinate modes and the four matrix
bits from existing texgenMode/texMatEnable uniforms. Static coordinate modes and
matrix-enable flags are then ignored, including poisoned unused fields. The
three-bit mode selects passthrough, eye/object linear, sphere, normal or default
reflection (including values 6/7). Sphere R/Q and normal/reflection Q return zero
in this dynamic path, matching the production reference; static invalid
component/mode pairs retain their earlier fallback rules.

Reuse the model-view and normal transforms across coordinates, including all
skinning modes, plus one lazy viewer branch, reflection and sphere reciprocal.
Each coordinate uses structured selection, and each texture matrix executes only
when its live bit is set. Input IDs shared between branches are materialized in
a dominating block; no pointer into a recyclable input view is retained. The
helper's eye-position pointer is a synchronous lend of a local scalar SPIR-V ID,
not a geometry or uniform-buffer lend.

Only dynamic families add texMatEnable to the final layout (four bytes in a
sixteen-byte std140 tail). texgenMode[4] uses the existing final member and its
first load also supplies the viewer bit. Five uniform reads cover four mode
words and one matrix mask. Point parameters retain their earlier member and
offset; no new allocations, matrices/vertices copied on the CPU, payload clones
or file I/O are introduced. The immutable job/cache key remains borrowed only
through count/write emission, with existing native publication/retirement.

An additional 1,024 fixtures cross all eight live mode values with every matrix
mask and both viewer, normalization and point-attenuation settings, rotating
through all skinning modes. They cover raw/compressed and
inline inputs, exceptional values and unrelated high-bit noise. CPU contracts
verify ignored static state, five control reads, sixteen selections, four guarded
matrix operations and shared transforms. All 12,688 earlier module words and
uniform members remain identical despite sharing the normal/reflection emitters.
Dynamic CSV0_C and local/spot/dynamic lighting still use GLSL. Expanding direct translation does not establish
cold-play PSO recovery.

## Dynamic fixed-function fog and quadratic precision

Live fogCtl selects all eight source values, all eight curve values and enable
state. Source 0 clamps raw specular alpha, 1 uses radial position, 4 reads the
fog coordinate, and remaining sources use planar distance (absolute only for 3),
matching the reference defaults. Share the skinned/unskinned model-view result
with texgen and cache one uniform read for both source and curve selection.
Pass the scalar distance directly to the postlude, avoiding an intermediate
vec4 construction/extraction. No layout, buffer or resource owner is added.

The initial physical probe reported 64 differing fog outputs; software matched.
Extra test-only captures preserved the original failures while locating equal
distances and a one-ULP difference in the pre-exp2 argument. For example,
distance c237ce66 and density -.01 produced reference argument c0d838e9 versus
native c0d838e8; the latter matches a regrouped density square. This is consistent
with permitted reassociation, not evidence of a driver defect. Four
NoContraction annotations pin the quadratic products to the reference order
alongside the already required FP32 SignedZeroInfNanPreserve mode. The
[Vulkan floating-point rules](https://docs.vulkan.org/spec/latest/appendices/spirvenv.html)
describe these operation-order controls.

The count pass records exactly four result IDs in bounded stack storage. The
write pass emits their annotations directly in the final buffer before types,
checks that those IDs repeat, and writes every word once. There is no
post-construction module patch, intermediate word buffer or payload clone.
Previously accepted modules receive no annotations and stay byte-identical.
Allocation counts, immutable-key lends, uniform owners, native publication and
retirement remain unchanged. This constrains arithmetic optimization where
precision requires it; it is not an isolated GPU-speed or stutter claim.

New coverage includes 4,096 dynamic source/curve/enable/matrix-mask fixtures
with finite and exceptional parameters, plus 768 recovered static quadratic
fixtures. Skinning, texgen, points and input formats vary across them. CPU
contracts require four precision annotations only on affected shaders, one
shared control read, ignored static fog fields and shared transforms. All
13,712 prior modules/layout members remain identical. The regression retains
initial failures and strict comparisons; cold-play PSO recovery remains open.

## Material and directional lighting

Static fixed-function lighting supports all eight directional-light slots and
all 256 on/off masks. Local, spot and dynamic light modes remain correct GLSL
fallbacks pending their native implementation. The material base follows the
existing ambient/emission-source formula, with all three valid source values,
material/diffuse/specular alpha selection, two-sided back material colors and
separate/combined specular behavior. Back light colors use their own register
rows and negated normal dots, preserving their distinct source rules.

Directional vectors are normalized; programmed infinite half vectors are not.
Local-viewer half vectors use normalized direction + eye vector, with explicit
component division by homogeneous w. Specular uses the hardware-coefficient
rational model and its exact cutoffs, denominator guards and 10000 bound. Its
division executes only on the valid branch. Disabled specular calculations and
associated uniform groups are omitted. The normal result is cached as an SSA ID
across texgen and lighting, preserving one weighted transform/normalization.
This cache is local to the emission call and contains no guest pointer.

A profile appends only consumed existing lighting uniform groups to the final
UBO layout, with std140 offsets and array strides. It does not create additional
bindings, buffers, staging copies or allocations. Names share the same final
metadata allocation; final words/layout owners and native retirement remain
unchanged. Existing tracked source updates populate these groups. No geometry,
matrix or immutable-key payload is cloned, and no file I/O is added. All 18,576
prior fixture modules and layouts remain byte-identical.

An additional 4,096 fixtures cross all directional masks with two-sided,
local-viewer, specular-enable and separate-specular flags, rotating through all
81 material-source combinations, skinning, points, fog and texgen. GPU inputs
include nonzero scene/light registers, asymmetric directional/raw half vectors,
material alphas and exceptional/zero/negative-denominator specular coefficients.
Small light colors keep the specular cutoff bound observable before final color
clamping.
CPU checks cover profile membership, guarded specular paths, normal sharing,
final allocation counts, poisoned keys and unsupported local/spot/dynamic modes
and material-source values. The growing GPU corpus retains every case across
five bounded batches with the same per-process watchdog. This is a foundation
for the remaining light modes, not completion of direct lighting or cold-play
PSO recovery.

## Local and spot lighting

Static local and spot lights use the same eight slots, front/back colors,
material-source rules and coefficient specular model as directional lights.
Existing LTC1 ranges, local positions and attenuation coefficients are appended
only when consumed. The range test is inclusive and rejects NaN distances/ranges;
out-of-range lights preserve the incoming colors through structured merge values.
The homogeneous view-position divide is emitted at the first local-light use,
after preceding infinite lights, matching the reference evaluation order.

Distance attenuation preserves the reference polynomial order and reciprocal.
Local half vectors retain the second normalization even for an infinite viewer.
Spotlights consume the cooked context vector directly, with the existing rational
falloff cutoffs, denominator guards and final clamp. A zero factor selects zero
instead of multiplying zero by infinite attenuation. Branches prevent evaluation
of invalid guarded divisions. No geometry, constants, matrices or keys are cloned;
new groups use the existing final uniform metadata/word allocations and ownership.

The extra 4,096 fixtures exercise isolated local/spot slots and mixed light types,
all skinning modes, both viewers, front/back and separate/combined specular states.
Inputs include inclusive ranges, invalid/NaN ranges, polynomial coefficient edge
cases, hard-edge cooked cones and NaN/zero/negative falloff denominators. Existing
GPU cases remain in the corpus. Dynamic lighting and dynamic CSV0_C remain GLSL
fallbacks. Earlier sections describe the coverage available at their milestones.

The first physical-GPU run found two one-ULP back-color differences in case
27472, while software Vulkan matched. Diagnostic contraction and intermediate-
output probes localized this to evaluation around the homogeneous view-position
subtraction. Broad explicit FMA changed other results and was rejected. Moving
the divide to its first local-light use matches the reference evaluation order
without new precision decorations. The final full corpus matched 45,983,232
scalar outputs on each of software Vulkan with validation and the RTX 5090
without installed validation layers. Failed probes remain retained. All 22,672
prior CPU fixture modules/layouts remain byte-identical to the parent. These are
correctness/work-count results; driver caches remain uncontrolled and cold-play
and draw-path PSO recovery remain open.

## Dynamic light modes

With static CSV0_C, the direct path also consumes live light modes and counts
from the existing `lightMode` uniform. One counted shader loop covers the eight
slots; it does not unroll eight copies of the lighting body. Uniform production
bounds the count to the highest active slot (zero through eight), while the
shader retains the reference low-four-bit count mask and ignores higher bits.
Disabled slots and slots beyond the count leave the carried colors unchanged.

A mode branch chooses directional or local geometry, and the local path applies
the cooked spotlight curve only for spot mode. The dynamic reference skips a
local light only for `distance > range`; its NaN behavior differs from the
static inclusive range branch and is preserved. Range-rejected terms do not
reach accumulation. Geometry results and all four colors flow through structured
SSA merges and loop-carried values. These CPU-side temporaries contain only
32-bit IR IDs, with no vertex, matrix, uniform or immutable-key snapshots.

The material sources, viewer, normal normalization and specular settings remain
static in this stage. Two-sided shaders consume the existing live back-light bit
from `csv0cCtl` for per-light back contributions, as the reference does. Shaders
that ultimately pass back colors through omit these unused contributions and
the control group. Dynamic CSV0_C remains a correct GLSL fallback.

Only consumed existing uniform groups are appended to the final metadata owner.
The same immutable module-cache/worker key is lent synchronously through count
and write passes, and no pointer escapes. Final word/layout owners, allocation
counts, module adoption, cancellation and last-consumer retirement are unchanged.
No intermediate text/AST, payload clone, new binding, file I/O or draw-side
bookkeeping is introduced. All 26,768 previous CPU modules/layouts remain
byte-identical to the parent.

The extra 4,096 fixtures exercise runtime mode patterns and counts zero through
eight, disabled holes, poisoned bits outside the count and upper control bits,
front/back flags, all viewers and skinning modes, points, fog and texgen. They
retain local/spot exceptional coefficients, NaN/zero/negative ranges and cooked
hard-edge cones. CPU checks poison the unused static mode array between emissions,
validate loop structure and existing final allocation counts, and retain failure
fallbacks and capability/generation checks. Full GPU and gameplay qualification is recorded below; this does not establish
cold-play or draw-path PSO recovery.

Physical-GPU probes exposed a divide/subtract contraction boundary for the local
viewer. The GLSL reference has a pre-loop homogeneous divide shared with its eye
vector; that rounding remains observable even when disabled specular makes the
eye-vector result dead. Native emission pins the local-viewer light-position
subtraction with one NoContraction annotation. Its ID is recorded by the count
pass and emitted directly into final words, with no module patch buffer or extra
allocation. Applying the control to every viewer changed other previously
matching cases and was rejected; those failed logs remain retained. The final full corpus matched 52,667,904 scalar outputs per device on software
Vulkan with validation and the RTX 5090 without installed validation layers.
The rebuilt core completed 26 OpenGL/Vulkan game checks, four lifecycle cases
and five exact image pairs. Driver caches and timings remain unqualified;
cold-play and draw-path PSO recovery remain open.

## Live CSV0_C

The whole CSV0_C path reads one existing control uniform for lighting enable,
normal normalization, viewer, all four material sources, two-sided lighting,
specular enable/combine and alpha control. Static copies of those fields and
light modes are ignored. Existing live lightMode/count selection retains one
counted shader loop. Reserved material source values follow the reference's
per-use defaults, including its distinct base-color and per-light rules.

Normalization is selected lazily before its normal SSA value is shared between
texgen and lighting. The lighting master branch carries input colors through
when disabled; the specular postlude remains live outside that branch. Point and
fog consumers after the master merge use a position ID defined in a dominating
scope. Temporary color arrays contain four SSA IDs, not vertex/color payload
snapshots. No guest or mutable uniform pointer is retained across the lend.

All potentially consumed existing lighting groups are appended to the same final
uniform metadata allocation. The immutable cache-entry/worker key remains the
owner through synchronous count and write passes. Words and metadata transfer
through existing native references and retirement; no new allocation count,
resource binding, geometry/matrix/key copy, file I/O or draw bookkeeping is added.

A static local viewer and the live viewer have different reference expression
shapes: live VPeye is conditional. Carrying the static viewer's NoContraction
boundary into the live path caused physical-GPU differences. The live subtraction
retains reference contraction freedom; the already-qualified static-viewer
control is unchanged. Failed probes remain retained, with no tolerance changes.

An extra 4,096 CPU fixtures poison ignored static fields and check ownership,
layout bounds, one shader loop and structured branch/normalization counts. The
GPU fixtures cover all 256 material-source combinations, all 128 live flag
patterns across material-dependent pairwise cells, poisoned reserved bits,
light counts zero through eight and existing exceptional local/spot coefficients.
The final corpus matched 59,352,576 scalar outputs per device on software Vulkan
with validation and the RTX 5090 without installed validation layers. All 48
contract suites passed; the rebuilt core completed 26 OpenGL/Vulkan game checks,
four lifecycle cases and five exact image pairs. Driver caches and timings remain
unqualified. Cold-play smoothness and draw-path PSO recovery remain open.

Constant-row emission reuses one SSA value when the selector, A0 definition and
basic block are unchanged. The cache contains four 32-bit IDs/indices, no
uniform payload or pointer. A different block ends reuse even when an earlier
value could dominate it; this conservative scope avoids retaining branch-local
values. An A0 update changes the relative-address key. Direct and relative reads
remain distinct, and each count/write pass starts with an empty cache.

Three constant operands need one access/load pair rather than three: operand
setup falls from 57 to 37 words, or 72 to 42 for relative reads. These are operand
work counts, not whole-shader or frame-time percentages. Arithmetic, swizzles,
negation, layouts and fallback eligibility are unchanged. Existing persistent
modules remain compatible because the ABI and results are unchanged; no cache
schema/key change or invalidation is needed.

Signed-integer declaration selection uses a 256-bit mask owned by the count
pass on the compiler stack. The synchronous write pass borrows that mask until
emission completes and records its own mask to verify agreement. Each first
use charges four declaration words in the count pass. Reserved IDs are kept,
so removing declarations does not renumber arithmetic or other instructions.
No key, payload, mask or stack pointer escapes in the result; final word/layout
ownership and allocations are unchanged. The mask replaces the conservative
256-entry declaration-selection table. Existing modules remain ABI-compatible.

The integer-lend test covers 262,144 unique/repeated/index/generation cases and
full two-pass emission; controls require detection of wrong declaration charges,
mask-bit aliasing and duplicate charges. CPU parent comparisons additionally
require identical modules after excluding signed-integer declarations, unchanged
layouts and no word-count increase. Smaller modules do not establish PSO
readiness, GPU execution speed or cold-play recovery.
