# Direct programmable vertex emission

Vulkan module preparation now tries NV2A programmable microcode -> SPIR-V
before generating GLSL. Geometry emitters and the established GLSL compiler
remain available. This is a translation-work reduction, not a claim that
first-use pipeline stutter is fixed or that draws never compile pipelines.

## Coverage and fallback

The emitter handles the fourteen MAC and eight ILU encodings, paired writes,
register/output masks, R12/oPos aliasing, constant addressing, uniform and vertex
inputs, compressed inputs, BGRA swizzles, dynamic/disabled fog, all six valid
static fog modes and the Vulkan vertex postlude. Unsupported encodings, constant writes, fixed-function shaders,
invalid static fog modes and programmed point-size states return to the existing
compiler without allocating an unsuccessful direct result. The selector is
conservative; it does not replace unknown work with a placeholder.

An initial expanded RTX 5090 transform-feedback comparison found sixteen
exceptional RSQ/LOG point-size differences. Explicit special-case control flow
reduced that to eight, with unstable reference point-size values. Software
Vulkan matched both versions. The cause is unresolved: do not call this a driver
bug or accept those mismatches. All programmed point-size states remain excluded.
The failed logs are retained with the local validation artifacts.

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

Module/pipeline identity and consumed semantics are unchanged. Existing cached
GLSL-generated modules remain valid, including their own reflected layouts.
There is no cache-key/schema bump or forced invalidation to make the new path
appear faster. Warm caches may bypass emission entirely. Newly emitted modules
carry their own layouts through the existing owner and disk-cache machinery.
Shader compiler optimizations and driver pipeline compilation remain separate.

## Regression coverage and qualification

- `test-spv-vsh.py`: 1,120 deterministic SPIR-V modules, final-owner allocation
  counts, poisoned source keys, twelve allocation-free unsupported paths, UBSan and
  spirv-val. Both SPIR-V 1.5 and 1.6 targets are covered.
- `test-spv-vsh-gpu.py`: 1,568 programs / 2,558,976 scalar outputs compared with
  the production GLSL generator/compiler using Vulkan transform feedback. It
  captures all user outputs, position and point size before clipping; numerical
  differences fail (NaN payload identity is not required). It exercises opcode
  pairs, masks, UBO/push inputs, compressed/BGRA inputs, R12 aliasing, relative
  constants, dynamic/static fog and postlude flags. Static tests poison fogCtl
  independently and exercise NaN/Infinity/zero/subnormal distances and parameters. Excluded point-size states are checked as
  selector rejections. Software Vulkan requires validation; an explicitly
  requested physical-GPU run without installed validation layers reports that
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
