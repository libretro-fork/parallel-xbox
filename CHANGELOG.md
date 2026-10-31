# Changelog of the libretro core

What changed in the libretro core of this fork, newest first. Changes that come with a merge of upstream xemu are in upstream's own history; a merge is noted here with the upstream version it brought in.

## Unreleased

- Chihiro DIMM states stream changed pages directly into the frontend state buffer and borrow the input blob during restore. Replace the full intermediate blob with a page-number list; preserve the v1 wire format and game-image identity checks.

- Remove the unused QEMU yank registry, its startup mutex, QAPI definitions and chardev handover fields. Remove unreachable KVM private-memory setup and its discard-counter mutex; unsupported private-memory requests still fail explicitly. Xbox and Chihiro emulation features are unchanged.

- Vulkan clear shaders and the shared fullscreen display vertex shader now use immutable SPIR-V directly. Creating them no longer allocates compiler payloads, array wrappers or uniform metadata, and Vulkan borrows their static words without an intermediate copy. Pipeline creation and driver compilation remain.
- OpenGL shader-cache loading transfers file ownership with the binary view instead of copying every payload. Release mappings after GL consumption, duplicate rejection or eviction; retain at most 64 mapped files, using the existing copy path beyond that limit. Cache formats and emulation behavior are unchanged.

- Remove the two remaining unused migration mutex objects and their initialization/destruction. Saved states retain their existing frontend/BQL ownership.

- Vulkan startup checks now inspect the five required pixels directly in mapped readback memory, avoiding a 1,228,800-byte heap allocation and image copy. GPU synchronization and winding detection are unchanged.

- Remove unused QEMU I/O channel classes, socket/file transport and GLib watch adapters after saved states moved to direct memory. Remove dead block-channel abort plumbing and unused TDX guest/quote structures, including their unused mutex. Xbox and Chihiro behavior is unchanged.

- Remove 19 unused migration/postcopy synchronization objects and an unreachable recovery loop, including ten unused semaphores and two mutexes. Saved-state behavior is unchanged.

- Saved-state streams now use small caller-owned cursors directly over frontend memory. Remove the channel/GObject wrapper, 32 KiB scratch allocations, iovec staging, descriptor-receive and live-migration RAM-discard machinery. Payloads copy once to their final destination; input views borrow the state buffer. The state format is unchanged.
- Vulkan emits triangle and quad wireframe/point geometry directly as SPIR-V. Edge slopes, point depth varyings, flat colours, winding and quad-strip parity keep their existing behavior. Repeated endpoints share lazily loaded input values; the final shader word allocation transfers to the cache without copying.

- Vulkan emits filled quads and quad strips directly as SPIR-V, retaining their separate triangle depth planes and quad-strip parity rule. One final word allocation transfers into the cache without copying; repeated quad vertices reuse input values on first use, and flat-colour inputs and output-member pointers are shared across emissions.

- Vulkan emits filled-triangle geometry directly as SPIR-V for triangle lists, strips, fans and filled polygons. It preserves winding, flat colours, strip parity and depth-slope precision while bypassing GLSL strings and compiler scratch buffers. The module cache takes the one final word allocation without copying.

- Thread cleanup for frontend-owned threads now runs during core deinit, before TLS is destroyed. Windows keeps separate per-thread notifier lists instead of retaining TLS pointers in a process-exit list, preventing a shutdown crash; cached AIO objects can register cleanup again on reload.

- RCU deferred callbacks now use fully published libretro-common MPSC batches, each detached before its grace period. This replaces the legacy dummy/tail queue and separate callback count implicated by an intermittent Windows startup abort.

- Trim unused GLib compatibility services (trees, datalists, mapped files, date objects, timers and helper-process plumbing), including the unused datalist spinlock and migration page-request lock objects. Xbox and Chihiro emulation features remain available. Logging formats timestamps on the stack, and Windows UTF conversions transfer their result buffers without an intermediate copy.
- Vulkan emits narrow-line geometry shaders directly as SPIR-V, on both render-thread misses and background prewarming. The final word allocation is transferred into the module cache without copying; other stages and geometry variants retain the GLSL compiler.
- The fallback SPIR-V compiler appends word streams in bulk, inserts into its existing function buffer, consumes initializer buffers and reuses the function allocation for the final module instead of assembling another complete copy.

- RCU epoch publication now uses its existing release/acquire ordering instead of an extra process-wide flush before each period; old-reader scans retain their global memory barrier.

- Short RCU handoffs poll their queue and completion predicate briefly before registering sleepers, and idle-reader grace periods avoid unnecessary waiter bookkeeping. Polling uses the existing calibrated eventcount budget and is disabled by a zero spin override.

- Loading the core no longer leaves the host loader thread registered as an RCU reader. Only threads that actually run emulator work register, so a short-lived loading thread cannot leave a stale TLS record behind.

- RCU registry updates and grace periods now run on a dedicated owner through libretro-common MPSC requests and eventcounts, removing both RCU mutexes. Reader unregistration acknowledges TLS lifetime before a thread exits; overlapping grace periods keep their own completion boundaries.

- OpenGL disk-cache loading now publishes binaries for the render thread to adopt, removing the shader-cache mutex and preventing cache eviction on the disk thread. Save workers own immutable binary snapshots; cache shutdown joins loaders and releases entries even when caching is disabled.

- Background OpenGL shader-source generation now hands jobs and completed sources between the render thread and its worker through atomic slot publication, without a job mutex or condition variable.

- CPU callback dispatch uses a libretro-common MPSC queue and eventcount completion. The single guest CPU no longer needs SMP exclusion locks or unused halt/ioctl synchronization objects. GLib interned strings and default context use immutable atomic publication; global random draws use an atomic counter. Other emulator locks remain.


- Reduced worker wakeup overhead, especially short Windows handoffs, while preserving support for older Windows versions and existing high-resolution timeout handling.

- x87 double loads and register moves now retain their precision when single-precision arithmetic is selected. The hard-FPU cache keeps binary64 values and rounds affected arithmetic results explicitly; soft-FPU double loads widen at extended precision before restoring the control state. This corrects integer conversions that previously saw an already-truncated input. Hard-FPU remains a binary64 approximation of x87.
- Signed G8B8 mipmapped textures now honor mixed point/linear minification and magnification filters, including sampler bias. Base-clamped Vulkan G8B8 samplers retain the correct filter without reading another mip. Matching-filter shaders and shader-seed layout are preserved; no runtime diagnostic logging is added.
- Headers no longer declare 1,776 functions that no longer exist anywhere in the tree (about 4,400 lines): left behind by QEMU subsystems the core never had (the HMP monitor, virtio, KVM, block jobs) and by earlier removals. Nothing changes for someone using the core.

- Code for QEMU features the core is never built with is gone from the source (about 2,500 lines in 88 files): KVM, Xen, HVF and the other hypervisors, TCG plugins and the TCG interpreter, VNC, SPICE, curses, io_uring and Linux AIO, and the like. None of it was compiled; nothing changes for someone using the core.

- QEMU's command-line parser is gone: the core always started the machine with the same seven options, which QEMU parsed through its full table of about a hundred. The core now hands each one to what it sets directly, and the option table, its 5,850-line source and the code only other options reached (about 10,000 lines) are gone. QEMU no longer looks for a host qemu.conf at start either. Nothing else changes for someone using the core.


- Signed G8B8 mipmapped textures now decode each texel before filtering within and between mip levels in both renderers. Matching point/linear reconstruction with wrap addressing and a zero lower mip clamp is covered; Force LOD0 remains effective. No runtime diagnostic logging is added.
- Windows asynchronous network handlers now accept raw Winsock handles as well as CRT-wrapped sockets. Failed raw-handle event registration leaves an existing handler intact, and readiness masks follow the supplied read/write callbacks. Winsock event registration makes raw sockets nonblocking; no new waits or synchronization primitives are added.


- RDPMC reads of the Xbox CPU's two unmodelled performance counters now return zero instead of an illegal-instruction fault. Privilege checks and virtualization intercepts remain active, unsupported indices raise a general-protection fault, and repeated reads do not produce diagnostic logging. Performance event counting itself is not emulated.

- OpenGL and Vulkan retry overflowing barycentric depth calculations in a shared power-of-two coordinate frame. Ordinary calculations retain their previous arithmetic path; very large projected coordinates can now produce finite interpolation weights instead of NaNs without additional shader variants or GPU/CPU synchronization. GL binaries from older generator revisions are rebuilt; existing seed states remain usable.
- CPU Boost retains class-specific translations in a secondary cache consulted only after a primary miss, avoiding repeated global code-table lookups. The primary generated lookup is unchanged. This costs 1 MiB of preallocated CPU metadata, with no mid-game cache allocation or guest-buffer copy; code and page invalidation cover both caches.


- Single-level signed G8B8 textures now honor mixed point/linear LOD0 minification and magnification filters, including sampler LOD bias, in both renderers. Vulkan preserves minification selection without sampling another mip level. Matching-filter shader paths are unchanged; no runtime diagnostic logging is added.
- The Conexant TV encoder now advances its field counter with each guest vblank, including while the previous interrupt is still pending. Encoder registers, the SMBus transaction and its read cursor are preserved in savestates, so the counter can stay aligned with guest frame/flip tracking after a load.


- OpenGL and Vulkan use the Xbox forced destination-alpha value when blending on XRGB-style targets, fixing colors that depended on unused host alpha bits. Vulkan applies the correction to both static pipeline keys and dynamic blend state, including guest format changes sharing the same host attachment format.


- Guest indirect branches use the x86 state hook directly with CPU boost both enabled and disabled, and avoid the debug-flag helper call during normal execution. Single-step, one-instruction and no-chain debug modes keep their existing flag policy.


- Vulkan devices with host query reset keep render passes open across visibility-report boundaries, avoiding repeated pass setup and pipeline binds. Query scopes still close at real pass boundaries, and game visibility queries do not overlap cost-model occlusion queries. Devices requiring command-buffer query resets retain their existing path.

- Signed G8B8 textures now decode each texel before bilinear filtering and supply signed color values to the combiner in both renderers, fixing the speckled water in the tested Ninja Gaiden Black scene. This covers single-level, projective 2D textures with matching point or linear filters; mixed filters, mip chains, texture-border layouts, color keys and signed alpha-kill retain their previous paths. Force Quality Filtering preserves the sampling footprint of G8B8 data. No runtime diagnostic logging is added.
- Vulkan converts unsupported three-component byte/short and signed-scaled short vertex formats directly into mapped staging memory, including inline arrays. Device capabilities are checked once at renderer startup; guest dword padding and provoking values remain intact, and GPUs supporting the native formats keep their existing upload path.


- Vulkan vertex remapping skips leading vertices that the draw does not use, keeps packed copies contiguous, and specializes small strided copies. Remap metadata is passed by reference rather than copied; existing indices, staging capacity and in-flight snapshot protection are preserved.

- Vulkan retains a compatible larger linear depth buffer when a game switches to a smaller color target, avoiding depth downloads and reuploads. Cached target switches keep the color-sized render area and clip, and partial clears preserve the rest of the depth/stencil image.
- CPU Boost probes cached destinations supporting every carry class before calling the dispatcher when outgoing flag classes are dynamic. Specific-class targets still use the helper; independent-only probes reject incompatible cache entries before extra identity loads.

- Extend native file-I/O regression coverage to partial vectored/scalar transfers, interrupted calls, EOF, zero-progress transfers and terminal errors using real files with fault injection at the syscall boundary.

- CPU Boost retains safe direct chaining after variable-count shifts when zero and nonzero counts have the same carry class. Class-changing shifts still invalidate the proof, preserving the recent flag correctness fix.

- Texture and shader cache visits walk resident nodes instead of scanning all 65,536 hash bins, reducing empty-bin memory traffic without another cache, allocation or lock. Callback eviction remains supported.

- Vulkan no longer mistakes the previous submission's final surface write for work in the next command buffer, avoiding redundant display/readback/upload submissions. Existing fences still protect GPU work in flight; no extra frame, copy or synchronization primitive is added.

- Scalar file reads and writes now reject negative or overflowing signed-offset ranges before transferring data or padding EOF, matching vector-request range checks.

- Native and mapped file-vector operations reject malformed or overflowing requests before touching files or destinations. Empty native requests skip system calls; vectored native I/O reuses the validated total.

- Frontend VFS reads validate the full scatter/gather range before any provider I/O. Empty reads no longer seek, and invalid or overflowing requests leave the provider cursor and destination unchanged.


- Vulkan surface conversion no longer hangs on GPUs with compute workgroup limits below 1024. Workgroup selection respects both invocation and dimension limits.
- Network reception resumes after the guest returns RX buffers, including descriptors returned without a register write. Recovery polling stops when reception is disabled or ready, and is rebuilt after loading a savestate.
- The GPU's PBUS PCI configuration mirror handles both hardware aliases and byte/word accesses through the normal PCI write masks.
- Vulkan uses standard border-color samplers where their values are exact, reducing custom-sampler consumption without adding shader variants or changing integer depth-texture border values.
- Memory-backed Chihiro media reports failed address-space transactions as I/O errors and stops subsequent transfers, rather than reporting successful reads, writes or zeroing.

- Reject negative-length block discards before dispatch, including memory-backed media, instead of silently accepting an invalid range.


- Block I/O completion excludes the active callback from nested drains and cancellation, preventing duplicate callbacks and pending-counter underflow while retaining its request slot until return.


- CPU Boost invalidates stale entry flag assumptions after variable-count shifts, fixing incorrect carry/overflow reads. It compares reconstructed byte/32-bit subtraction operands at block entry instead of materializing all flags for relational conditions. SAHF also skips discarded flag calculations when overflow is already known clear. Both retain pending floating-point publication and safe block-class handling.
- Added an opt-in bump-filter observation probe covering 192 G8B8 sampling cases. Rendering behavior is unchanged.

- Completed IDE buffered reads leave the pending list before their callbacks run. A callback that cancels pending work no longer receives a second completion for the request already being completed; buffer cleanup still happens after the callback returns.

- IDE buffered-read cancellation marks pending reads orphaned before invoking their callbacks and captures callback arguments before the list can change. Re-entrant cancellation no longer repeats callbacks, and a callback that drains completions no longer leaves the cancellation loop using freed requests.

- qcow2 checks scatter/gather counts and total lengths before I/O, rejecting integer-overflowing requests instead of accepting wrapped totals as empty transfers. Valid empty requests remain supported.

- CPU Boost carries byte-subtraction borrow across translated blocks directly instead of calling the carry helper. It uses the remaining carry-class encoding without widening the block key and preserves pending floating-point state.

- qcow2-to-raw conversion reads only the valid bytes of an uncompressed final cluster and skips zero-only logical tails even when unused cluster padding is nonzero. Partial-word tails retain their last valid byte.

- Retain GLib callback data until active dispatches finish. Replacing a callback or destroying its source from inside a callback no longer runs its data destructor while the callback still uses that data, including nested dispatch.

- CPU Boost computes parity-only conditions after addition, subtraction, increment/decrement and shifts directly from the low result byte. Other flags remain available lazily, and zero-count shifts preserve their incoming flags.

- Preserve the caller's file position when the GLib descriptor-mapping wrapper reads a file. A temporary native mapping replaces the seek/read loop, while the returned storage remains a private copy independent of the caller's descriptor.

- Native POSIX disk sync retries interrupted system calls, including the final sync when a writable image closes. Genuine sync errors still propagate instead of being retried indefinitely.

- CPU Boost emits the MMX exit tag update directly instead of calling a helper; native MMX entry updates all eight tags with one wide store. Floating-point publication, TOP and register contents are preserved.

- Honor bounded UTF-8/UTF-16 input and report actual consumed lengths in the Windows GLib conversion wrappers. Empty conversions now succeed, and UTF-16 conversion no longer includes text beyond the requested prefix.

- Successful native sparse zero operations on Windows and Linux skip the preceding file-size query. Fallback writes still query and clip to EOF, and large/invalid ranges are checked without overflowing offsets.

- The test suite runs every test script: 25 test scripts were never run by run.sh, and four of them no longer compiled against the code they check (DMA mapping, vertex fetch spans, the vertex snapshot cache, the Vulkan shader-module cache). They are fixed and run now, the GL and MCPX boot ones with the Linux core and the Vulkan copy ones where there is a Vulkan device.

- 47 source files the core compiled but never linked (QEMU utilities such as the dirty-bitmap, throttling, readline and keymap code, and stubs nothing calls) and 163 headers nothing includes are gone, about 29,000 lines. Nothing changes for someone using the core.

- The generated QAPI code is pruned to what the core uses: QEMU generates a type, a visitor and a QMP event for everything in its management schemas (block jobs, migration, the QMP schema's own 32,000-line introspection table), and the core reaches about a quarter of them. About 80,000 generated lines go: 98 of the 139 generated sources and 103 of the 186 generated headers. Nothing changes for someone using the core.

- QEMU's trace points are gone from the build: they were compiled with no backend, so each was already an empty call, but their event tables (about 50,000 generated lines), the trace control code and the -trace option were still built. Nothing changes for someone using the core.

- Detach surviving GLib child sources when their parent is finalized. A retained child can now be attached and readied independently without following a dangling parent pointer.

- Failed qcow2 L2 entry writes no longer publish the new mapping in memory. A failed whole-cluster zero keeps reading the original data, and retrying still updates the on-disk entry.
- Native floating-point comparisons capture condition flags in registers on x86-64 hosts with LAHF support, avoiding the stack push/pop roundtrip while preserving RAX and normal register allocation. Older hosts retain the existing path; extended CPUID probing now correctly detects LAHF and LZCNT.

- Keep GLib sources alive while prepare/check callbacks run, and skip sources removed by a callback. This prevents use-after-free when a source removes itself or another source during a main-context readiness walk; ordinary walks reuse their snapshot storage.

- CPU Boost computes parity-only conditions after logical operations directly from the low result byte, avoiding a full flag helper while retaining other lazy flags and existing floating-point flush boundaries.

- Native scattered reads stop issuing host reads once EOF is confirmed, zero-filling remaining destinations directly while preserving gaps. Short reads before EOF still retry, and read errors still propagate.

- Release GLib main-context pending-source references when a readiness check is replaced or a context is destroyed before dispatch. This fixes leaked sources and skipped finalizers during cleanup.

- qcow2 writes clear autoclear header flags before modifying existing data as well as allocating clusters. Failed header writes remain pending for retry; empty requests leave the header untouched.

- Fix GLib hash-table replacement writing through a stale slot after a destructor clears/resizes the table, and keyed-data replacement using a freed node after recursive cleanup. Hash clearing now detaches old entries before callbacks, replacement releases discarded references even for equal pointers, and keyed-data operations serialize their list access.

- A failed qcow2 metadata sync remains pending, so later flushes retry the host sync instead of silently reporting success. Successful syncs still let clean flushes skip host I/O.

- Correct the test frontend's Vulkan close/reopen lifecycle: tear down the old device and negotiate a fresh device before resetting the core context. This removes a harness-only reload crash and stale GPU handles; it is not a fix for the reported RetroArch heap corruption.

- Fix Windows thread-local key publication races in RCU readers and QEMU helpers, GLib global string/random state races, non-atomic shared-object references, shared-array use-after-free, nested one-time initialization deadlocks, and duplicate default-context initialization. These are confirmed defects; the intermittent heap-corruption report is not yet conclusively attributed.

- Partial zeroing of exclusive qcow2 clusters uses the native sparse range-zero operation, avoiding zero-buffer writes where supported. Tiny ranges retain a single bulk write; shared clusters retain copy-on-write, and unsupported filesystems retain bulk zero-write fallback.

- Full-cluster qcow2 replacement writes bypass scratch copies and old-data reads/decompression. Partial writes still preserve unchanged bytes, and snapshot copy-on-write remains intact.

- Adjacent native disk-write spans now share bulk writes on Windows and other platforms without vectored writes, avoiding one host call per span without staging copies.
- CPU Boost processes eligible packed SSE add/subtract/multiply operations four floats or two doubles at a time, reducing per-lane loads and stores while retaining the existing rounding, flushing and software-fallback rules.

- x87 status/control-word reads use generated loads instead of C helper calls, avoiding their general register bookkeeping while preserving floating-point stack flushes and instruction-pointer publication.

- Video gains a separate Force Quality Filtering option (restart required, disabled by default), requesting up to 16x anisotropic filtering on wrapping/mirrored linear 2D color textures. Point/depth, cube and clamped/bordered sampling keep game filtering, and OpenGL isolates forced filtering from shared point-sampled bindings. Force Texture LOD0 remains independent.

- Force Texture LOD0 keeps eligible Vulkan mixed RAM/GPU cubemaps at internal resolution without GPU-face readback; RAM faces expand on the GPU and temporary images retire through the existing pool. Compatible Vulkan 2D surfaces continue to use direct sampling.

- RCU startup is initialized once and its callback queue admits only one worker. This prevents the duplicate-consumer queue abort observed during startup fast-forward; fork children can still replace their inherited worker.

- The PC machine code the Xbox is built on loses what the Xbox never has: memory hotplug (DIMMs, NVDIMMs, virtio memory), the fw_cfg firmware interface (and the -fw_cfg option) and the ACPI table builder the PC BIOS reads. The Xbox's own ACPI power management (acpi_xbox.c) stays. Nothing changes for someone using the core.

- Devices and services the Xbox machine never creates are removed: the TLS channel, credentials and sessions, the command, DNS-resolver and network-listener channels, the qtest server (and its -qtest options), and the IOAPIC (the Xbox's interrupts go through the MCPX's i8259 pair). Nothing changes for someone using the core.

- The test harness checks audio cadence with an integer phase, avoiding false sample-count failures when hard-FPU tests change host rounding. The declared timing is checked explicitly, and failed x87 test output is retained in CI logs. Core audio output is unchanged.

- CPU Boost avoids saving dead x87 operand scratch values into CPU state between instructions. Real stack registers, helper synchronization and rounding retain their existing handling.
- Savestate loads revalidate bound texture state after restoring GPU registers, fixing OpenGL retaining a later mip clamp. Mip/LOD0 regression covers CPU edits and states loaded under the opposite frontend setting.

- Voice lock masks use unsigned 64-bit shifts at voices 63, 127, 191 and 255. Regression checks now exercise the production lock helpers and verify that locked voices stop advancing, without relying on signed-wrap behavior.
- qcow2 HDD transfers combine adjacent memory spans before the bounded file-I/O vector is built, avoiding extra read/write calls and fragmented copies for contiguous buffers. Empty spans are skipped; scattered buffers and cluster boundaries keep their existing behavior.

- ParaEQ and spatial direct/room filters scale negative samples with defined multiplication, and voice register masks reaching bit 31 are unsigned. Existing filtered output and rounding are preserved; the voice regression now checks signed filter histories without signed-wrap assumptions.

- Audio filter feedback uses defined signed scaling instead of shifting negative values. Existing fixed-point rounding and clipping are unchanged; a wide-integer regression checks boundary values and filter histories without signed-wrap compiler assumptions.

- The Run-Ahead audio regression now covers the EP DSP burst-output queue, checking an unbroken ramp and byte-identical output after repeated save/load cycles. Runtime audio behavior is unchanged.

- CPU Boost lowers all legacy register SHUFPS selectors directly, extending the existing broadcast optimization. Identity shuffles avoid the helper and self-copy; other selectors read every input before writing any result. Memory operands and other encodings keep their existing paths.

- Frontend VFS disc reads combine adjacent destination spans into bulk provider requests without staging buffers. Seeks honor the API's zero-on-success result, URI-backed Xbox discs are classified correctly, and short reads during partition probing are retried. Local disc probes borrow libretro-common mapped views; gapped spans and EOF zero-fill are preserved.
- Read-only frontend VFS reads safely skip empty scatter spans when filling a request past EOF, avoiding a null-pointer library call for zero-length vectors.

- Savestates no longer go through QEMU's live-migration code: the guest's RAM is written once, every page, while the machine is stopped (migration/ram-libretro.c), in the same stream format, so savestates made before load after and the other way round. Gone with it (about 9,000 lines): postcopy, multifd, XBZRLE compression and its page cache, COLO, background snapshots, auto-converge throttling and the migration commands only two running QEMUs exchange. Saving no longer turns on dirty-memory logging for the whole RAM.

- CPU Boost checks native SSE arithmetic eligibility once per add, subtract or multiply instruction rather than once per lane. Rounding, flushing and software fallback behavior retain the existing policy.

- The host-audio precision regression avoids undefined signed shifts and now runs with undefined-behavior sanitizer traps. Audio output is unchanged.
- CPU Boost lowers legacy same-register SHUFPS lane broadcasts without the generic shuffle helper call. All bit patterns and bytes above 128 bits are preserved; other selectors, memory sources, VEX/double encodings and big-endian layouts retain their existing paths.

- Fixed the non-futex lock-counter build: its retro_atomic compare/exchange helper is shared with macOS and other non-futex platforms, rather than being hidden in the futex-only section.

- Otogi can start from an unmodified XISO or full disc image with Complex 4627. Runtime Disc-check Compatibility adjusts only the loaded executable's physical-disc check flag before entry; disable it to retain the original checks. No security dump or image preparation is needed for this workaround.
- Read-only local images use libretro-common mapped views, removing repeated read syscalls and allowing zero-copy metadata access. Scatter reads go straight into guest buffers and combine adjacent spans. Native positional I/O remains the fallback, writable HDDs stay sparse, and frontend VFS reads avoid redundant seeks and handle partial reads correctly.
- Vulkan cubemap redraws copy only changed GPU faces when updating the existing texture image. Replacement images and RAM uploads still restore every required GPU face.

- Another round of the code no build of the core calls is removed now that QEMU's user-mode, coroutine and atomics code is gone (about 6,900 lines): among it QEMU's ELF, a.out and multiboot loaders (the Xbox boots its own BIOS), most of its socket address handling, the block layer's dictionary helpers and the task objects channels ran in a thread. Nothing changes for someone using the core.

- CPU Boost reuses checked jump-cache entries for same-page branches into instructions spanning two code pages, avoiding repeated dispatcher trips without installing unsafe permanent links. Cache misses, remapping, code writes, debugger restrictions and instruction budgets retain their existing handling. Only targets in the final 14 bytes of a page gain the extra fallback code.

- Vulkan mixed cubemaps no longer re-upload unchanged RAM faces for an in-place GPU-only face update. RAM layers are still restored when an update needs a replacement image.

- RCU's readers (dirty-memory tracking, device and DMA accesses through an address space, the event loop's handler lists) no longer run a full memory barrier on entering and leaving a read section on Linux and Windows: the rarely run writer makes every thread of the process take one instead, with Linux's membarrier or Windows' FlushProcessWriteBuffers. On macOS, Android, or a Linux kernel older than 4.14, readers keep their barriers.

- QEMU's own atomics (qemu/atomic.h) are replaced by libretro-common's retro_atomic.h, as the emulated GPU and sound chip already used, and qemu/atomic.h is gone: the object and QObject reference counts, the QOM cast caches, RCU's grace-period counter, reader state and call_rcu queue, and the lists RCU readers walk (the RAM blocks, the CPUs, each bus's devices, the event loop's handlers and bottom halves), the vCPU's shared state (its exit and interrupt requests, its kick and running flags) and TCG's (the compile flags, chained jumps, jump cache and page descriptors), the translated-code hash table, spinlocks, events, lock counters, seqlocks, the 64-bit statistics counters, the dirty-memory bitmaps and the blocks that hold them, the RAM block list's cached block and version, each address space's memory map and DMA bounce-buffer count, the memory maps' reference counts, the event loop's bottom halves and wakeups, the timer lists, the log file, the CPUs' exclusive sections and work items, device realization, the shutdown request and migration's counters and states. QEMU's lock profiler (-enable-sync-profile), which the core never turned on, is gone, and locking a mutex no longer goes through a function pointer; so are the I/O thread object, which nothing created, and the 128-bit atomics only QEMU's parallel mode used. Guest code reads and translated-code patching stay single plain-width accesses. Nothing changes for someone using the core.

- OpenGL permits clears after rebinding an exactly matching swizzled color view, avoiding the old assertion while preserving channel masks and rectangles. Layout changes, larger cached views and depth targets retain their guards.

- Vulkan permits partial-channel and partial-rectangle clears after rebinding an exactly matching swizzled color view. Format, size, layout, anti-aliasing, scaling mode and clip bounds must still match; depth views and layout migration remain guarded.

- CPU Boost can chain STI to its one shadowed instruction using the existing checked cache lookup. Pending-stop and instruction-budget checks remain at the next block, and interrupts are checked when the shadow ends. Segment-changing MOV/POP SS, debug and resume paths retain their exits. The guarded kernel-idle proof also accepts the loop's second NOP, retaining opcode and empty-work checks. Already-posted CPU interrupts/stops go through the normal interrupt handler before the idle proof; masked interrupts do not disable idle handling.

- OpenGL reuses a still-current speculative surface readback when eviction requests the same data, avoiding a duplicate GPU download and fence replacement. Redraws still invalidate and replace stale snapshots.

- The coroutine plumbing the core never ran is removed (scheduling coroutines on the event loop, channels yielding in one, the waits for one in incoming migration), as are 47 pregenerated QEMU sources no build compiled: the QMP command handlers and the block layer's generated wrappers. Nothing changes for someone using the core.

- QEMU's user-mode emulation code (linux-user's side of TCG and the target) and TCG's debug build checks, which no build of the core turned on, are removed; so are the lockless-lookup atomics of the interval tree, which only TCG's optimizer still uses, on one thread. Nothing changes for someone using the core.

- Vulkan no longer emits normal-draw dynamic state while a clear pipeline is bound. Partial-channel clears preserve untouched channels, and subsequent draws restore their own state.

- Vulkan permits full-surface, all-channel clears when rebinding an exactly matching live swizzled color view, avoiding the previous assertion. Partial clears, depth views and layout/size changes retain their existing guards.

- Vulkan combines live GPU cubemap faces with RAM-backed faces at native resolution for single-level ARGB cubes, avoiding GPU face readback at texture binds. Scaled, unsupported and ambiguously aliased cubes retain the existing fallback.

- Added a two-renderer regression for GPU-copied cubemaps becoming RAM-backed: independent CPU face edits, a filter change and face switching must preserve both changed and untouched faces.

- Strengthened the synthetic texture-lifetime tests so ongoing draws reach the captured surface. Added a depth-cache transition test that verifies CPU-written zeros and subsequent GPU depth writes across a filter change.

- Vulkan keeps the six GPU-produced cubemap sources available while their copied texture is sampled, avoiding six face readbacks and a cubemap upload on a later filter change. Sources still expire after sampling stops.

- Kernel idle checks inspect freshly translated RAM directly instead of copying overlapping instruction windows and list words through debug buffers. Page-boundary and non-RAM mappings retain the existing reads; idle instructions, work checks and interrupt handling are unchanged.
- Deterministic mode now finishes submitted GPU work and display composition before handing the frame to the frontend, just like normal mode. This prevents repeated or incomplete pictures when CPU execution outruns the render thread, without advancing guest time during the wait.

- Vulkan keeps actively sampled GPU depth-copy sources available across filter changes, avoiding an age-triggered readback and texture upload. D16 and D24 conversion arithmetic and idle expiry remain unchanged.

- Vulkan keeps actively sampled ARGB-to-RGB565 source surfaces available across sampler changes, avoiding a source readback and texture re-upload after age-based eviction. Idle surfaces still expire normally.

- OpenGL refreshes render-target surface age when its GPU texture copy is sampled, including unchanged-state reuse and cubemap sources. Actively sampled sources no longer expire into a readback and native texture re-upload after 60 frames.

- Vulkan keeps compatible surface images alive while bound texture views are still sampled, including unchanged-state draws. This avoids expiry-triggered readback and private texture upload during continued GPU sampling. Surfaces still expire after sampling stops.

- Vulkan surface expiry now uses the existing lazy/eager eviction policy instead of forcing every dirty surface back to RAM after five frames. GPU-only content remains owed to a later reader, avoiding a premature CPU readback while preserving CPU reads and savestates.

- The synthetic GPU suite now checks a GPU-written palette change while the indexed texture and sampler stay bound. This extends the unchanged-state surface-write regression from texture bytes to independently addressed palettes.

- Vulkan surface writes now invalidate overlapping bound texture and palette views, including when the game draws again without resending texture state. This fixes stale samples from GPU-owned data that has not been written back to RAM. Only the four bound units are checked at write time.

- About 170,000 lines of QEMU no build of the core compiles are gone: QEMU's whole block layer (block.c, block/io.c, its qcow2 driver, the mirror/backup/stream jobs, blockdev), NBD, the LUKS and qcow crypto, the coroutine library, and the 745 headers nothing includes any more (the ARM, s390x, virtio and other machines' devices among them). The core reads and writes its disks through its own libretro disk layer, as before; the macOS build no longer links IOKit, which only QEMU's host CD-ROM code used.

- libretro-common is in step with RetroArch's again (retro_atomic, the SPSC queue, rtime and CRC32 had fallen behind): rtime_localtime() now uses the C library's thread-safe localtime_r()/localtime_s() instead of a spin flag, and the SPSC queue gains retro_spsc_grow_into().

- The CPU emulation no longer carries QEMU's parallel mode, which this core never used: it runs its one vCPU serially, so guest loads and stores to RAM are now plain host loads and stores instead of going through atomicity checks that always came out the same, and the out-of-line atomic helpers and the stop-the-world fallback are gone (about 2,700 lines).

- Indexed draws track vertex bounds while accepting index commands instead of scanning the finished list in both renderers. Non-incrementing index packets are decoded in one dispatch with per-word tracing preserved. Bounds are rebuilt from legacy savestates; the state format, index order and GPU uploads are unchanged.

- Vulkan can grow a clean vertex snapshot by appending only its new tail, keeping the prefix immutable for earlier GPU readers. The active arena tracks its last allocation without searching, and needed prefix pages are rechecked after downloads/settles. Dirty/invalidated, non-tail, out-of-space and unsafe non-coherent atom boundaries retain the existing copy path; arena capacity and VRAM allocation are unchanged.

- Vulkan now checks bound texture and palette dirty pages before taking its unchanged-state bind fast path. CPU-written texture pixels can refresh on the next draw even when the game does not resend texture state; clean pages avoid content hashing.

- Vulkan now revalidates a cached texture immediately when a newly detected dirty page contains a CPU write after an earlier draw in the same frame. This fixes stale pixels caused by the frame validation throttle; clean textures keep their fast path.

- Fix Ninja Gaiden Black running in slow motion near the door despite low core latency. The short STI/CLI idle shortcut now requires a recognised kernel idle loop and empty work queues, retaining its fast path without skipping active game work. Instruction-threshold and device-poll waits remain.

- The synthetic GPU suite now checks one-pixel YUY2 and UYVY textures, including cache updates when their shared chroma byte changes beyond the first pixel's nominal two bytes. This protects complete-pair conversion and its full hash span.

- Both renderers now ignore unread padding after the final row when hashing unconverted linear textures. Padding-only writes no longer recreate or upload an unchanged texture; dirty tracking and guest allocation spans stay complete, and converted formats retain their full hashes.

- Vulkan linear texture staging no longer copies padding after the final image row. Earlier rows retain guest pitch and each staged level still uses one copy; direct VRAM upload retains its full guest span.

- About 300 more functions in QEMU's generic code that nothing in the core calls are gone (the TCG op builders, the I/O channel and socket layers, the TLS sessions, ELF and ROM loading, the memory-region IOMMU hooks and more), about 9,000 lines with their header declarations, now that the non-Xbox code that named them is gone. Nothing changes for someone using the core.

- When compressed volume data is already in slice order but cannot use direct VRAM upload, Vulkan now stages it with one contiguous copy rather than a copy for each compression block. Layouts needing rearrangement keep the existing block conversion.

- Vulkan can upload compressed volume levels directly from VRAM when they have one depth slice or one compression block per slice. Eligible levels avoid the CPU staging copy; other block layouts and unavailable/misaligned VRAM retain the existing rearrangement path.

- OpenGL fallback upload tests now cover BC1/BC2/BC3 partial blocks, one-pixel rows, and mip tails at all unpack alignments, checking exact RGBA8 pixels and restoration of the caller's unpack alignment.

- The OpenGL compressed-volume regression oracle now verifies interpolated RGB, transparent BC1 colors, and BC2/BC3 alpha through actual RGBA8 fallback uploads, including odd dimensions and one-texel mip tails.

- The test script (libretro/tests/run.sh) removes the temporary folder it makes, about 500 MB, when it ends, whether the tests passed or failed; each run used to leave one behind. A folder given as its second argument is still kept.

- The code QEMU and xemu kept for builds other than the Xbox one is gone: every `#ifndef XBOX` branch, which this core never compiles, about 5,900 lines, most of them the PC CPU models (Skylake, EPYC and the rest) the core never offers. Nothing changes for someone using the core.

- Audio regression tests now compare captured GP and EP DSP output against an exact decimal host-volume oracle. EP coverage includes every S16 input at mute and 20/40/60/80 percent volume on both DSP engines, guarding against double attenuation and biased rounding.

- About 1,100 functions in QEMU's generic code that nothing in the core ever calls are gone (TCG, softfloat, ACPI, PCI, the character devices, QAPI/QObject helpers, the console and input layers, networking and more), about 24,000 lines with their header declarations. The core behaves the same; the Windows build is smaller, since its linker keeps every function it is given.

- The synthetic GPU fixture now publishes the wrapped pushbuffer write pointer before waiting for the read pointer, preventing old-command replay and stalls at ring wrap. New tests retain both draws across same-frame texture and palette updates and verify their distinct colors.

- The synthetic GPU suite now checks two same-frame updates to GPU-produced texture bytes and indexed texture indices, extending the palette-only coverage of immediate cache invalidation across both renderers and all tested scales.

- Vulkan now revalidates cached texture and palette views immediately when GPU surface writeback changes their RAM, even if they were already sampled in the same frame. This fixes stale indexed colors after multiple GPU palette updates before a flip while retaining the RAM-only validation throttle.

- OpenGL and Vulkan track the bytes actually fetched by each vertex attribute: the final element contributes its width, not a full trailing stride. This avoids unnecessary upload/dirty pages and includes the full last element when attributes overlap. OpenGL skips uniform-only empty syncs without activating its quick cache and accepts a vertex fetch ending at VRAM end. Vulkan also reuses the draw-array bounds already maintained by command batching.

- The synthetic GPU suite now repeatedly rebinds an unchanged GPU-produced palette for 40 draws and checks that its final blue color remains stable on both renderers at 1x, 2x and 4x.

- Indexed textures now see palette colors rendered by the GPU before CPU palette expansion and cache validation. Both renderers synchronize overlapping palette surfaces; OpenGL also handles parked surfaces. This fixes stale or one-update-late colors when a render target supplies a palette.

- Vulkan vertex uploads coalesce connected attribute ranges without sorting, merge other ranges in place, and clear only the COW dispatch entries used by the draw. Uniform-only zero-stride attributes no longer queue empty vertex-buffer syncs. Packed unaligned vertex data uses one bulk copy instead of one copy per vertex.

- The synthetic GPU regression suite now renders textures and indexed palettes ending exactly at the 64 MiB VRAM boundary, and checks that cached texture views sharing the last dirty page refresh together on both renderers at 1x, 2x and 4x.

- CPU translation-block hashes reuse executable RAM pointers already resolved by decoding or invalidated-block lookup. Page-crossing hashes consume RAM as separate XXH3 spans, avoiding byte-by-byte reads and a page-sized gathering buffer. Device/sub-page spans retain ordered reads through 64-byte scratch storage instead of a size-dependent gathering array, and following-page resolution stays in fetch order.

- Both renderers now accept texture dirty-page queries ending exactly at the end of VRAM. Empty spans no longer clear or invalidate a page; texture and palette invalidation retains page-wide coverage so cached views sharing a dirty page stay coherent.

- Added an offline image-preparation utility with an explicit runtime disc-check adjustment for Otogi. It creates a separate image and preserves the input. Startup notes record DOA2 Ultimate's roughly 100-second initial black screen with both MCPX boot paths.

- QEMU's migration code is cut down to what the savestates use (a state is one stream into the frontend's buffer): the migration transports (socket, exec, fd, file, TLS), multifd compression, RDMA, COLO, dirty-rate measurement and the QMP migration commands are gone, about 15,000 lines, and the -incoming and -loadvm options with them. The migration capabilities, which nothing in the core ever turned on, are constants now, so the code behind them is not built either. States saved before load as they did.

- CPU block generation reuses its executable-page resolution for invalidated-block lookup, avoiding a second probe of the same address. Ordinary cache lookups still resolve their address, and MMIO/non-cacheable mappings retain their bypass behavior.

- OpenGL now writes back GPU surfaces before uploading a one-byte texture view that overlaps them, fixing stale texture pixels from an inclusive-end off-by-one. Both renderers share an overflow-safe half-open VRAM overlap test; empty ranges no longer trigger surface writeback or reject GPU texture reuse.

- Vulkan bordered cubemap base uploads now skip the same four-texel source border as OpenGL. Higher mip copies fit their destination levels and compressed rows retain block alignment. Copy skips remain separate from allocation/flush spans. Higher mip source origins retain the existing Vulkan policy pending an Xbox layout reference.

- Vulkan shader preparation reuses identical immutable SPIR-V modules across queued states, avoiding repeated generation/compilation and array-wrapper reconstruction. Temporary reflection borrows the input words; both parser metadata and the module’s input reference are released after extracting persistent layouts. A 64-entry worker memo retains at most 4 MiB of CPU payload and is cleared after quiescence; no GPU cache or VRAM reservation is added. GArray/GByteArray references use retro atomics, and ownership-transfer arrays no longer allocate a discarded dummy buffer. Ready-table removal moves slot indices instead of shader-key records, preserving FIFO eviction; module teardown releases its packed uniform storage.

- Retail Xbox games queue their learned shader seeds from the first flip instead of waiting 15 seconds to name the disc. This gives the existing workers boot time to prepare known shaders and avoids delaying seed loading into play. Chihiro retains its detected-title priority and fallback delay.

- Texture face offsets and dirty-tracking ranges now share the renderers' mip-storage rules: tiny DXT cubemaps retain complete blocks, padded borders are included, and volume mip depths shrink per level. This fixes zero face strides and stale bordered texture ranges while avoiding excess volume hashing/readback ranges. Sampling LOD clamps leave storage offsets unchanged.

- Vulkan stores converted R6G5B5 textures as RGBA8 SNORM with opaque alpha, preserving the existing RGB bytes while avoiding unsupported three-channel optimal images and misaligned three-byte copy offsets. This uses one additional host byte per texel; guest storage and conversion arithmetic stay unchanged.
- The Windows shader-dictionary test builds its helper with the Windows compiler and runs it under Wine when cross-testing. Helper failures now propagate instead of being hidden by the summary pipeline.

- macOS links the system resolver library required by the bundled libslirp DNS backend, fixing the final core link.

- CPU translation-block hashes read executable RAM directly when the code fits in one guest page, avoiding a temporary byte copy and one guest-memory helper call per byte. Hash values and code-invalidation checks are unchanged. Cross-page, device-backed and sub-page mappings retain their existing fetch behavior.

- macOS architecture and deployment-target flags now reach every compiled source group, including libretro-common and libslirp, fixing Intel core builds on Apple Silicon runners.

- Linux and Android no longer compile the unused vhost-user server utility, whose dependency was removed with the meson subprojects. This fixes clean builds without restoring that dependency.

- OpenGL texture uploads now use byte-aligned rows and restore the prior unpack alignment, fixing corrupted narrow mip levels, small volume slices and tightly packed RGB conversion output. Linear guest pitches remain explicit row lengths. Bordered palette/RGB cubemaps also crop using the expanded host pixel stride.

- Every platform uses the bundled Vulkan headers, fixing macOS builds that could not find Homebrew's headers through the MacPorts dependency paths.

- Apple Silicon JIT write protection uses libretro-common's `memjit_write_begin/end` helpers. A per-thread adapter preserves QEMU's absolute write/execute switches, including repeated requests across the CPU and DSP JITs. Intel macOS keeps no-op switches.

- OpenGL DXT volume textures preserve the Xbox block order when native VTC compression is available, fixing scrambled slices/colors from the old slice reorder and avoiding that CPU copy. Drivers with 2D S3TC but no VTC use the RGBA8 volume fallback.

- The meson build is gone (meson.build files, configure, the meson subprojects, Kconfig and the device configs): every platform is built by the Makefile, and the buildbot's Linux jobs now are too, so the buildbot's Linux core is built the same way as the GitHub one. The macOS meson job is dropped; the Makefile's macOS job takes its place.

- With APU DSP Emulation disabled, audio now preserves normalized PCM values at unity volume and rounds once to the nearest output sample after host volume. This removes avoidable low-level truncation and the 32767-scale amplitude loss in the host float output path. DSP arithmetic and its integer output path are unchanged.

- An absent MCPX boot ROM uses an experimental, independently implemented MCPX 1.0 HLE boot path. The flash BIOS is still required. Supplied ROMs retain the existing boot path; Chihiro retains its BIOS's built-in boot code. MCPX 1.1/FBL boot is not implemented by the fallback.

- Android is built with the plain Makefile (`make platform=android NDK=... ANDROID_ABI=arm64-v8a` or `x86_64`), with nothing but the NDK: libepoxy (EGL) and libslirp come from `deps/`, the core's own GLib from `libretro/glib`. The Android core now has NAT networking too, which the meson build left out.

- Vulkan shader switches reuse the current immutable descriptor set when its textures, uniform buffer and both uniform ranges still match. Dynamic offsets continue to select each draw's snapshots. This avoids redundant descriptor writes and pool consumption without another cache or GPU allocation.

- OpenGL allocates native-size readback buffers for depth and swizzled color surfaces already converted to native pixels on the GPU, avoiding scale-squared excess allocation. Scaled color buffers remain available for GPU-only surface restoration.

- GLib is no longer linked into the core on any platform. QEMU and libslirp are built on the core's own `libretro/glib`, only the part of GLib's API they use, on libretro-common where it has the piece (file streams and its VFS, paths, directories, UTF-8 and UTF-16, base64, SHA-1, the clock, atomics). Its main context is a list of sources driven by QEMU's own loop, without GLib's wakeup pipe, locks, or an allocation on each dispatch. The Linux core is one file now: libslirp is built from `deps/` and linked in, so `libslirp.so.0` no longer goes next to it, and building needs neither GLib nor libslirp installed. The Windows build no longer compiles GLib, PCRE2 and proxy-libintl from `deps/`.

- OpenGL avoids uploading pending RAM pixels when a full-surface, all-channel color clear immediately replaces them. Partial or masked clears, clip offsets, multisampled surfaces and forced uploads keep the existing path.

- OpenGL now keeps live, tightly packed ARGB-to-RGB565 texture reinterpretation on the GPU, including at higher internal resolutions. Pixel buffers preserve the original bytes without CPU readback or texture upload; unsupported layouts and competing dirty surfaces retain the existing fallback.
- Vulkan reuses unchanged shaders' immutable uniform snapshots within the current staging generation. Switching back to a shader can select its existing offset, avoiding another CPU upload copy and duplicate GPU-buffer transfer bytes. Changed values, state loads and staging resets/rotation invalidate reuse; existing fences still protect in-flight draws. No buffers or VRAM allocations are added. Unchanged draw bindings retain their fast path.

- The core is about 2 MB smaller (15.7 to 13.7 MB on Linux): it is built with each function in a section of its own, and the link drops the 4,400 or so QEMU functions nothing in the core calls. Every build does it: the Makefile on Linux, Windows and macOS, and meson.

- Vulkan can reinterpret a live, tightly packed ARGB surface as a twice-wide RGB565 texture entirely on the GPU. A device-buffer transfer preserves the original bytes; at higher scales a nearest native-size GPU copy comes first. Other layouts, competing dirty views and unavailable transfer storage retain the RAM fallback. Texture snapshots and redraw invalidation are preserved.

- The GPU test ROM now checks compatible and incompatible render-to-texture views, including reinterpretation of ARGB bytes as RGB565 and refreshing an aliased texture after a GPU redraw.

- The Vulkan test harness now captures outputs larger than 1920x1080 instead of silently skipping their pixel/hash checks. Readback buffers grow on demand; timing-only runs allocate none, and capture failures are explicit.

- Vulkan compares uniforms directly against each shader's packed block, removing the fixed 2.65 MiB duplicate CPU value cache and the extra copy of changed fields into it. Only generation metadata is kept per shader; state loads invalidate it by epoch. Contiguous arrays copy in bulk, padded arrays update only changed elements, and small padded vectors use inline bitwise comparisons/copies. The staging copy that protects in-flight GPU draws is retained.

- Vulkan no longer uploads old RAM contents before a single-sample clear that replaces every color pixel and all RGBA channels. This also avoids the command-buffer drain those uploads could require. Partial clears, offset clip regions, multisample surfaces and depth/stencil keep their existing upload behavior.

- Dirty-memory reprotection skips primary TLB groups whose conservative host-address tags cannot overlap the changed range, instead of scanning every entry in every populated mode. The index uses 512 bytes per MMU mode, with no allocation; fills, first writes, victim restores and flushes maintain it under the existing TLB lock. Tag collisions still run the ordinary checks. Victim translation entries and their slow-path metadata now swap together under that lock.

- Windows builds retain the file-truncation helper needed by migration file channels after removal of the old block backend.

- The hard disk and the DVD drive no longer go through QEMU's block layer (its request layers, coroutines and I/O thread pool, about 65,000 lines): the core reads and writes them itself, each request one system call straight between the image and the guest's memory, on the emulator thread, with no thread switch and no allocation. A disk request now takes the same, fixed guest time every run (the moment it completes no longer depends on the host), which keeps run-ahead and deterministic emulation exact through disk access. Savestates (and each run-ahead frame) no longer sync the hard disk image to the disk when the game has written to it; the image is synced when the content closes, and a write is in the system's cache, safe from a crash of RetroArch, as soon as it is made.

- The hard disk image may be a qcow2 (`xbox_hdd.qcow2`) or a raw image (`xbox_hdd.img`). A qcow2 is read and written as it is, so standalone xemu and the core can keep sharing one, its internal snapshots kept. When there is no image, the core writes its bundled one as a raw `xbox_hdd.img` (8 GB, of which only the data, about 2.5 MB, takes space), or as `xbox_hdd.qcow2` on FAT32 and exFAT, which have no sparse files. A qcow2's tables are written as each write needs them, so a crash or a kill of RetroArch leaves at most unused space in it (which `qemu-img check` reports as leaked), not lost writes.

- Trim HDD on Close empties X, Y and Z and gives their space back to the disk in place: it no longer rewrites the image, needs no free space, and runs whether or not the image holds a snapshot.

- A full host disk is now a write error to the game, as on an Xbox, instead of a machine stopped for good.

- The Chihiro's snapshot store (`chihiro_snapshots.qcow2`) is no longer made: it held QEMU's own snapshots, which the core never took (its savestates are its own).

- QEMU's coroutines and thread pools are no longer built into the core, and a state load no longer makes and destroys a thread pool each time (each frame with run-ahead).

- More of QEMU that the core never used is gone, from the code and not only from the build: record/replay, the GDB debugger stub, the last of the monitor (the "info" printers, device and network commands, the -monitor, -qmp, -mon, -gdb, -add-fd and --preconfig options), the TAP, stream and datagram network backends (networking is NAT, and the Chihiro cabinet link uses the socket backend, which stays), the accelerator statistics and the ring-buffer character device. Record/replay sat on the emulator's paths even when off: the main loop, the timers, the CPU's interrupt and exception handling and every input event went through it, and they no longer do. The Android and macOS builds drop the same pieces.

- Vulkan draws that reuse vertex data first check four recently used immutable snapshots before searching the full 512-entry cache. Repeated draws avoid those searches without another RAM copy or more VRAM; invalidated snapshots and recycled arena generations still miss. This reduces CPU bookkeeping for repeated vertex draws; the guest instruction count and the work sent to the GPU stay the same.

- Software DXT decoding now keeps the original endpoint precision through color interpolation and rounds once to RGBA8; DXT5 alpha interpolation also rounds to nearest. This removes the fallback decoder's downward bias, verified against Microsoft's DirectXTex decoder. Native compressed texture uploads are unchanged.

- OpenGL's software DXT fallback explicitly requests RGBA8 storage for decoded 2D textures and cubemap faces, matching the existing 3D fallback and preserving all eight decoded bits per channel.

- Audio Volume now rounds attenuated DSP output symmetrically instead of rounding negative samples downward, removing a small DC bias added by the host volume control. At 100% volume the samples are unchanged; the Xbox DSP's own precision stays the same.

- Video > Force Texture LOD0 (Restart) bypasses mipmapping in OpenGL and Vulkan, sampling the largest texture level even when a game requests a smaller one. Disabled by default; enabling it can sharpen distant textures at the cost of shimmering.

- OpenGL offset scanout uploads only visible dirty background regions, directly from guest RAM, and merges adjacent runs into one strided upload. Pixels hidden by GPU tiles remain pending until exposed. Background scaling skips regions immediately covered by those tiles when the bounded rectangle plan saves enough work. Video overlays reuse their texture storage until their dimensions change. Vulkan display fences already observed complete during retirement polling no longer incur a redundant CPU wait call when their command buffers are reused. This does not remove waits for unfinished GPU work or the OpenGL composition images.

- Vulkan offset scanout now reinterprets supported swizzled and mismatched color layouts directly from existing GPU images, including packed 8-, 16- and 32-bit color bytes and differing row strides with padding. Normal and byte-conversion display pipelines are compiled separately at startup, keeping conversion work out of ordinary frames. It avoids their scanout readback and subsequent RAM-background upload. Deferred older overlapping writebacks are settled before younger ones, so later surface recycling cannot overwrite newer pixels in RAM. All bounded layer counts draw into the final display image; the old scaled composition fallback and its tile copies are gone. Display bindings are reserved at startup. Common native background modes are prepared at startup in an eight-entry, 32 MiB bounded cache (about 10 MiB initially, twice that on AMD's scratch path), independent of render scale. Coverage changes refresh only the old GPU footprint, preserving unrelated cached CPU background tiles. Dirty background batches share one host dependency, one source-retirement dependency and one staging flush. Visible CPU-written bytes still use the existing native-image upload path; live guest RAM is not sampled asynchronously.

- QEMU's monitor (its HMP and QMP command interfaces, about 100 pieces: the command tables, the monitor itself and the commands' handlers) is no longer built into the core: nothing in the core ever opened one, and the frontend is its only interface. The core is about 0.3 MB smaller. QEMU's events now go nowhere instead of through the monitor's queue.

- Android and macOS builds: the same unused QEMU pieces are no longer built there either (their sources are gone from the tree), and the build no longer fails on the savestate code since the switch to libretro-common's atomics.

- Less work between the game's instructions: the emulator's main loop no longer goes through GLib's main loop and a poll() system call on every pass of the frame loop (about 30 times a frame). Deferred device work and timers now run directly, and when the emulator has to wait for the GPU or a disk read, it sleeps until that work signals it is done. In the test ROMs this cut the core's system calls by 38 to 62%. The core also no longer sets up signal handling of its own (SIGBUS, SIGALRM, SIGIO): those signals belong to the frontend. With NAT networking on, the network sockets are still polled as before.

- About 150 pieces of QEMU that nothing in the core ever reaches are no longer built into it (unused USB, SCSI, ACPI, chardev, block filter and network filter devices, TLS and auth backends, and stubs nothing calls): the core is about 0.8 MB smaller and registers fewer device types at load.

- Two of QEMU's ways of running the emulated CPU that the core never uses (one thread per CPU, and round-robin) are no longer built into it.

- Vulkan offset scanout shades only the background rectangles left visible by its GPU regions, instead of shading the whole background and painting over it again. The exact rectangle plan is cached until coverage changes; adjacent pieces coalesce. It uses at most eight background draws sharing one descriptor set, with the previous full-background draw retained for complex layouts or too little covered area to justify extra draw calls. Video overlay coordinates and color keying are unchanged. Timing-only Vulkan harness runs now submit a completion marker without an unintended frame readback.

- macOS: the audio DSPs' JIT works on Apple Silicon. macOS there only hands out memory that can hold code being written and run when asked for it as JIT memory; the core did not ask, its request failed, and the DSPs ran on the slower interpreter instead. It now asks for JIT memory and switches the thread between writing code and running it, as QEMU's CPU JIT does. Not yet seen running on a Mac.

- The macOS core can be built with the plain Makefile (`make` on a Mac, `ARCH=x86_64` for Intel), as the Linux and Windows cores are, with the same MacPorts libraries upstream xemu uses. The CI builds it alongside the meson build for now.

- The stock xemu GPU the fork carried as a fallback is gone (about 28,000 lines and 0.6 MB of the core). The core never ran it: GPU boost is always on in the core, and the stock GPU has none of the core's handover to RetroArch.

- The emulated GPU and sound chip, and the core's glue to RetroArch, now use libretro-common's atomics (retro_atomic) for everything their threads share, instead of QEMU's. Along the way, several places that forced the PC's CPU to wait for all its outstanding memory writes no longer do where it isn't needed, mostly on x86: whenever a game touches memory the GPU draws into, and whenever the GPU marks a picture it drew as not yet written back. Two ordering slips that could hand the GPU's thread a stale download request or lose a register write from the game on a CPU with weaker memory ordering than x86 (ARM) are fixed too. Savestates are unchanged.

- Run-Ahead no longer breaks up the sound. Loading a state (which Run-Ahead does every frame) threw away the sound queued for RetroArch and started it again from a short silence, restarted every voice's resampler and filters from scratch, and made the frame after the load run two frames' worth of the game's time. States now carry the queued sound, the audio engine's timing, the voices' resampler and filter state and 3D-sound (HRTF) table, and the frame loop's timing, so a load carries on exactly where the save left off: with Run-Ahead the sound is now the same, sample for sample, as without it. This also makes loading any state seamless, and a state loaded after restarting RetroArch keeps its 3D sound. States saved by earlier versions still load, as before.

- Vulkan offset scanout uploads only dirty native background tiles that are visible, rather than the whole framebuffer after any CPU write. Fully GPU-covered tiles remain deferred until exposed. Dirty page ranges are collected without allocating a bitmap snapshot, uploads share one recording, and imported RAM stays the direct source. Staging devices pack straight into the existing upload ring; the scratch-image workaround also copies only the selected regions.

- Vulkan offset scanout samples up to 32 compatible GPU surfaces directly into the final frontend display image. The common path no longer allocates a scaled intermediate composition image or copies its tiles; only a native-size RAM background is kept. Larger layer sets retain the previous compositor. Video overlays reuse their image/sampler between size changes and queue uploads without a per-frame CPU drain. Retired renderer resources also wait for outstanding display reads before recycling.

- Games whose sound goes through the Xbox's encoder DSP (the EP) now have it written straight into the queue RetroArch takes its sound from, eight engine frames (256 samples) at a time, with the volume applied there. Before, it went into a buffer first and was copied out and cleared 32 samples at a time. The sound is the same, sample for sample, except after a reset that came in the middle of the encoder's eight frames: the queue used to repeat up to a few hundred samples there, and now it does not.

- Savestates now include the Xbox's AC97 sound controller. Before, loading a state left the controller wherever the running session had it, so its playback position no longer matched the game's (in the test, half a second ahead and stopped at the end of its buffers). States made by earlier versions still load, with the controller left as it is, as before.

- Vulkan color upload/readback scratch images now use the native Xbox dimensions instead of reserving the scaled render-target size. A 640x480 RGBA scratch image at 8x needs about 1.17 MiB rather than 75 MiB. Packed depth/stencil conversion keeps its required scaled storage.

- Offset framebuffer scanout no longer uploads the whole background or recomposes an unchanged picture every frame. GPU-only tile changes preserve its existing background. OpenGL allocates mailbox images only while earlier ones are held, rather than three at once. Recently used display modes retain their images within a bounded cache, avoiding repeated allocations and Vulkan drains on a warm mode switch. Incompatible byte layouts read back only their dependent views; independent compatible surfaces stay on GPU.

- RetroArch's thread no longer waits each frame for the emulated audio DSPs to finish their last 32 samples before taking the frame's sound. The core's sound queue now starts with one more 32-sample frame of silence (0.67 ms) to cover it, so every frame hands over the same samples as before, however the threads were scheduled, and the DSPs' last frame runs alongside RetroArch instead of in front of it. A counter of dropped audio frames that nothing read is gone too.

- QEMU's audio layer is gone from the core: about 7,000 lines, with what hung off it. Game audio never went through it: it goes from the Xbox's audio processor straight to RetroArch, as before. What did go through it was thrown away at the end, after real work: the AC97 controller read every sample queued on it out of the Xbox's memory (192 KB a second while it plays), and QEMU's mixer resampled, converted and mixed them every 10 ms of the game's time. The AC97 controller now keeps only what a game can see of it, how fast it takes samples: its position, its buffer-complete status and its interrupts, at the codec's rate in the Xbox's time, exactly as before (checked by a new test). It reads no sample data at all. Also removed: QEMU's generic Intel AC97 card, its USB speaker and audio record/replay code, none of which any Xbox or Chihiro software can drive, the PC speaker's tone generator (its port, which guests time delays with, stays), and the `-audio`/`-audiodev` options that chose host sound drivers the core never had.

- The core's once-a-second check of the running game's title (for the Auto aspect ratio) no longer copies the game's headers into a newly allocated buffer, nor searches the emulated machine's device tree for the EEPROM.

- DVD and hard disk reads no longer allocate memory while a game runs (about seven allocations per read before): the request records, scatter-gather lists and coroutine bookkeeping of a finished read are kept and reused by the next.

- Translating the game's code no longer allocates memory for each new block (about 2,500 times in ten seconds of Ninja Gaiden): the index of translated blocks is a sorted array kept from one code-cache flush to the next, instead of a tree with a node allocated per block. With this, Ninja Gaiden's gameplay no longer allocates in the core at all, except for a few DVD reads.
- OpenGL and Vulkan: frames drawn into surfaces offset inside the displayed framebuffer now appear with their CPU-written borders, instead of a black or stale picture (adapted from xemu PR #2190). Compatible linear color surfaces are composed on the GPU without reading them back to RAM. Other layouts use ordered guest-byte readback, batched on Vulkan where supported, so an older parked surface cannot overwrite newer pixels. Composition images are reused; the ordinary direct scanout path does not allocate them.

- Graphics: a game that clears its depth buffer (or colour) while it has depth writes (or colour writes) turned off now finds the cleared values in memory when it reads them back, uses them as a texture, or saves a state. The Xbox clears regardless of those write settings, as measured on hardware by nxdk_pgraph_tests, and so did the core's GPU, but the core took the picture for unchanged and kept handing out the old contents. Both renderers.

- Game code that writes over code already translated (self-modifying code) no longer allocates memory on each such write; the pages it locks are kept in a small array instead.

- Translating the game's code no longer allocates and frees memory for every block translated (about 17,000 times in ten seconds of Ninja Gaiden): the table of the block's constants is made once and emptied in one step.

- The GPU no longer allocates memory each time it raises an interrupt (a few hundred times in ten seconds of Ninja Gaiden): the callback that raises it is made once and reused.

- The emulated graphics chip no longer draws every frame a second time, the old VGA way, into a picture nothing shows: the core shows what the GPU renders. That drawing allocated memory on most frames and made the game's next writes to the screen's memory take the slow path again.

- OpenGL: when a game reads back a picture it drew into a swizzled texture (render-to-texture effects), the GPU now puts it in the Xbox's swizzled order itself, shrinking it to the Xbox's size on the way at higher internal resolutions, and the driver writes it straight into the Xbox's memory. Before, it was read into a scratch buffer and the CPU shrank and swizzled it pixel by pixel. Depth buffers keep the CPU path.

- Vulkan: a cached texture can now switch from a private image to sampling a compatible render surface in place. Previously, a texture first loaded from RAM or used for feedback kept copying that surface even after it could be sampled directly (one 640x480 image copy per rendered frame in the tested Jet Set Radio Future scene at native resolution). Recorded draws retain their original images until they finish.

- Vulkan: every picture the game draws into (its render targets) now takes half the graphics memory and half the allocations on non-AMD GPUs. Each one used to come with a second, same-size image that the Xbox's memory was copied through on its way in, an extra copy of the whole picture each time; that detour is only needed for a driver bug on AMD, where it stays. Render targets the game drops are also kept for reuse, within the GPU's memory budget, instead of being freed and allocated again.

- Games that wait in a loop for a hardware register to change (the vblank, the GPU) no longer run many times slower when the compiled loop also stores to memory each turn, which game code does when it keeps the value it read on the stack. Such a loop was taken for real work, so the Xbox's time was spent instruction by instruction instead of moving on to the next event: the test ROM's display test ran at 0.08x real time instead of 3.7x. A tight loop reading the same unchanged register over and over is now recognised as a wait however it is compiled, and sooner than before (a plain polling loop runs about three times faster too).

- CPU Boost: repeated STOS memory fills now chain between translated iterations, as MOVS copies do. Each element still uses the normal guest store and instruction-budget checks, with the same debugger, interrupt-shadow and shortened-budget exclusions.

- A game waiting for room in its GPU command buffer no longer keeps a PC core busy spinning while the GPU works through it: reading the GPU's read pointer while the GPU still has commands left counts as a wait, so the core waits for the GPU instead (Jet Set Radio Future's boot: about 2% less time per frame).

- Vulkan: converting depth buffers between the Xbox's format and the PC GPU's (when a game reads its depth buffer back, writes it with the CPU, or uses it as a texture) no longer makes the core stop and wait for the GPU to finish all its work every 1,024 conversions. Depth read-backs now also go to the GPU together with the other read-backs, in one submission with one wait.

- CPU Boost: repeated MOVS copies chain between translated iterations instead of returning to the dispatcher for every element. Each element still performs the normal guest load/store and instruction-budget checks; debugger, interrupt-shadow and shortened-budget blocks retain their existing paths. No host bulk copy replaces the emulated memory accesses.

- The emulator thread no longer takes a lock shared with the GPU thread when a game touches memory the GPU draws into. It used to wait for the GPU thread to finish up to 64 commands first; now it only marks the pages it touched and the GPU thread picks that up before its next command. It still waits when the game reads something the GPU has drawn but not yet written back to memory, and then only for that read-back.

- Vulkan: when several pictures the GPU drew are read back in a row (all of them for a savestate, so every frame with run-ahead; those a texture or a CPU read needs), they are now read back in one go, with one wait for the GPU instead of one wait per picture.

- Savestates are smaller and quicker: loading one takes a quarter less time (Ninja Gaiden Black: 79 ms instead of 104, mid-game) and saving one a fifth less (88 ms instead of 112). The GPU's two inline vertex and index buffers went into every state whole, 4 MB written and read a number at a time, although at the end of a frame they nearly never hold anything; a state now holds only the part in use. States saved before load as they did.

- Vulkan: when a game reads back what the GPU drew (a render-to-texture effect, a screenshot, a savestate), the GPU now writes the picture straight into the Xbox's memory itself. Before, it went through a staging buffer the CPU then copied into the Xbox's memory row by row, and a swizzled picture was copied once more into a scratch buffer and swizzled by the CPU; a depth buffer also took an extra GPU copy into the staging buffer. Now there is no CPU copy at all, and swizzling is done on the GPU. Where the GPU driver cannot use the Xbox's memory directly (no host-memory import, such as on MoltenVK), the old path stays, minus the scratch copy. OpenGL: a swizzled picture read back asynchronously is swizzled straight out of the driver's buffer instead of being copied out first.

- Graphics: a game that renders into a swizzled texture with a surface pitch that does not match its width now gets the picture the Xbox gets. The Xbox ignores the pitch of a swizzled render target and lays its texels out by width and height alone (measured on hardware by nxdk_pgraph_tests); the core used the pitch, so a pitch smaller than a row folded the rows onto each other when the picture was read back, and a larger one made the core treat memory past the surface as part of it, throwing away unrelated surfaces there. Both renderers. Based on xemu pull request 942 by Matt Borgerson.

- CPU Boost uses a 65,536-entry dispatch cache instead of 16,384 entries, reducing collisions and expensive block lookups. The per-CPU allocation grows by 768 KiB; CPU Boost off still uses the original 4,096-entry lookup range. Page invalidation and block-validity checks are unchanged.

- CPU emulation no longer revives a translated block deliberately discarded to learn an I/O instruction boundary. Its guest bytes were unchanged, so the reuse cache kept restoring the old block and forcing the same device access to rewind it again. Ordinary code invalidation still retains block reuse.

- Saving a state is about a fifth faster (Ninja Gaiden Black: 112 ms instead of 142). The state fills only part of the buffer RetroArch gives it (66 MB of 146 MB there), and the core zeroed all of the rest, so the system had to clear every page of it first; it now writes zeroes only where the buffer is not already zero.

- CPU memory writes: updating a clean page's translation flags skips empty MMU modes, matching the earlier GPU dirty-range optimization. Populated primary and victim mappings still receive the same flag updates under the existing lock.

- Vulkan: a depth buffer uploaded from the Xbox's memory (a game writing its depth buffer with the CPU, a loaded savestate) no longer stalls the GPU thread until all earlier uploads have finished on the GPU; the GPU orders them itself.

- Vulkan: uniform-only draw changes reuse their descriptor set, passing the two uniform offsets at draw binding instead of rewriting the uniform and texture descriptors. Texture/shader changes still use immutable sets, and upload-buffer rotation still retires the recording before reuse.

- Reading the Xbox CPU's timestamp counter (rdtsc: games time with it, and the kernel's performance counter is it) is cheaper: the conversion from the Xbox's time used a 128-bit division, a quarter of the time of a loop timing a delay with it; it is now the same value from 64-bit arithmetic. A loop polling an I/O port that keeps the same value (a status port) is a wait too now, as one polling a memory-mapped register already was. About 6% less time per frame on Jet Set Radio Future's boot (lavapipe).

- Vulkan: changing one shader stage's uniforms no longer copies the unchanged stage's block into the upload buffer. Module switches invalidate their own stage; buffer rotation/reset still uploads both. Capacity checks include each stage's alignment padding.

- Games that poll a hardware register in a loop (waiting for the vblank, or for the GPU to catch up) no longer burn the PC's CPU doing it: a loop that only reads the same register and gets the same value is a wait, like the kernel's idle loop, and the Xbox's time moves on to the next event at once. Each such read had also been costing a full exit from the translated code and a rewind of its block; the instructions that read devices now end their blocks, so no read pays that again. Measured on Jet Set Radio Future's boot (lavapipe): 23% less time per frame, the worst frame 318 ms instead of 2.9 s. The core's test suite runs in half the time.

- Performance: GPU memory dirty checks skip empty CPU address-translation modes and avoid touching large TLB metadata for mappings outside the affected range. Write tracking, watchpoints and memory-access behavior are unchanged.

- Ninja Gaiden Black (and any game that waits for its GPU to finish) no longer runs in slow motion with Deterministic Emulation off. While a game waited for its GPU, the core moved the Xbox's time on to its next timer instead of waiting, again and again, so the whole frame passed while the GPU was still drawing (16.5 of 16.7 ms skipped in a heavy scene): the game missed its frames, and each run took about 1 ms. Now the game waits for the GPU, and its time moves on only by as long as the GPU work really took.

- Memory: direct DMA mappings honor GPU surface access callbacks before lending RAM. Pending GPU writes are settled before DMA reads or overwrites the range, preventing a later readback from replacing newer DMA data while keeping the direct transfer path.

- Audio: steady-pitch voices reuse their quantized source step instead of recalculating an exponential every MCPX frame. Pitch ramps, resets and multipass transitions invalidate the cached step.

- Audio: ADPCM voices decode directly from guest RAM for stream blocks and buffered blocks inside one SGE page. Only blocks split across SGE pages use a gather buffer, reducing copies and address translations without retaining guest pointers.

- Tests: the shader seed validation tool links the libretro timing implementation required by CPU feature helpers, allowing the Windows test suite to continue past its shader-tool build.

- Audio: the eight-point resampler exposes its fixed tap loop to the compiler without changing sample accumulation or adding copies. Native x86-64 GCC tests keep identical output while reducing the measured fractional-pitch kernel cost; this is not a whole-game performance claim.

- Audio: natural source endings wait for decoded samples and the eight-point interpolation window to reach the mix before completing the voice, instead of cutting off short voices or their tails during lookahead. Inclusive buffer and stream-segment ends retain their last sample. Persistent stream underruns stay active for refill; explicit stops remain immediate. VP reset clears transient playback history, and a zero-progress fetch cannot stall the frame.

- Audio: register, envelope and LFO pitch changes ramp across each MCPX frame in logarithmic pitch space. Steady frames calculate one source step; changing frames avoid a scratch step array. Voice restart begins at its current pitch.

- Audio: guest MCPX voices use eight-point Lagrange interpolation with a fixed-point source cursor instead of the Hermite approximation. Circular history avoids shifting samples, and integer source positions bypass interpolation.

- Vulkan: when a game's CPU reads a picture the GPU drew (a screenshot for a save, effects done on the CPU), the read-back buffer is no longer mapped and unmapped each time, and only the rows read are made visible to the CPU instead of the whole 64 MB buffer.

- Audio: both voice LFOs now modulate volume, pitch and filter cutoff with signed depths and optional delays. Previously ignored delay/depth writes are handled, explicit mute remains silent, and processing reads each packed LFO word once per frame.

- Audio: DLS2 and ParaEQ cutoff changes move across each 32-sample voice frame. Existing coefficient storage holds the next starting value, avoiding duplicate endpoint arrays; restart and re-enabled filters begin at their current target.

- Audio: the filter envelope now bends DLS2 and ParaEQ cutoff using the signed cutoff scale, including the mono cascade and null-HRTF filter paths, without another sample frame.

- DVD reads go from the disc image straight into the Xbox's memory, the whole read at once, as the hard disk's already did. They went in 128 KB pieces, each read into a buffer allocated for it, copied into the drive's own buffer and copied again into the Xbox's memory: two copies of everything a game loads from its disc. On Android, where RetroArch hands the core its content through the storage framework, the image's reads also no longer go through a buffer allocated for each.

- Audio: volume and envelope changes ramp across each 32-sample MCPX frame, including first-frame fade-in and the DSP-off output. Integer ramp steps replace per-sample rounding calls; no extra sample frame is copied.

- Audio: envelopes use measured phase lengths and quantized levels; attack and decay start from zero, release has its measured tail, and an inactive filter envelope contributes no pitch modulation. Envelope processing reads only parameters needed by its active phase.

- Audio: voice attenuation follows the measured MCPX curve, including the quiet tail and silence at 63 dB. A shared constant table replaces per-route exponential calculations.

- Audio: spatial voices now apply independent I3DL2 direct and room filters in modes 1-3; room sends 5 and 7 use the room filter. Processing reuses the discarded source-channel slot instead of allocating or copying a room frame.

- Audio: ParaEQ modes now apply their frequency, signed gain and Q fields, including the DLS2/ParaEQ cascade for mono voices. The filter operates in place and voice restart clears its history.

- Audio: DLS2 filters use measured coefficient and state rounding, retain their intended gain and resonance, and no longer apply an artificial waveshaper or output clamp. Mono 2D filter parameters are computed once for both histories.

- HRTF: both ears share one mono history and borrow the active entry during processing, removing duplicate history writes and a target-parameter copy while retaining independent ear delays and gains.

- HRTF: edits to active entries and changes of handle take effect on the next processed frame; tap gains move across that 32-sample frame. A restarted voice begins at its current entry rather than stale coefficients.

- HRTF: 3D voices use channel zero, routes 0-1 carry the left ear and 2-3 the right, while routes 4-7 retain the dry voice. Null HRTF entries silence only the ear routes and select the existing 2D DLS2 behavior. All routes use their mix bin headroom; HRTF bypass avoids a frame copy.

- The Xbox's CPU runs on while its GPU draws, as on the console. The core used to stop the Xbox's time whenever the GPU's thread had work, so a frame cost the CPU's work plus the GPU's (about half of each frame was spent stopped, measured on Jet Set Radio Future); now they overlap, and at the end of each run the core waits for the GPU's thread to finish what the frame gave it and to draw the display from it, so each run still hands over the picture drawn in it: no repeated frame and no frame of delay. System > Deterministic Emulation (off by default) brings back the old behaviour, where every run of a game is the same however fast the PC is, for Run-Ahead and netplay.

- Audio: the emulator worker mixes directly into the DSP output during parallel frames too, avoiding a redundant 4 KiB scratch clear and full-buffer accumulation each frame.

- Audio: nonstandard multipass bins, uncleared submixes and interleaved source lists run in voice order instead of asserting. Serial frames mix directly into the DSP buffer, and parallel scheduling reads each voice dependency once.

- HRTF: filter taps retain their hardware gain and use the measured sign-magnitude minifloat format; updating a zero left-ear filter also updates the right ear.

- Audio: multipass submixes restore their voice format headroom (up to 42 dB previously lost); mix-bin conversion rounds halfway values toward positive infinity to match hardware.

- Audio: setting DSP output buffers no longer overwrites voice sample addresses, and an inactive linked voice with its trap disabled no longer aborts the core.

- A game that black-screened after the Xbox logo on every boot after RetroArch crashed or was killed with it running (Jet Set Radio Future, Ninja Gaiden Black) starts again without Trim HDD on Close: the core marks the hard disk image open while content runs, and finds the mark at the next load when the last session never closed. It then empties the cache partitions X, Y and Z before the game starts - they only ever hold caches the games rebuild - since a session cut off mid-write could leave them naming data that never reached the image, and the game read its broken cache back. C and E, the system and the saves, are not touched.

- A reset (RetroArch's Restart, and the reset a savestate load starts from) with a game's audio running crashed the core: the APU's global registers were cleared but its two DSPs kept running their program against them, and the next audio frame divided by a FIFO of no size (or walked past an empty page table). The reset now stops the DSPs' thread for its frame, clears the processors' registers too and resets both DSP cores, as the hardware's reset does.

- The emulator thread's hold on the GPU for a reset, a savestate or a renderer switch is a request count and two eventcounts: the GPU thread parks between two batches, says so, and sleeps until the hold is released, where it signalled a condition variable under the FIFO's lock and the holder took both locks. The renderer's own lock and condition variable around the frontend's framebuffer are an atomic flag and an eventcount. The surface scale change halts the pusher with the flag alone. A one-time message to stderr at the first flip stall is gone.

- Vulkan: a game's picture went black, or froze, when the game showed a frame without drawing to it again for a while - a loading or title screen, a movie the CPU writes into the front buffer (Jet Set Radio Future after the Xbox logo). Since the display is drawn at the guest's flip, nothing marked the front buffer as on screen any more, so after five frames it was dropped as an old surface; and a front buffer the CPU wrote, or a change of display mode, did not count as a new picture. The display now marks it at every look, and redraws when the CPU wrote the front buffer or the mode changed, as OpenGL's display does.

- Vulkan: a game that cleared the screen and then drew with nothing else changed drew with the clear's pipeline, which has another layout and none of the dynamic states a draw sets; NVIDIA's driver crashed on it (Jet Set Radio Future at boot, since the pipeline-dirty fix above stopped every vertex-array draw from looking its pipeline up again, which had hidden it). The clear's pipeline now always counts as changed for the next draw, and a pipeline evicted from the cache - a pipeline the worker made landing in its place - is never bound again from a stale pointer.

- Vulkan: with dynamic vertex input (every desktop driver), a draw with a vertex array counted its pipeline as changed every time - the key carries no vertex descriptions in that mode, and the check compared the live ones against the key's zeros - so each draw rebuilt, hashed and looked up a 3 KB pipeline key for nothing; the check skips the descriptions in that mode. The shader state is hashed once per change, not twice.

- The frame reaches RetroArch without the frontend's thread waiting for the GPU thread, on either renderer. Vulkan: the GPU thread draws the display when the guest moves the scanout (its flip), into the next of the renderer's own frame images, and publishes it; retro_run takes the last published one - no lock, no wait - and repeats the last frame when nothing new was drawn, asking the GPU thread for another look (a front buffer drawn in place, the video overlay). Before, every retro_run took the FIFO's lock, asked the GPU thread to draw the display into the frontend's image of the moment, and waited for it. OpenGL: the front-buffer ticket passes through a three-slot mailbox with no lock, the surface references are atomic counts freed on the render thread, the UI's fence is swapped atomically, and the first frame no longer waits for the render thread (it is taken at the next run).

- A request another thread makes of the GPU thread and waits for - a flush, a display sync, a surface download for a CPU read, the dirty surfaces before a savestate, the shader cache or seed written out at shutdown, a renderer switch - is a flag and a libretro-common eventcount, where it was a flag and a QEMU event with a lock taken around the asking; the requester takes no lock. The OpenGL shader cache's shutdown writeback released its waiter only when the list file could be opened; it does so either way now.

- The guest's PGRAPH and PFIFO register reads and writes take no lock: every register is one atomic word, the interrupt words are changed with atomic bit operations by the vCPU (the ack) and the GPU thread (the raise), the flip counter in NV_PGRAPH_SURFACE with compare-and-swaps by both, and a context restore by the driver is taken by the GPU thread at its next method, which then reloads the switch registers and counts every register as changed. The GPU thread's "waiting for a NOP" and "waiting for a context switch" are the interrupt bits themselves, not flags beside them (a savestate keeps its two bytes, unused). A one-time message to stderr at the first flip is gone. The frames are the same on both renderers.

- The GPU thread's wake-up is lock-free: a kick (the guest's pushbuffer PUT write, the vblank, the display asking for a frame, a surface sync) sets a flag and notifies an eventcount, where it took the FIFO's lock and signalled a condition variable; the thread prepares its wait under the lock and commits it without, so no kick is lost and no kicker waits for the thread. The guest's pushbuffer registers (PUT, GET, REF) are read and written as single atomic words, with no lock at all, where every write took the FIFO's lock the GPU thread was holding. The GPU thread runs the pushbuffer's methods in batches of 64 under the two locks it used to take and release around each (four lock operations a method, 14,000 methods a frame in Dead or Alive 3), letting both go between batches and at once when a guest-side thread wants one (`pfifo_guest_lock`: a register access from the vCPU, a display request), so no one waits longer than before - the method under way - and the thread spends 60,000 fewer lock operations a frame. The frames are the same on both renderers. Measured on lavapipe, where a method is a software draw and the vCPU's waits for the GPU's locks swing with the machine's scheduling, the wait totals are within their run-to-run spread; the gain is the render thread's lock traffic, which a real GPU's frame budget feels.

- The code is the core's only: every `#ifdef LIBRETRO` is gone, with the standalone's side of each (QEMU's `main()`, the wall-clock vblank and flip timers, the standalone's thread and interrupt paths, its timed waits), and the `LIBRETRO` define with them. The source reads as what runs. (Merges of upstream xemu now conflict where upstream touches a line that was in one of those branches; the resolution is the core's side.)

- No allocation on the surface and texture paths mid-game, on either renderer. What was allocated and freed as surfaces and textures came and went now comes from pools made at the renderer's start (`hw/xbox/nv2a/pgraph/pool.h`): the surface bindings (both renderers), OpenGL's texture bindings, its query reports (one per report the game asked for, each frame) and the display's tickets. The copies a surface goes through - a swizzled surface written back to RAM, OpenGL's unswizzle and pitch repack on upload, its surface-to-texture read and the video overlay's YUV conversion - go through buffers kept for the renderer's life, sized at start for a 1024x1024 32-bit surface and grown only by a larger one. The debug labels of Vulkan images are formatted on the stack. The frames are the same (Dead or Alive 3, both renderers).

- Vulkan: a game with a learned seed of more shader states than the first-meet worker's ring holds asserted at boot (`put == sizeof(d)`, shaders.c): the seed was queued whole, the worker compiled it all, and the ring that hands the modules back to the render thread - drained only when a draw looks - overflowed. The queue is now bounded by what has been drained (a ring's worth of states at a time), the seed is fed from a list a ring's worth per flip until it is through, and a module the ring cannot take is dropped (the state then draws as its sibling) rather than asserted on. Reproduced here with a 9,334-state seed.

- Vulkan: a state met for the first time no longer compiles its pipeline on the render thread. Its shaders already came from a worker while the draw used the state's dynamic sibling; now the pipeline does too: once the words land, the draw path hands the whole pipeline (the sibling's key with the state's own shaders) to the pipeline worker and keeps drawing with the sibling, and the state is switched to once its pipeline is in the cache. On a real driver that compile was a few milliseconds on the frame per new state, each session, for the states the seed and the pipeline cache did not cover. The draw path no longer waits on any worker: the fragment half that the fork compiled on the worker while the render thread compiled the vertex half (then waited for) is compiled on the render thread with the rest, for the states that have no sibling and no seed. The pipeline worker (`vk-pipe`) and the seed library pump are one thread fed through a lock-free ring (release/acquire counters, an eventcount to sleep on; the two mutex and condition-variable pairs are gone); at the renderer's end what is queued is skipped and only the compile under way is waited for, the same for the shader worker. Verified on lavapipe (Dead or Alive 3's frames identical with and without; 22 states landed ahead in a 300-frame fight, none compiled twice); the hitch it removes is only measurable on a real GPU.

- Vulkan: the GLSL to SPIR-V compiler takes `out` and `inout` function parameters. The geometry shaders of games that draw quads (`calc_quadz`, two `out mat4`s) use them, and the compiler refused them, so those games asserted at their first draw - Dead or Alive 3 on a real GPU among them, which the lavapipe runs here never reached. Every Vulkan shader of the shipped seed dictionary (74,614, for every state the games are known to meet, both push-constant variants) now compiles; the test suite runs them all (`libretro/tests/seedshaders.c`, `make seedshaders`, in `run.sh`), each distinct module passes the Vulkan validator and reflects as glslang's did. A shader the compiler still refuses is written to `shader_compile_failed.glsl` next to the session log, which names the line and the reason.

- The test harness measures stutter (`harness --stutter`): per run, `retro_run`'s time on the CPU and the frame's time on the GPU - a timestamp the GPU writes once it is through with the frame (a Vulkan timestamp query in the harness's readback, a GL timer query), never waited for - reported as mean, median, 99th percentile, max, the runs over twice the median and the five worst, so a change is judged on its worst frames and not its average; `--csv` then carries both times per run.

- Loading a savestate reads it where RetroArch hands it over: QEMU's state reader (`QEMUFile`) takes its 32 KiB window straight over the state instead of copying the state into its buffer 32 KiB at a time first, so a RAM page is copied once (into the guest's RAM) and what a device's loader takes in place is not copied at all. One pass over the state fewer (79 MB for an Xbox game, more for Chihiro); the load's time on this machine is within its run-to-run noise either way, as the load is mostly elsewhere (the reset, the devices, the TLB and translated code the RAM load flushes).

- Vulkan: the shaders are compiled to SPIR-V by the core's own compiler (`hw/xbox/nv2a/pgraph/spv`, plain C, one pass), and glslang is gone from the core. The compiler takes the GLSL the NV2A generators and the renderer's compute and display shaders write - and only that: a construct they do not write is an error, not a guess - and makes SPIR-V 1.5 or 1.6 that the Vulkan validator passes and that reflects (sets, bindings, blocks, offsets, strides, locations) exactly as glslang's did. On this machine it compiles the 41 shaders of a Dead or Alive 3 fight in 0.18 ms each on average where glslang took 7.7 ms: 42 times faster, so a shader the game meets for the first time costs a fraction of a frame instead of most of one. The first-meet worker that compiles unseen states off the render thread now hands the words over through lock-free rings (libretro-common's SPSC queue and eventcount; the mutex and the condition variable are gone) instead of writing a `.spv` file the render thread then read back from disk: the `vk_shader_cache` directory is no longer written or read. Both GPUs (GPU boost on and off) compile this way. The frames are pixel for pixel what glslang's shaders gave (Dead or Alive 3, 120 frames hashed, on lavapipe; the compiler has not run on a real GPU here). The Linux build needs no `glslang-dev` or `spirv-tools`; the Windows build no `deps/glslang`; the meson builds for macOS and Android no glslang subproject or package.

- Files the core reads whole are read where they lie: libretro-common's filestream maps them (POSIX mmap, Win32 file mappings) and the parser, the driver or the comparison reads the page cache in place, with no buffer of the file's size allocated and filled first. Chihiro: the netboot image is mapped once for the session when the game is put in the machine, and every savestate - saved or loaded - diffs the DIMM against that mapping, where each one used to allocate a buffer as big as the DIMM, read the whole image into it and CRC it (a few hundred MB, twice per save and load); its CRC is taken once. Also the shader seeds, the OpenGL shader and module caches (the driver takes the program binary straight from the file), the Vulkan pipeline cache and the network firmware's version. Where files cannot be mapped the file is read in one call instead. The game's disc and HDD were already read by QEMU's block layer straight into the guest's RAM (DMA) and stay so. `libretro.h` and `libretro_vulkan.h` live in `libretro-common/include`, as in RetroArch.

- libretro-common's inflate (the seed dictionary, the HDD image's compressed clusters) synced from RetroArch, where it got faster: a 4 MiB binary decodes at 375 MB/s on this machine (338 before), the seed dictionary's kind of data at 272 (226), stored blocks at 1.7 GB/s (1.1), and a gzip header that an input slice ends inside no longer fails the stream.

- zlib and xxHash are gone from the core; libretro-common's own code does what they did, identical in every value. Hashing (`fast_hash`: shader states, texture and surface keys, the shader cache's file names, the seed dictionary) is libretro-common's XXH3, which picks AVX-512, AVX2 or SSE2 at run time instead of the SSE2-only build of xxHash the core carried: the same hashes, three times the throughput here. The Chihiro DIMM image's CRC-32 is libretro-common's carry-less-multiply CRC: twelve times the zlib 1.3.1 the Windows build carried. The seed dictionary, compressed qcow2 clusters (the shipped HDD image is compressed), `gunzip` and the guest memory dump use libretro-common's inflate and deflate; its inflate runs half as fast again as that zlib 1.3.1 on this machine and a quarter slower than a distribution's patched zlib. The Linux build needs no `zlib1g-dev`, the Windows build no `deps/zlib`, and the Windows build's unused copy of miniz is gone. `ui/libretro.h` is RetroArch's current header. (The meson builds for macOS and Android still link zlib for the rest of QEMU.)

- Vulkan: the GPU is no longer waited for at every flip. At each frame's flip the GPU's thread submitted the frame and waited on the CPU until the GPU had drawn all of it (and the emulator thread, with nothing to do while the guest waited for the GPU, waited behind it): a frame cost the game's CPU time plus its GPU time, one after the other. Now the flip submits and goes on; the guest starts its next frame while the GPU draws this one, and the one wait left is the ordinary one of a double-buffered renderer, when a frame's command buffer is to be reused and the GPU is still two submissions behind. What the wait used to make safe is handled as submissions retire instead: each slot has its own half of the descriptor sets; resources evicted mid-frame (pipelines, samplers, images) are destroyed once the last submission that could use them has retired; the frame's GPU time (which decides how often the renderer submits early) is read back from the submissions that have retired, through a ring of timestamps. The display's draw, which reads the frame's surface, is ordered after the frame on the GPU rather than waited for. With Real hardware speed on (standalone only) the flip stays synchronous, as its fragment queries are read there. Verified for correctness on lavapipe (frames identical, no validation errors, savestates); the gain is on a real GPU, which this machine has none of.

- Vulkan: swizzled textures and surfaces are unswizzled by the GPU. A compute shader reads the Morton-ordered texels straight from the Xbox's RAM (its buffer) and writes them out linear into a 24 MB device-side ring, from which they are copied into the image; an 8-bit palette texture is looked up through its palette on the way. Nothing of a swizzled level passes through the CPU any more - the last CPU pass on the texture path. YUV and R6G5B5 textures, the few that need a conversion the shader does not do, still go through the CPU and the staging ring, as does everything where the RAM could not be imported. The pictures are bit for bit what the CPU made (Dead or Alive 3, hashed).

- Vulkan: the core offers version 2 of libretro's context negotiation interface (`create_instance`, `create_device2`): RetroArch makes the instance and the device through the core and adds its own extensions and features to them on the way, which is how it gets its low-latency path (NVIDIA Reflex through `VK_NV_low_latency2`, AMD Anti-Lag) and its video queues on the core's device. A frontend that knows only version 1 is answered with version 1, as before (the core asks which version the frontend supports; one too old to be asked gets version 1). `ui/libretro_vulkan.h` is RetroArch's current header. The core's tests run the Vulkan context both ways.

- OpenGL: DXT1/3/5 textures are uploaded as they are (`glCompressedTexImage`, with `GL_EXT_texture_compression_s3tc`, which every desktop GL has) straight from the Xbox's RAM, instead of being decoded to RGBA8 on the CPU, and linear textures go up straight from the RAM too; a swizzled level is unswizzled into a scratch buffer the renderer keeps (grown to the largest level once, never allocated per upload) instead of one `malloc`ed and freed for every level. Without the extension the CPU decode stays, into the scratch.

- Vulkan: the Xbox's RAM (64 MB, or Chihiro's 128 MB) is imported as a Vulkan buffer where the GPU can take host memory (`VK_EXT_external_memory_host`: NVIDIA, AMD, Intel and Mesa drivers have it), and what the GPU can take as it lies there is copied straight out of it: DXT textures where they go up as BC, linear textures and linear surfaces (their rows the pitch apart). No pass through the CPU and no staging for those; swizzled textures and surfaces still go through the staging ring, unswizzled. Without the extension, or if the driver cannot import the RAM, every upload goes through the ring as before.

- Vulkan: texture and surface uploads no longer wait for the GPU or allocate. Every upload waited on the CPU for the previous one's command buffer to finish before it could reuse it and its staging buffer (a GPU round trip per texture), decoded or unswizzled each mip level into a buffer `malloc`ed for it, then copied that into the staging buffer, mapping and unmapping it each time. Now eight single-time command buffers rotate, each with its own fence, and a 64 MB staging ring stays mapped: a level is decoded, unswizzled or copied straight into the ring, the command buffer is submitted, and the only wait left is when the ring is full or the GPU is eight uploads behind. Where the GPU samples BC (`textureCompressionBC`; every desktop GPU), DXT1/3/5 textures are uploaded as their blocks are, as BC1/BC2/BC3, instead of being decoded to RGBA8 on the CPU - a quarter to an eighth of the bytes, and no decode; a cubemap's border crop is a proper top-left crop now. Where the GPU has no BC the CPU decode stays, into the ring. Surfaces uploaded from the Xbox's RAM are unswizzled straight into the ring too. The display's descriptor set is one of eight, as its command buffer is no longer waited for before the next frame's set is written (the validation layer flagged this after the previous change).

- A frame less of lag: the guest's vblank now comes just before the end of each run of guest time (1 ms of it before, the time its vblank interrupt takes to flip), not at the start. The frame the game drew in a run is flipped at that vblank and handed to RetroArch when the run is done - drawn between two vblanks and shown at the next, as on the Xbox. Before, the vblank at the start of a run flipped the frame drawn in the run before, so RetroArch got every frame one run late: the pad RetroArch read at the start of a run reached the screen two runs later, now one. The frames themselves are the same (the same sequence hashed on Dead or Alive 3).

- Vulkan: the frame goes to RetroArch without the emulator waiting for the GPU. The display was drawn into an image of the renderer's, copied into the frame image of RetroArch's sync index, and the GPU's thread then waited for the GPU to finish all of it (and the emulator thread waited for the GPU's thread) before the frame was handed over, every frame: the game's next frame could not start on the CPU until the GPU had drawn this one. Now the display is drawn straight into RetroArch's frame image, the command buffer is submitted and handed over as it is, and RetroArch's own work on the frame is ordered after the draw on the GPU: by the queue where the renderer draws on RetroArch's queue, and by a pair of semaphores the renderer owns where it draws on a queue of its own (never by semaphores handed to RetroArch, which a frame skipped under fast-forward would leave hanging). The copy is gone too. The frames are pixel for pixel what they were (checked on Dead or Alive 3, 120 frames hashed through the core's tests on lavapipe).
- The core's test harness has a Vulkan mode (`harness --vulkan`), a Vulkan context as RetroArch's vulkan driver gives one: the device made by the core's negotiation interface, the frame an image the core hands over, read back through a command buffer per sync index on the shared queue, under the queue lock, with `--hash-video` and `--shot` working as on OpenGL. `run.sh` runs the idle guest on it where there is a Vulkan device (Mesa's lavapipe will do; CI installs it) and skips it where there is none. The Windows harness prints its lines without carriage returns, which `run.sh`'s checks of the pad test did not match under Wine.

- The DSP JIT runs on AArch64 too (Linux and Android builds, and Windows on ARM): the translator is one for all hosts, and each host's instructions are in a file of their own (`hw/xbox/mcpx/apu/dsp/jit/host_x86.h`, `host_a64.h`). Checked with `dspdiff` under qemu-aarch64 on the test program and on Dead or Alive 3's GP and EP programs: the same state as the interpreter after every frame. Android builds had run the interpreter, and had no DSP JIT option; they have the JIT and the option now.
- `libretro/tests/dspdiff` (`make dspdiff`) runs a DSP program on the interpreter and on the JIT side by side and compares the whole DSP state after every frame; `libretro/tests/run.sh` runs it on the test ROM's GP program. It takes a dump of a game's DSP too, so a JIT bug in a game's program can be pinned down without the game.
- The DSPs' DMA converts the DSP's words straight into and out of the Xbox's RAM, page by page, with no buffer in between (it went through a 24 KB scratch buffer each way, and read or wrote the DSP's memory a word at a time through a function call); the sound engine's mixbins go straight into the GP's mixbuffer and the GP's 32 output samples straight into RetroArch's queue (both went through a function call per word, the output through a copy as well); and the EP's output lands in the DSP thread's buffer as the DMA makes it instead of being copied there. The sound is bit for bit what it was.
- The APU DSP's JIT is the core's own, in C (`hw/xbox/mcpx/apu/dsp/jit`), in place of the prebuilt Rust library (mborgerson's dsp56300, Cranelift) the core downloaded at build time. It translates the DSPs' programs to x86-64 code a block at a time on the interpreter's own state, instruction for instruction and flag for flag (the interpreter and the JIT leave the same registers and memory after every frame, checked on Dead or Alive 3 and the test program), with the accumulators in host registers, the condition codes computed once for a run of operations, REP loops as loops, DO loops and branches going straight to the next block, and the code of a program that rewrites itself dropped a word at a time instead of all of it. On the test program (16 x 4096 MACs a sound-engine frame) it runs 2.5 times real time where the Rust JIT ran 1.7 and the interpreter 0.4; in a Dead or Alive 3 fight the DSPs' thread takes half the CPU time it took on the Rust JIT and a third of the interpreter's. On hosts the JIT has no code for yet (AArch64) the interpreter runs, as before on Android. Nothing is downloaded at build time any more, `make HAVE_DSP_JIT=0` is gone, and the Windows DLL no longer carries the Rust runtime.
- Closing content stops and frees the APU's DSP thread and both DSPs, which were left running and leaked with every load, and frees the sound queue; the DSP DMA no longer grows a buffer with `malloc` on the DSP thread (leaking the old one each time). Content loaded after another (Vulkan) has sound again: a leftover flag from the first content left the new one's output point unset, so it played nothing.
- A stick pushed fully up no longer reads as fully down (Jet Set Radio Future turned round instead of running forward; halfway up worked): RetroArch's full up is -32768, and turning it the Xbox's way up overflowed a 16-bit value. It is 32767 now, on both sticks.
- Sound no longer stutters. A sound-engine frame that fell while the game held a voice locked (it was changing it) was put off, and one put off past the end of the video frame left that frame short of samples and the next one over: a dropout of up to a video frame about every second in games that lock voices often, worse under fast-forward and after a state was loaded. A locked voice now sits that frame out instead, as the engine keeps its tick. The sound's queue also starts with two engine frames instead of one, since the 25 or 26 engine frames in a video frame against its 800 or 801 samples could leave it one short at the worst phase.
- The sound goes to RetroArch without copying: the APU's DSP thread (or the voice processor, with APU DSP Emulation off) writes each frame's samples straight into the queue RetroArch is handed a pointer into, with the volume applied on the way, where before every sample was copied three times (into a frame buffer, through the volume, into the queue) and once more out of it. The DSP thread's input (the voices' mix) is written by the voice processor into the thread's own buffer too, instead of being copied over. The engine's frame no longer waits for the DSP thread before mixing the voices: the two overlap, the mix of one frame alongside the DSPs on the one before.
- libsamplerate is gone. The core never resampled its output (48 kHz, raw, as the Xbox makes it); the library was only the APU's per-voice pitch, every voice's samples played at its rate through a sinc filter, which allocated at the first play of each voice. That is now a 4-point cubic interpolation in the voice processor, allocation-free. Linux builds no longer need `libsamplerate0-dev`, Windows builds one library fewer.
- Sound with APU DSP Emulation on no longer crackles: the DSP thread's encoder rewrote the output every 8th sound-engine frame while the engine was reading its frame of it, so every 8th frame could come out garbled or silent, and a fast PC hit it most. The engine now reads its frame before the thread is started on the next.
- The core is ParaLLEl Xbox: `parallel_xbox_libretro.so` / `.dll`, core name `parallel_xbox`, "Microsoft - Xbox / Sega Chihiro (ParaLLEl Xbox)" in RetroArch. Its files stay under `system/xemu/`, and its options keep their names, so nothing of yours moves. The Windows build no longer links libatomic, which nothing in the core used.
- The analog sticks and triggers work: the core now tells RetroArch its ports take an analog RetroPad ("Xbox Controller"), without which RetroArch answered every stick read with zero. Rumble was already reaching RetroArch; the core's tests now drive the sticks and the motors through the Xbox's USB pad both ways.
- The APU's DSPs (the GP and the EP) run on a thread of their own, one sound-engine frame (0.67 ms) behind the engine, so the game's sound programs no longer hold up the emulator thread: with APU DSP Emulation on, a Dead or Alive 3 fight took 18% less of it. A game's own reads and writes of the DSPs wait for the frame under way, which is short.
- The game's indirect jumps and returns look their target up inline, as CPU Boost does for standalone: QEMU had kept them on a slower helper call under instruction counting. About 15% more of the game's code runs a second (Dead or Alive 3 in a fight, 111 to 133 million instructions a second on the test machine).
- The Xbox kernel's idle thread, which spins with interrupts toggling ("sti; nop; nop; cli; cmp; je") rather than halting, is recognised as waiting after a few turns (16 block ends with nothing stored), not after 8192 instructions: a game that has done its frame's work and waits for the GPU or the next vblank now costs nothing on the CPU while it waits, where before the idle thread ran 7.6 million times a second through the emulator (about half the core's time per frame in Dead or Alive 3's title screen, measured). While the GPU has work the game may be waiting for, the game is parked, with its time standing still, until the GPU is done, instead of spinning.
- The core's tests read the pad through the Xbox's USB controller the way the kernel does (`libretro/tests/rom/pad.c`), so a change that stops RetroArch's input from reaching a game is caught.
- Waiting is skipped: when the Xbox's CPU runs 8192 instructions that store nothing and touch no device, it is waiting (the kernel's idle thread spins like that, and so does a game between its frame's work and the next vblank), and its time moves on to the next event instead of being emulated instruction by instruction. Before, with the Xbox's time counted in instructions, a frame cost the full count of a real Xbox's CPU even when the game had nothing to do, 20 to 26 ms on a Ryzen 9 9950X3D, 38 to 56 frames a second and RetroArch's Core latency of 20 to 26 ms; now a frame costs the game's own work. A wait on something the GPU or the disk is doing is not skipped, it ends when the work does; a loop timed on the TSC is run in full.
- Chihiro: the media board's V850 and the network board's MIPS CPU catch up with the game's time when a millisecond of that time has passed, instead of looking every millisecond of the PC's clock.
- The Xbox's time is counted in its CPU's instructions (1.977 ns each, the pace of a Pentium III on game code that standalone's Real hardware speed uses), not by the PC's clock. Before, the game's time passed as the PC's while its CPU ran, so a frame always took at least 16.7 ms of real time whatever the PC: the core took about 20 ms a frame (RetroArch's Core latency), a game ran at 49 to 56 frames a second instead of 60 when the sound's DSPs or the GPU added their time on top, and fast-forward did nothing in a game that keeps its CPU busy. Now a frame takes only as long as the PC needs for the game's work in it, and fast-forward goes as fast as the PC can run the game. The game's speed, its sound and what it computes no longer depend on the PC's speed or load: the same frames do the same work, run flat out or paced.
- Nothing in the core waits on a clock any more: RetroArch's thread waits for the frame to be done, not at most a second; the GPU's thread waits for the host GPU to finish an occlusion query instead of looking every millisecond; a renderer switch waits for the switch, not at most 5 seconds; and the GPU's vblank and flip timers on the PC's clock are gone from the core (its vblank opens each frame of the game's time, as before). While the game waits for the GPU or a disk read, its time stands still until the work is done.
- Audio > APU DSP Emulation is on by default (standalone has it off), where the core has the DSP JIT (every build but Android's): the game's own sound programs run on the APU's DSPs, as on an Xbox, so effects such as reverb and the encoder's output are the game's. The JIT runs them fast enough (2.8 times real time on a heavy program). Options saved before keep their value: RetroArch's Reset Options, or the option itself, turns it on.
- Windows builds with the Makefile too: `make` in MSYS2's MINGW64 shell, with nothing installed but the compiler, builds `xemu_libretro.dll`, every library it links (GLib, libslirp, libepoxy, libsamplerate, zlib, glslang) built from source and linked in. The DLL needs only Windows' own DLLs, and exports only the libretro API (meson's build exported GLib's too). libsamplerate has only the converter the APU uses, which leaves 9 MB of unused filter tables out.
- The core builds with a plain Makefile (`make`), without meson, ninja or python, on Linux x86_64 and aarch64; the code QEMU's build generates is pregenerated in `libretro/gen`. Its builds run the guest's CPU about 6% faster than meson's: QEMU's trace points compile to nothing instead of being checked at run time, and it leaves out QEMU's hardening flags (zeroing every local variable, clearing registers on every return), which a game console's emulator has no need of. Windows, macOS and Android are still built with meson.
- Savestates are real RetroArch states: the whole machine (CPU, devices, the Xbox's RAM, what the GPU had drawn) goes into RetroArch's state file, about 8 MB to 79 MB before RetroArch compresses it. Before, every slot was the same snapshot inside the HDD image, as big as the RAM, and RetroArch's file held only a 4 KB marker; the HDD image also could not be trimmed while it held one. That snapshot is removed from the HDD when content starts, so states saved by earlier versions no longer load. The hard disk's contents are not in a state (they would make every state as big as all the game has written): when a state is loaded after the game has written to the disk since it was saved, RetroArch shows a warning.
- The machine no longer allocates a second, unused copy of the Xbox's RAM (64 MB, 128 MB on Chihiro and debug kits).
- Audio > DSP JIT is on by default (standalone has it off): with APU DSP Emulation on, it runs the game's sound programs about 5 times faster than the interpreter, with the same sound. Builds without the JIT's library (Android) no longer show the option, which did nothing there.
- The Xbox's time no longer passes while the core emulates its devices between the guest's instructions. With APU DSP Emulation on and a sound program heavier than the host could run in real time (on the DSP interpreter, or a slow PC), each piece of sound took longer than the 0.67 ms it covers, which made more of them due than were done: the game slowed to a frame a second instead of running somewhat below full speed. The game's CPU now also gets its full frame of time, which the devices' work had taken a little of.
- The core no longer does process setup in RetroArch's process: it installed signal handlers (on Windows a console Ctrl handler that slept 10 seconds), turned off the buffering of RetroArch's stdout and stderr, and raised the file descriptor limit.
- The guest runs on RetroArch's frames: each frame RetroArch asks for runs exactly one frame of the Xbox's time (1001/60 ms) and hands over that frame's picture and its 800 or 801 samples, no more and no fewer. The sound's pitch and pace are the game's whatever the host's speed, without the crackles and gaps that came from the sound being made by the PC's clock and the frames by RetroArch's. Fast-forward, slow motion, pause and the menu now apply to the game itself: it stops between frames (a game no longer runs on while the menu is open, Akrido's Ninja Gaiden dirty disc error after the menu), and while the Xbox has nothing to do its time moves straight on, so fast-forward goes as fast as the host can run the game. The core declares the Xbox's exact NTSC rate, 59.9401 frames a second.
- The pads are read once a frame, when RetroArch polls them, not from the emulator's thread in the middle of a frame, where RetroArch does not expect it.
- Audio > Audio Volume works: it scaled nothing in the core.
- Audio > Audio Thread is gone: the sound comes with the frames.
- Audio > Voice Processor Threads Auto is up to 4 threads (half the logical CPUs), not one per logical CPU.
- The log lines every 300 frames about the time a frame took are gone, with the waits they measured.
- The Xbox's RAM is the core's system memory for RetroArch (cheats, RetroAchievements' memory inspection).
- SDL is gone from the source: the standalone's SDL display, input and GL context code, SDL3 as a subproject, and the Xbox Live Communicator, which played and recorded through SDL and was never in the core.
- QEMU's host audio drivers (ALSA, CoreAudio, D-Bus, DirectSound, JACK, OSS, PulseAudio, PipeWire, SDL, sndio, Spice, WAV and the null driver) are gone: the core's one audio backend is the frontend. The Xbox's AC'97 was put on a host driver of its own: DirectSound on Windows and CoreAudio on macOS builds.
- Internal Resolution Scale is Internal Resolution Scale (Restart), taken when the content starts: raised while running above the scale it started with, it made RetroArch rebuild its video context and the core crashed (NNshi).
- New category Controls, after Audio: Rumble (Player 1) and (Player 2), on by default, turn off everything that rumbles that player's pad, the Xbox games' and a Chihiro racing game's force feedback; Analog Dead Zone (Player 1) and (Player 2), 0 to 30%, 10% by default, for both sticks, Xbox and Chihiro alike, with the rest of the travel scaled up to full (OutRun 2 stopped at a drive board error when the stick moved during its test) (NNshi).
- The core carries xemu-dashboard's pre-formatted hard disk (`xbox_hdd.qcow2`, 1.6 MB, MIT: an unsigned dashboard and no Microsoft software) and writes it to `system/xemu/` when there is no HDD, so only the boot ROM and the BIOS have to be supplied (NNshi).
- Options: new defaults, unlike standalone's: Skip Boot Animation on, System Aspect Ratio 16:9, Light Gun Aim Analog Stick, Force Feedback Rumble 100% and Vulkan Fast Paths No Pipeline Libraries (RalliSport Challenge 2 and Conker crash with them). The System category is System (Restart), as its options are taken when the content starts, and its options no longer say (Restart) each; Trim HDD on Close is no longer Experimental and comes last there. Shader Cache (OpenGL) is gone: OpenGL's shader cache is always on, as Vulkan's is (NNshi).
- Options: Display Filtering is gone, it set what only standalone's UI reads (RetroArch's own filtering is what applies). Shader Cache is Shader Cache (OpenGL), as only the OpenGL renderer has it to switch (Vulkan's modules are always kept on disk). System Memory, Network Backend, Skip Boot Animation, Hardware FPU Emulation and AV Pack say (Restart), as they are read when the content starts. New in Audio, with standalone's defaults: DSP JIT (off), HRTF (on) and Voice Processor Threads (Restart, Auto) (NNshi).
- New option Video > System Aspect Ratio (Restart): Dashboard (as before), 4:3, 16:9 or Letterbox, set in the emulated EEPROM's video settings at each start, so a game that supports widescreen runs in it without going to the dashboard; the EEPROM file keeps the dashboard's own setting. Aspect Ratio is now Core Aspect Ratio, after it; its Auto follows the setting (NNshi).
- Aspect Ratio Auto: 16:9 when Widescreen is on in the dashboard's video settings and the game is one that renders 16:9 then, 4:3 otherwise and always on a Chihiro. The core knows the games by their Title ID, from a list of 200 games (226 releases) with 16:9 support; before, Auto waited for the game to switch the AV pack to widescreen, which games hardly do, and stayed at 4:3. The log says which Title ID runs, whether it is on the list and what the dashboard is set to (NNshi).
- New option System > Trim HDD on Close (Experimental), off by default: when the game is closed, the hard disk's cache partitions X, Y and Z are emptied (games format them again when they need them) and the image is rewritten without unused space, so `xbox_hdd.qcow2` shrinks. It needs as much free space as the image's data for a moment, and is skipped while the image holds a savestate, which would keep the space and be lost by the rewrite (NNshi, Akrido).

- The source tree holds the core and nothing else: every file no platform's build compiles or includes is gone (about 7,900 files): QEMU's other targets and their machines, user-mode emulation, the standalone's tools (qemu-img, qemu-io, the guest agent and the like), its tests, documentation, scripts, Python and Rust code, translations, firmware blobs and ROM sources, and the unused devices, accelerators and backends. They are listed in `.upstream-excluded`, so a merge of upstream does not bring them back.

- The core info file says the core takes savestates (basic: Run-Ahead and netplay need System > Deterministic Emulation on), so RetroArch offers Save State and Load State for it, and names the core "ParaLLEl Xbox", as libretro-super's copy does.

- OpenGL: the render thread no longer waits on the shader generation worker. Both took one lock around generating GLSL, so a draw that needed a shader generated on the spot, or adopted one the worker made, could wait for the whole of another shader's generation. The generators read only the state they are given, as the Vulkan renderer already relied on, so the lock is gone.

- CPU Boost: the CPU emulation no longer calls a helper to find the carry flag at the start of a block of guest code. When a loop's block begins with INC or DEC (or ADC, SBB, JC) before anything in it has set the flags, it needed the carry flag the previous block left, and found it through a function call each time; now the block is translated knowing which kind of instruction set the flags last and computes the carry inline. A test loop of that kind runs 10% faster, a loop counting in RAM 8% faster; the guest's results are the same. A block that sets the flags itself before it reads them is translated once, however the block before it set them, so the extra translations stay with the blocks that need them: the GPU test ROM translates 419 blocks, against 441 without this and 414 with CPU Boost off.

- The CPU emulation no longer stalls at the start of every block of guest code. Each block takes its instructions off the instruction budget and stores what is left as 16 bits; the next block read it back as 32 bits (with the exit-request half beside it), a load the processor cannot take from the store still on its way to the cache, so it waited for the store each time. Blocks now read the two halves separately. A guest that keeps the CPU busy runs 36% faster (16.5 s to 10.5 s for 600 frames of the test ROM), one that spins the way the Xbox kernel's idle thread does about 30% faster; the guest's results are the same.

- The CPU emulation no longer clears its 1 MB block lookup cache when the guest flushes a TLB that holds nothing. The GPU test ROM does that about once a frame, and 95% of the flushes found nothing to drop.

- Audio: the voice processor no longer fills in the standalone's audio debug window (voice by voice, 1,500 times a second, then copied whole), which the core never shows. Its voice mute and solo controls, reachable only from that window, are gone with it.

- Audio: the voice processor works in integers from the decoded source to the DSPs, as the hardware does, instead of converting every sample to float and back to 24 bits at the mix bins. Each stage keeps the integer steps measured on hardware (the resampler's weights in 1/65536, the filters' 1/16-sample grid, gains in 1/16384), with nothing rounded that the hardware does not round, and the mix bins add up exactly, so the sound is the same whatever the number of Voice Processor Threads. The test ROMs' sound is bit for bit what it was; filtered voices differ from before by at most 48 steps of a 24-bit sample, where float rounding used to flip a filter step, and a filter driven unstable now clips instead of going silent. Savestates from earlier versions still load.

- Audio: an ADPCM block with a header the decoder rejects plays silence. It used to play whatever was left in memory, which could differ from run to run (and between Run-Ahead's runs of the same frame).

- Audio: the voice processor's control math is integer too: the pitch steps, the filter cutoff curve, the envelopes' decay and release curves, and the LFOs, from exact tables instead of the C library's exp2, exp and floor in double precision. The results are the same as before: checked over every input of the cutoff curve and every envelope count and rate, every steady pitch and 400 million pitch ramps, 1.1 billion values with no difference. Nothing in the voice processor or in its hand-off to the DSPs uses floating point any more.

- The guest's time no longer depends on how busy the host is. When the guest waited and the GPU's thread was busy with work of the core's own (setting itself up at boot, after a reset), the guest's time moved on by the host time that took, so the same frames could hold different amounts of the guest's work from run to run; the test suite's "same work flat out and paced" check failed now and then for it. The guest now waits only for work it gave the GPU.

- Audio: 3D voices with HRTF take less than half the time they did: the convolution reads its history straight instead of through a ring's division per tap, and ramps its taps by an addition a sample. The sound is the same, bit for bit.

- Audio: the emulator thread no longer sleeps on the Voice Processor Threads. Each audio frame (1,500 a second) it handed them shares of the voices and slept until the last was done; now it takes voices from the same list as they do and, when none is left, waits only for voices a thread has already started, a few microseconds, without sleeping. It also fixes a multipass voice that mixes back into the bin it reads: the next multipass voice could run on another thread and read an empty bin, so its sound depended on how the voices were split.

- Audio with APU DSP Emulation on: the emulator thread no longer waits for the DSPs' thread. It waited before every audio frame for the frame before to be done, and whenever the game wrote to a DSP or to a register the DSPs use; now the DSPs may be up to 8 audio frames behind, and the game's writes go to them in order between frames, where they would have landed. It still waits for a game reading a DSP's memory. The sound queue starts with 7 more audio frames of silence to cover the frames in flight (about 5 ms more latency), so every video frame hands over the same samples as before. With the sound engine stopped, its silent frames no longer wait either.

- The emulator thread, which runs QEMU's main loop and the guest's CPU, is registered with QEMU's RCU, as upstream's main and CPU threads are. It never was: QEMU's background thread that frees what a change replaced (the translated code's hash table when it grows, the old memory map after a device moves) did not wait for it, and could free memory the guest's CPU was still reading, a crash at random. Now it waits until the CPU is out of the translated code, and kicks it out of a long run for it.

- The machine runs on RetroArch's own thread, in retro_run, instead of on a thread of its own that retro_run handed each frame to and slept on until it was done. That was two thread wake-ups a frame (about 0.1 ms on the test machine) on the way into the frame and out of it, and a thread less now. Building the machine puts the frontend's OpenGL context back as it found it.

- At the end of each frame RetroArch's thread asks the emulated GPU once to finish the frame's work and draw the display, and waits once. It used to wait for the work, then ask for the redraw and wait again: two handoffs between the threads a frame. On the test ROM's GPU scenario the wait at the frame's end is 10 to 30% shorter with Vulkan; with OpenGL the difference is within noise.

- Audio: the Voice Processor Threads are woken only when an audio frame has work for them: a frame with one voice, or one group of voices that must stay together, runs on the emulator thread alone. Before, every thread was woken 1,500 times a second of game time whatever the frame held: with 4 threads and one voice playing, 35,000 thread switches in 300 video frames of the test ROM, 2,700 now.

- Audio with APU DSP Emulation on: the DSPs' thread is woken for four audio frames at a time, and for the rest at the end of each video frame, instead of for every audio frame (1,500 a second of game time). With the game's CPU busy, as in a game, the test ROM went from 8,100 thread switches in 300 video frames to 2,700, and the time spent in the system from 0.30 to 0.23 s. When the core has to wait for the DSPs, it wakes them first, so nothing waits longer.

- `libretro/tests/dspdiff` compares the DSP JIT's registers, memories, cycle count and loop state with the interpreter's for real: it compared a copy of them that nothing filled in, so only the scratch RAM, the FIFOs and the interrupts were ever checked. The JIT and the interpreter do agree on the test program (checked with the fix).

- Run-Ahead, and loading states, are faster. A state load wrote all of the guest's 64 MB of RAM back, threw away every piece of translated CPU code, and had the GPU renderer copy the whole RAM again, twice (OpenGL and Vulkan alike). Now it writes back only the pages that differ, drops only the translated code from those pages, and the renderer takes RAM again only where its draws read it. On the test ROM Run-Ahead 1 runs at 0.69x real time where it ran at 0.39x on OpenGL, 0.64x where it ran at 0.39x on Vulkan; games keep their translated code across the load as well, which the test ROM barely has.

- Saving and loading states takes less work again (a sixth less CPU time with Run-Ahead 1 on the test ROM): a state no longer ends with a description of every field in it that nothing reads, the guest's RAM goes into it without being checked for empty pages first and without a copy on the way, and the devices' numbers are written and read four or eight bytes at a time instead of one. What is left is copying the 64 MB of RAM into the state and comparing it back on load. States from earlier builds still load.

## Test build 4 - 2026-10-25

- The core info file names the core "Microsoft - Xbox / Sega Chihiro (xemu)" (NNshi).
- Chihiro: the Chihiro options are in a category of their own, Sega Chihiro, the last one (NNshi).
- Chihiro gun games: new option Sega Chihiro > Light Gun Aim, Mouse or Analog Stick. With the mouse it aims, the left button fires, the right one is the gun's button (House of the Dead III's pump reload, Virtua Cop 3's change weapon, Ghost Squad's action) and the middle one Virtua Cop 3's pedal or Ghost Squad's fire rate, as standalone binds them; RetroArch's light gun still works. With the stick the left stick or D-pad aims, R2 fires, L2 is the gun's button, L the pedal or fire rate and R shoots off the screen. New option Sega Chihiro > Crosshair, on by default: each player's aim drawn over the picture, player 1 red and player 2 blue, where standalone shows the mouse cursor; with one mouse only player 1's (NNshi).
- Vulkan: new option Video > Vulkan Fast Paths (All by default, as standalone), to leave out the pipeline libraries or the dynamic states on a GPU that crashes with one of them (Akrido).
- Vulkan on RetroArch's device: the device is made with the extended dynamic states, vertex input dynamic state, graphics pipeline libraries, host query reset and shader demote where the GPU has them, as standalone's own device is. Without them the renderer's fast paths were off and every new state compiled a whole pipeline while drawing: shader stutters of up to two seconds where standalone barely hitched (NNshi).
- Chihiro: another Chihiro game loads without restarting RetroArch: its image goes into the media board's memory, the last game's backup is written to its save file, and the cabinet, board type, card readers and save file follow the new game. Only a game larger than the media board memory of the first one (Gundam after a 512 MB game) still asks for a restart; an Xbox game after a Chihiro one, or the other way round, too (NNshi).
- Chihiro: new option Sega Chihiro > Force Feedback Rumble, off by default as in standalone, or 25-200%: the drive board's force feedback in OutRun 2 and Maximum Tune rumbles port 1's pad, as standalone gives it to a gamepad (NNshi).
- Vulkan: the picture handed to RetroArch is taken once the game has handled the vblank of that run, which is where it points the display at the frame it just finished. Taken right away, it raced the game: about a quarter of the frames came twice and as many were skipped while the game itself ran at 60 (NNshi: OutRun 2 between 30 and 60 fps, 224 new pictures in 300 runs).
- The log's line every 300 runs has a second one: how many frames the game flipped, when it issued each flip after the vblank it waits for, how long it then waited, and how late the vblank came after RetroArch asked for the frame - for telling a slow game from a late vblank (NNshi: OutRun 2 at about 44 fps where standalone holds 60).
- The core is built on Tovarichtch's xemu fork (main and perf-experimental) in place of mainline xemu: Sega Chihiro emulation (an image that is not an Xbox disc boots a Chihiro), and the fork's tuned guest CPU and NV2A renderer. New options: System > CPU Boost (the fork's native SSE and wider jump cache, on by default) and Video > Smooth First Play (the shaders a game is known to use are compiled while it boots, on by default). The fork's GPU boost is always on in the core: the stock renderer it keeps for it being off has none of the core's frontend handover. The guest's vblank still comes with each retro_run, not from the fork's wall-clock timer, so RetroArch keeps pacing the frames; the fork's Real hardware speed is left off for the same reason (NNshi).
- Chihiro: the cabinet controls are on RetroArch's pads and light guns, per game (README, Chihiro controls), and the Chihiro files are read from `system/xemu/Chihiro/` under MAME's names (NNshi). Chihiro games load as `.bin` netboot images. The machine is built once per RetroArch start, so a Chihiro game after an Xbox one (or the other way round) is refused with a message to restart RetroArch. New option Sega Chihiro > Free Play, on by default (NNshi).
- Reduced Vulkan Buffers is gone: the fork's renderer keeps its inline vertex buffer at 16 MB on every card, smaller than the option's smallest; the depth/stencil conversion buffers still follow the resolution scale.

## Test build 3 - 2026-10-12

- The console's files are also found in `system/` itself, where an older core info file had them, and the BIOS also as `Complex_4627.bin`. `system/xemu/` and `Complex_4627v1.03.bin` stay the documented place and name (boardsofcanada).
- Reduced Vulkan Buffers (Video, on by default): the Vulkan renderer's buffer for inline vertex data is a tenth of xemu's size on every graphics card, as it already was on those with less than 4 GB - about 1.2 GB less video memory and as much less system memory (3.16 to 2.01 GB in NNshi's run, frame times unchanged). Off reserves xemu's size.
- Linux: the core no longer links libX11, so it loads where X11 is absent. The one Xlib function the OpenGL renderer's GLX path called is found at run time, in the frontend's own libX11.
- Every 300 runs the core logs where a run's time went on the shared Vulkan device: how often it ran, how many of the pictures were another frame of the game's, and the time spent waiting for the renderer and in RetroArch's video and audio callbacks.
- A recipe for the libretro buildbot (`.gitlab-ci.yml`): Linux x64, Linux aarch64 and Windows x64, built by the scripts in `.ci/`; the Linux core there has libslirp linked in, as the buildbot ships one file. QEMU's own GitLab CI files are removed. A GitHub workflow replays the jobs in the same images.
- Vulkan on RetroArch's own device: the renderer draws on the device RetroArch makes with the core and each frame is handed over as an image, with no copy between two devices. The core's and RetroArch's frame times are no longer disconnected, and scrolling that was choppy on Vulkan is smooth (tested by NNshi).
- Graphics API option (Video): Vulkan by default, OpenGL as the fallback. Android always uses Vulkan, macOS always OpenGL.
- Aspect Ratio option (Video): Auto, 4:3 or 16:9. Auto follows the console's aspect line, as standalone does; 16:9 is for a game that renders widescreen but is shown squashed.
- Audio paces the core: RetroArch gets one frame of audio per run, and the sound chip waits once about 64 ms is queued.
- Rumble through RetroArch's rumble interface.
- Internal Resolution Scale works on OpenGL: the frame is handed over at the size it was rendered at. The largest picture RetroArch is told to expect follows the scale, so its shader passes are not sized for a resolution that is not in use.
- Closing content stops the emulated console and flushes its disk. Before, the console ran on and was cut off mid-write when RetroArch exited, which could break the hard disk image.
- Another game can be loaded without restarting RetroArch, on Vulkan: the emulated console stays built when content is closed, and the next game puts its disc in the drive, resets the console and starts it again (six games in a row without a crash, tested by NNshi). On OpenGL a second game is still refused with a message. Core options that need a restart, such as the memory size and Graphics API, keep the first game's values until RetroArch is restarted.
- Start Core without content boots the dashboard with an empty tray.
- The core's version names upstream's version and commit and this repository's.
- Vulkan starts on Linux and on GPUs with little memory: the surfaceless EGL display is made in a way libepoxy accepts, and the compute and vertex buffers are sized for the surface scale and the memory there is.
- CI uploads the Linux core unstripped as its own artifact, for profiling and backtraces.
- The core option texts can be translated through libretro's Crowdin project: the options header is on the standard template, with the `intl/` scripts and the upload and sync workflows. All texts are English until the first sync.
- Closing content or RetroArch no longer crashes. The core stays loaded once it has been loaded: QEMU's RCU, vCPU and nv2a threads, which upstream never joins, and its exit handlers ran on code that was no longer there once RetroArch unloaded the core.
- Redump ISOs load. A whole-disc image is attached from its game partition's offset (XGD1, XGD2 or XGD3), found by the XDVDFS volume descriptor; before, the guest was shown the disc's video partition.
- Disc images through the frontend's VFS: a `saf://` or `content://` path, as Android's Play Store RetroArch hands over, is read through RetroArch's VFS, Redump offset included.
- The core logs at start which commit of this repository it was built from.
- The core info file, matching the core, is in every build's zip.

## Test build 2 - 2026-10-07

- Merged up to upstream xemu 0.8.136-53 (478b4f4961), with its changes since February, among them the new DSP with a JIT for the audio processor; on Android, which upstream's prebuilt DSP library does not cover, the core uses the C interpreter. The core reports upstream's version.
- macOS cores, Apple Silicon and Intel. The renderer there is OpenGL, through contexts shared with RetroArch's: MoltenVK has no geometry shaders, which xemu's Vulkan renderer needs.
- The repository holds the core only: the standalone app's UI, packaging and CI are removed (`.upstream-excluded`), and the core is built without SDL. The Xbox Live Communicator headset is not emulated, as it needs SDL audio.
- Linux: the cores load again. The bundled libpcap needed RDMA and D-Bus libraries most systems lack; the core's networking is NAT or off, so the build is without libpcap and carries only libslirp.
- README for the core.

## Test build 1 - 2026-10-05

- The first builds of the core from CI: Windows x86_64, Linux x86_64 and aarch64 (glibc, with libslirp next to the core), Android arm64-v8a and x86_64 (Vulkan only, with build fixes from X-OG Mobile).
- The core needs `mcpx_1.0.bin`, `Complex_4627v1.03.bin` and `xbox_hdd.qcow2` in `system/xemu/`, and stops with a message naming the file that is missing.

## February 2026

- libretro support, by danprice142: the original port this fork builds on.
