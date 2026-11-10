# Direct vertex emission

Vulkan module preparation tries NV2A programmable microcode -> SPIR-V and
a bounded unlit fixed-function subset before generating GLSL. Geometry emitters
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

- `test-spv-vsh.py`: 11,152 deterministic SPIR-V modules, final-owner allocation
  counts, poisoned source keys, thirty-one allocation-free unsupported paths, UBSan and
  spirv-val. Both SPIR-V 1.5 and 1.6 targets are covered. Extracted native device
  setup is checked across 512 API/property/generation cases, including stale
  target reset and zero queries on older APIs.
- `test-spv-vsh-gpu.py`: 12,560 programs / 20,497,920 scalar outputs compared with
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

The direct subset requires lighting off, skinning off, static CSV0_C and texgen,
coordinate texgen restricted to disabled/eye-linear/object-linear modes, dynamic
fog off and register-based points. Static fog and distance-based point parameters
are supported as described below. Enabled static texture matrices are supported. Other states
retain the GLSL path, including dynamic lighting/texgen/fog and skinned vertices.
The existing FP32 property gate also applies; no new device query is added.

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

Twenty-four combinations are direct: specular-alpha and explicit fog-coordinate
sources with all six static curves; radial, planar and absolute-planar sources
with linear/exponential curves and their absolute variants. Distance is computed
before projection from the original attribute or the existing model-view matrix.
Planar distance uses dot(fogPlane.xyz, modelViewPosition.xyz) + fogPlane.w,
not a homogeneous dot product. The shared static fog postlude applies the curve,
exceptional-value result and bounds. No runtime fogCtl/foggen switch is emitted.

The first 30-combination physical-GPU run exposed eight one-ULP fog differences
for transformed-distance quadratic curves. Software Vulkan matched that run.
The cause is unresolved; no tolerance was relaxed and the failed logs remain.
All six radial/planar/absolute-planar plus quadratic/absolute-quadratic combinations
therefore stay on GLSL for every device. Invalid positive and negative foggen
values are allocation-free misses; an unused foggen value remains ignored when
fog is disabled. Tests cover this exclusion alongside the supported combinations.

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
modules. Negative/unknown modes, invalid sphere/reflection components, dynamic texgen,
lighting/skinning and previously recorded fog exceptions retain GLSL fallback.
No key schema or driver-cache defaults change, and this does not establish
cold-play recovery.

## Static normal-map texgen

Unlit, unskinned static texgen can also select the transformed normal for S, T
and R independently, mixed with disabled and linear coordinates. Transform
vec4(normal.xyz, 0) by the existing inverse-model-view matrix, take xyz, and
normalize only when the programmed normalization flag requires it. Share this
result across all stages and generated components before texture matrices.
Normal-map Q and dynamic texgen remain unsupported; sphere/reflection cases use
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
lighting/skinning, dynamic-state and quadratic-fog exclusions. This is expanded
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
remains required, and lighting/skinning, dynamic-state and recorded quadratic-
fog exclusions retain GLSL fallback. No cache schema or file-I/O path changes.
These checks establish translation equivalence, not cold-play recovery.
