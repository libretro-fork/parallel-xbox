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
