# Fragment source temporary ownership

The GLSL fragment builder allocated an MString named `clip` containing window
clipping and depth code, appended that text into the returned final source,
and failed to release the temporary. Every such generation retained the
MString record, GString record and capacity of its owned buffer. This applied
to GL generation and Vulkan GLSL fallback generation, not the direct SPIR-V
emitter. It could grow during first-use shader generation or source prewarming.

Release the temporary only after its last CPU reader, the append into the
already-owned final source. The returned source remains owned until its caller
releases it. No shader key, cache schema, source bytes, native compile path,
queue, guest timing or renderer-selection rule changes. This does not remove
existing draw-critical compilation or establish cold-first-play recovery.

Normalization helper names are the four immutable strings `norm0` to `norm3`.
Use those literals directly in the texture-stage loop, shadow sampling and
convolution generation instead of formatting identical owned strings. No
normalization-name payload needs a heap owner or a formatting operation.
Other source-building allocations, formatting and final assembly copies remain.

The ownership regression fixture executes the actual builder, MString inline
allocation/free operations and real GLib-compatible GString implementation.
Its recorder tracks both the returned owner and temporary owners. Across
73,728 constructed states/targets it compares GL 400, GL 410 and Vulkan 450
source byte-for-byte with pinned parent 396d8f1f0a. The historical parent is
measured as retaining exactly one clipping owner per source; only that known
historical leak is reclaimed by the test after comparison. Candidate leaks are
never reclaimed to manufacture a pass. Two separately generated source owners
are indispensable test-only comparison inputs, not runtime payload snapshots.
The historical reference blob is SHA-256 checked before use.

The parent retained 157,089,792 aggregate owner/buffer bytes over this corpus,
with a maximum of 4,136 bytes per source (allocator overhead excluded). Candidate
temporary retention is zero. Parent normalization-name formatting calls are
302,592, candidate zero. These are allocation/lifetime work counts, not measured
frame-time or GPU execution improvements. The fixture adds explicit failure
controls for missing release, release before the final reader, and reintroduced
normalization-name formatting. Linux runs use address/undefined sanitizers
and leak detection; Windows runs use the same actual owners without sanitizer
substitution. Full shader/performance and cold-play qualification retain their
existing failed/unqualified status.


The follow-up moves the unchanged version directive to the start of the
preflight/declaration owner, then appends `main`, clipping, variables and code
into that same owner. Return ownership transfers to the caller without a
reference increment or a second final MString/GString allocation. No interior
lend is retained across growth; the final source stays alive through its
caller's last consumer. This removes the full declaration-prefix assembly
copy. GString growth, the remaining independently built source sections and
driver-internal copies/allocations remain; this is not a whole-renderer
zero-copy claim. The same byte-equivalence fixture checks exactly one fewer
MString record allocation per generated source and rejects a reintroduced
prefix clone even when its output and final live-owner count are correct.
