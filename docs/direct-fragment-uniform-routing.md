# Fragment uniform routing

A fragment compile now computes dense UBO member indices once. The 28-byte
`UniformMap` contains a prefix index for each of the 27 logical uniforms plus
its terminal count. Adjacent indices also identify enabled optional members.
The existing uniform predicates run 27 times to build this table; individual
uniform reads no longer rescan earlier predicates. Count/write declarations
and final layout construction consume the same table.

The public compiler stack owns the map from initialization after eligibility
validation until count, write and layout emission finish synchronously. Each
context borrows a const pointer during its pass. Neither the map nor a source
pointer enters the final word/layout owners, worker queue, module or GPU work.
The key remains immutable through the existing compile lend. Each compile
rebuilds the map, including after generation, state, reset or renderer changes.
There is no heap allocation, payload clone, persistent memo or new pool.

Arithmetic, SPIR-V IDs and words, descriptor/layout values, final allocations,
key schemas and fallback eligibility are unchanged. Unsupported states still
return before map construction. Existing modules remain compatible; no cache
invalidation or file-I/O change is required. Other compiler metadata costs,
including fragment layout-name copies and offset calculations, remain separate
optimization opportunities.

The production routing test checks 442,368 access/member/generation cases on
native and ARM64, canaries, optional members and terminal counts. It requires
27 profile queries per initialization and zero profile queries or heap calls
at each lookup, even without access to source-key bytes. Three controls catch
wrong prefixes, a wrong terminal count and routing by logical rather than dense
indices. A full 65,024-module parent comparison requires complete SPIR-V byte
equality, exact layouts and unchanged allocation counts and bytes; current
SPIR-V and family validation remains part of the evidence. These are correctness
and work-count results, not PSO readiness, GPU execution speed or cold-play
recovery. Driver-cache qualification remains independently required.
