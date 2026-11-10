# Direct SPIR-V coverage audit

This audit checks the current reference-implemented shader paths. It does not
certify every hardware/game state, ready pipelines, or recovery of cold-play
stutter. The established GLSL fallback remains required for unsupported states.

## Fragment family matrix

The independent production GLSL generator and compiler are compared with native
eligibility across 17 texture families. The original 384 shape cells per family
vary dimensions, rectangle/border/cube/packed flags, shadow formats and depth
comparisons. The expanded mode additionally crosses three convolution tags,
four color-key modes and two point-sprite settings (24 profiles).

The full matrix has 156,672 cells: 79,680 hit excluded reference assertion
preconditions, leaving 76,992 compiler candidates. Of these, 67,008 compile in
both paths; native eligibility must match reference compilation in every case.
Both modules of each pair pass Vulkan 1.3 SPIR-V validation (134,016 modules).
The original profile independently retains its 3,360 candidates, 2,880 compiled
pairs and 3,168 assertion exclusions. No old cases are replaced by easy cases.

Nine selected witnesses execute the real reference assertions, including the
new convolution-dimension and point-sprite/rectangle exclusions. Mutation
controls require detection of a lost projective-family path and a corrupt
module passed to the actual validator. This is eligibility and module validity,
not numerical GPU equality for the expanded cross-product.

On hosts with the native SPIRV-Tools library, the validator synchronously borrows
the final `GlslcResult.words` span. Each result remains owned until validation
returns; diagnostic objects are destroyed and the context is released at the
end. No module file or second word buffer is created. Validator-internal
allocations are not claimed to be absent. CI explicitly requires this mode.

Default auto mode treats a missing library as unqualified and fails. The
explicit CLI mode retains the original 5,760-module base and clearly reports
expanded coverage as unqualified. Set
`SPV_FAMILY_VALIDATOR=cli` to exercise that path; setting
`SPV_FAMILY_REQUIRE_IN_PROCESS=1` makes missing expanded capability a failure.
The larger correctness matrix has a separate watchdog; no runtime performance
acceptance threshold changes.

## Static combiner register audit

The family-test entry point also checks 17,024 static-combiner states against
production GLSL compilation. Source cells vary all 256 source-byte encodings at
each of the eight RGB/alpha general inputs and seven final inputs, excluding
reserved registers 6/7 and EF references before E/F have been evaluated. Output
cells cross all 256 operation/mapping/blue-to-alpha flag encodings with nine
legal destinations and each of the six AB/CD/SUM RGB/alpha destination slots.
Read-only constants, fog and sum/product registers are not legal destinations;
`psh_regs.h` records the read/write distinction.

There are 15,232 reference/native pairs and 1,792 matching reference compiler
failures. The failures are mux outputs without any explicit R0 reference or
write: the reference then emits a mux read without declaring R0. Preserve the
native guard rather than counting these failures as missing valid coverage.
All 30,464 modules validate through synchronous borrowed final-word spans;
result owners remain live through validation, and no module files or secondary
word buffers are created. Undefined-behavior checks are enabled for this audit.
Mutation controls require detection of a supported source forced to fallback
and a corrupt module sent to the actual validator. CI runs this through the
existing fragment contract; explicit base-only CLI mode does not qualify it.

These cells exercise one source/destination slot at a time in a single general
stage. They do not prove numerical GPU equality for arbitrary combiner graphs,
simultaneous destination aliases, multistage dependencies or mixed texture
families. Existing value/image evidence remains separate. No runtime changes or
PSO-readiness/cold-play recovery follow from this test-only expansion.

## Fallback boundaries inspected

- Vertex binding/capability guards reject unsupported host layouts and preserve
  the existing path when the required FP32 controls are unavailable. The fixed
  state guards mirror reference limits for skinning, fog and texgen channels;
  reserved material/light selections are not evidence of missing valid output.
- Programmable vertex guards reject missing final tokens, out-of-range programs,
  unknown MAC opcodes, invalid consumed operands and unsupported output writes.
  The reference asserts a final token; it declares R0-R11 and the R12/oPos alias.
  Masked/unselected operands already have separate supported coverage. This
  inspection does not prove arbitrary new microcode sequences by itself.
- Fragment texture-shape guards retain reference assertion/type-error cases,
  including dependent/rectangle shapes, unsupported shadow sampler families,
  invalid projected convolution dimensions and point-sprite rectangles. The
  expanded independent compiler matrix checks their acceptance boundaries.
- BRDF and constant specular-reflection modes remain unimplemented placeholders
  in the reference. Translating their placeholder output would not establish
  correct native coverage; preserve their established fallback.
- Static combiner register/self-reference guards and host binding limits remain
  separate audit requirements. Family parity alone does not cover all combiner
  graphs or all multiple-active-sampler products.
- Geometry selectors cover the current Vulkan line, triangle and quad paths.
  Wide-line expansion is OpenGL-specific; unequal front/back polygon modes and
  polygon points are unsupported by the current reference. UI clamp selection
  in the state builder excludes line primitives. These source findings do not
  substitute for the existing geometry value tests.

The Vulkan source audit finds the module fallback in `vk/shaders.c` and an
unused create-from-GLSL wrapper in `vk/glsl.c`; fixed utility creation uses
immutable SPIR-V modules. Removing an unused wrapper would not establish a
runtime performance win. A module being native or prepared is distinct from a
ready PSO; synchronous misses remain an independently tracked violation.

## Qualification boundary

The bundled dictionary after seed migration has 9,334 conditionally eligible
Vulkan entries and 9,305 geometry entries; four dictionary games contain no
Vulkan entries. Four-game image/lifecycle evidence and software/physical Vulkan
value comparisons remain separate from this CPU audit. The initial NGB 1x
capture failures during routing qualification are retained; subsequent isolated
fresh-process warm-private-cache repeats matched exactly. Driver caches remain
uncontrolled and cold play remains BAD/OPEN.

No new native eligibility gap was found by the expanded family matrix. Full
practical coverage is not inferred from those counts: static-combiner graphs,
multiple-active-family interactions, factory reachability and host capability
fallbacks still require their corresponding evidence before completion.
