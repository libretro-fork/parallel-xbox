# Unused programmable C operands

The production decoder formats C for every non-NOP token, so even an unused
C mux must be legal. Register/constant text is discarded when neither MAC nor
ILU consumes C. Native eligibility now distinguishes formatting from use:
unused bounds do not trigger GLSL fallback, while actual sources retain their
register/constant bounds. Keys remain immutable synchronous lends; the emitter
and final word/layout owners are unchanged. No new allocation or payload copy.

1024 paired CPU cases require canonical word equality, unchanged allocation
counts, poisoned-key owner survival and both SPIR-V targets. Sixteen rejection
controls keep invalid muxes and consumed out-of-range operands allocation-free.
GPU cases compare the production reference for all eleven MAC opcodes that do
not consume C, including ARL followed by a relative constant read. Prior GPU
recipes remain unchanged. Translation coverage does not establish PSO readiness
or cold-play recovery; driver caches remain uncontrolled and BAD/OPEN.

GPU recipes are fully constructed before either module is generated. A
fixture-only state/options digest guards against later recipe mutation without
a key snapshot. Initial signed-zero failures came from a fixture-order bug
that changed state after compilation and skipped fixed-function reference FP32
setup; those runs and policy diagnostics remain retained investigation evidence,
not passes. The renderer/compiler FP32 policy remains unchanged.

## Masked instructions

Bounds checks follow emitted reads, rather than opcode arity alone. A masked MAC or ILU result emits no operand reads. Paired MAC writes to R1 are discarded; paired ILU writes still use R1. ARL always updates A0, even when its ordinary masks are zero, so its A operand remains live. Mux validation still follows the reference decoder, which formats operands before discarding masked results.

The regression fixture adds 1,024 native/canonical word comparisons, checks unchanged final-owner allocation counts and poisoned input lifetimes, and restores each discarded operand to a live write to require allocation-free rejection. Invalid decoded muxes also reject with all results masked. Vulkan comparisons construct the complete key before compilation, cover paired live results and observe ARL through a subsequent relative-constant load. No payload is copied or retained by the eligibility checks.

## Outputs selecting a NOP pipe

A nonzero output mask does not imply an output reference: the mux-selected
pipe must also contain an opcode. The reference decoder never examines the
output destination when the selected pipe is NOP. Native eligibility likewise
ignores only that destination, while retaining temporary writes, ARL's A0
updates, source bounds and eager mux validation. Invalid destinations of active
pipes still reject. Keys, emitted instructions, allocation counts and final
word/layout ownership are unchanged.

The 1,024 added CPU pairs compare canonical and ignored-destination native
words, allocation counts and layout bounds, then poison the borrowed key. Each
case flips the mux to the active pipe and requires allocation-free rejection.
The GPU cases compare the production reference with both MAC and ILU active
in the non-selected pipe, including ARL followed by relative constant use.

## Relative constant selectors

For an A0-relative constant source, the eight-bit selector is an offset;
A0 + selector is the effective array index. Selectors 192 through 255 can
therefore address valid constants when A0 is negative. Native eligibility
accepts those offsets using the existing signed-add/index emission. Consumed
non-relative selectors outside 0..191 still reject. This does not add bounds
clamping or define out-of-bounds GPU accesses. Eager mux and temporary-register
validation are unchanged, as are immutable worker-key lends and final owners.

The 1,024 new CPU cases require deterministic words/layouts, unchanged final
allocation counts and poisoned-key owner survival, then clear relative addressing
and require allocation-free rejection. GPU recipes cover A, B and C sources,
array and uniform attribute 15, inline UBO/push constants and both shading modes.
Array A0 values are -96..-65 and effective indices are 96..190; uniform A0 values
are also bounded and checked before expected-value reads. Both modules must
match 180,224 independent expected constant values for MOV cases, alongside
1,671,168 full reference/native scalar comparisons per device. No payload
snapshot or additional allocation is introduced by this eligibility change.
