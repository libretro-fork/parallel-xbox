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
the existing compiler fallback. Other direct modules do not acquire this mode.
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

The direct UBO contains only programmable-stage parameters. Existing uniform
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

- `test-spv-vsh.py`: 2,752 deterministic SPIR-V modules, final-owner allocation
  counts, poisoned source keys, twenty-one allocation-free unsupported paths, UBSan and
  spirv-val. Both SPIR-V 1.5 and 1.6 targets are covered. Extracted native device
  setup is checked across 512 API/property/generation cases, including stale
  target reset and zero queries on older APIs.
- `test-spv-vsh-gpu.py`: 4,160 programs / 6,789,120 scalar outputs compared with
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
all coordinate texgen modes disabled, fog/dynamic fog off and distance-based
point parameters off. Enabled static texture matrices are supported. Other states
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
