# Vulkan source and layout ownership

Layout-bearing shader results move into one allocation with ShaderLayouts
first and ShaderSource immediately after it. The layout reference count owns
the block. The source reference count still owns the SPIR-V word allocation;
its last release frees words, clears the dead word pointers, and releases its
layout reference. Module-held layout references keep descriptors and names
valid until native module retirement. Nothing lends a dead source header to
new consumers.

This removes one source/layout header allocation and free per result. It
adds no word, texel, key or layout payload copy. Uniform/push member blocks
retain their original owners, and mutable parameter storage stays private to
each module. Source byte accounting includes both headers once. Empty-layout
sources and separately allocated legacy layouts retain their old free paths.
The ownership flag occupies existing source-header padding on tested 64-bit
hosts; compile-time alignment checks guard the combined block.

Tradeoff: after the last source-word reference, its header storage remains
allocated until the last layout consumer. This bounded region is measured by
the ownership fixture (48 bytes on the tested 64-bit host). No words remain
retained through it. This is an allocation-count improvement, not a claim of
allocation-free gameplay or faster frame times.

The worker fixture exercises direct/compiler results, prefix views, empty
layouts, standalone owners, memo eviction, module upload independence,
teardown and concurrent refcounts with the production ownership functions.
Shader output, GPU execution, PSO readiness and cold-play qualification remain
separate checks. Driver-cache state remains uncontrolled and cold play stays
BAD/OPEN.
