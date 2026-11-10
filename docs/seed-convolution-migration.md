# Convolution seed revisions

The projected-offset correction (73de2baf01) changed convolution key tags from
1/2 to 5/6. The current state builder already emits the revised tags, and the
shader compilers implement those current kernels. The bundled dictionary still
contains old tags: its Vulkan sections include 15 rejected fragment entries
among 9,334 state entries, in crtaxihr, gundamos, outr2 and outr2st. These old
prewarm keys cannot prepare the corresponding current pipeline families.

At seed load, migrate only legacy 1/2 to the existing current 5/6 tags. The GL
state path does this before dynamic canonicalization and deduplication. The GL
module-only fallback migrates fragment keys before any submission. Vulkan
updates both owned input arrays before generating dynamic and exact queues, so
both streams use the same revised keys. Vertex and geometry module keys are
unchanged; unknown tags and all unrelated state bytes are preserved.

The seed readers have already materialized the final owned records from mapped
local input or decoded dictionary storage. Migration writes those records in
place; it neither writes borrowed mappings nor retains input pointers. Existing
queue publication and retirement own subsequent records as before. There is no
extra allocation, payload clone, file scan, new queue, pool or per-draw call.

This is an explicit metadata migration to an existing semantic key revision.
It does not reinterpret old compiled modules as current ones, alter live-draw
keys or cache schemas, clear user caches, change seeding defaults or rewrite the
bundled dictionary. Current and unknown tags are unchanged. New compilation uses
the established current shader generation paths.

Regression coverage includes native/ARM64 legacy/current/unknown tag mixtures,
whole-record canaries and padding, idempotence, three mutation controls, and GL
in-place compaction with real keys, deduplication and forced collisions. The
production migration helper recovers all 15 Vulkan dictionary misses. This proves
matching metadata and ownership, not ready PSOs or recovery of cold-play stutter;
physical-game timing remains unqualified when driver caches are uncontrolled.
