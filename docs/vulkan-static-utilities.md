# Fixed Vulkan utility words

PVIDEO conversion, the three GPU-property probes and array/volume S3TC decode
use immutable core-image SPIR-V for both existing target versions. The reference
GLSL lives in utility-glsl.h for explicit generation and verification, and is
not included by runtime consumers. CI compares all twelve checked-in modules
with the existing compiler; it never regenerates its expected words.

The selector returns a pointer and word count without allocation. The core
image owns that storage; vkCreateShaderModule borrows the exact span until its
synchronous call returns. Module/pipeline handles keep their existing renderer
ownership and native teardown. No ShaderSource, reflected layout, temporary
word buffer, retained stack descriptor, file read or queue snapshot is added.
Driver-internal copies and allocations are outside this host-side claim.

The CPU fixture checks exact words, both target versions, call-site stage
selection, 120,000 exact pointer/length native handoffs, zero selector/constructor
heap calls and invalid selectors. It measures the displaced compiler heap calls
separately. SPIR-V validation and S3TC GPU/CPU value comparisons cover both image
variants and existing snapshot/direct-source, alignment, mip/cube/border, skipped
level and eight-slot resource-lifetime cases. GPU payload and retirement paths
are unchanged.

This removes fixed utility GLSL compilation, not Vulkan pipeline creation.
S3TC still creates its compute pipeline lazily; that remains a tracked draw-path
violation. Eagerly creating both variants would add allocations for unused
variants, so this change preserves existing creation counts rather than claiming
that work complete. Correct fallback and readiness need separate work. Cold play
remains BAD/OPEN and driver-cold qualification remains uncontrolled.

To regenerate intentionally, run test-spv-utility.py CHECKOUT cc --generate,
then rerun without --generate to compare and validate the new words. Normal CI
requires spirv-val. The retained fixed-shader-ownership.py entry point runs the
focused word/owner test without claiming SPIR-V validation on hosts lacking it.

The ARM64 cross-check exposed host argument-evaluation ordering in the GLSL
reference compiler's member access: pointer-type and member-index helpers both
allocate IDs. Sequence the index before the pointer explicitly, preserving the
existing x86 words. The twelve modules now match exactly on x86 and ARM64;
CI runs the ARM64 CPU/ownership check under qemu-user. This is a CPU cross-check,
not a physical ARM GPU qualification. No additional allocation or payload copy
is introduced by the sequencing fix.
