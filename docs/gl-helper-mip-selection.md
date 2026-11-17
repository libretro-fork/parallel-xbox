# OpenGL helper lanes at emulated depth-clip boundaries

Ordinary GL fragments now use the same delayed-discard policy as the prepared
fragment path. Texture sampling completes before depth, clip, alpha and texture
kills are committed. Covered fragments still get the original final rejection;
only helper lanes remain available for derivatives. Vulkan retains its explicit
helper-key policy, and the direct SPIR-V generator is unchanged.

The GL binary generator revision changes from `ordered-light-range-2` to
`gl-helper-liveness-1`: old native binaries must miss even though key sizes and
layouts are unchanged. No new cache owner, queue, allocation or option is added.

A repeated NGB fresh-process single-restore control exposed one changed pixel.
Private draw captures reproduced both complete images. The relevant geometry
was byte-identical (484 primitives), fragment uniforms were byte-identical, and
all four quad depth values matched. One uncovered helper exceeded the emulated
far limit while the covered fragment remained valid. Early discard produced
undefined derivatives in ordinary GL. On NVIDIA the old shader sampled mip 0;
the delayed-discard shader, prepared fragment and independent texture-only
oracle all sampled mip 8. llvmpipe already preserved the helpers in the old path.
This is a correction to ordinary GL sampling, not permission to ignore an image
difference during prepared-bank qualification.

The asset-free regression uses captured geometry constants with a procedural
256x256 color-coded mip texture. A CPU plane/gradient proof establishes covered
mask 4, rejected-helper mask 2 and mip 8 independently of the GPU generator.
It checks ordinary default/keyed fragments, prepared fragments and an independent
texture-only oracle, plus covered-fragment rejection: 28 exact scalar checks.
Four deliberate geometry, clipping, mip and texture-mode faults must fail
numerically. All 1,308 existing fragment states also produce identical ordinary
GL source with the explicit helper bit cleared or set. No original corpus or
negative control is removed. The broad fragment helper fault now mutates the
actual default emitter policy, since clearing the old optional bit no longer
turns off the fix.

`test-gl-helper-policy.py` checks 4,096 backend/key/ready cases, actual cache
header parsing (59 cases), source-state immutability and six deliberate faults
under UBSan. The source-owner suite retains all 73,728 historical byte comparisons and five
ownership fault controls under the original private emitter policy. A second
full 73,728-source pass exercises the actual new default and the same five
faults, checking explicit-helper GL equivalence and unchanged historical Vulkan
bytes. Historical leak reclamation remains confined to the historical owner;
candidate temporary owners must reach zero in both passes. The explicit-helper
reference adds test-only owners after the production allocation counters are
sampled; its allocations are not runtime work or an optimization claim. Native
Windows and Linux ASan/UBSan run both complete modes and their fault controls.

The CPU contract and software GPU oracle run in shader CI; the
current driver replays historical subject source rather than requiring new
fixtures to exist in old revisions. Historical default sampling is reported as
legacy, never as evidence that it has the new fix.

Native NVIDIA 621.01 (RTX 5090) and llvmpipe LLVM 20.1.2 / Mesa 25.2.8 pass the
procedural oracle and its faults. The full 1,308-case native/software fragment comparison and all original
negative controls also pass. Four fresh-process NGB restore captures (two per
build) now have the same full 720x480 image hash after 600 restored callbacks:
`e890a6d3535d977391662281820d78571387215f24c8334d4db8fd27db5dc9ff`.
The ordinary fixed core has both experimental flags off; the comparator is the
previously frozen prepared-bank build. All restores, audio and video cadence
checks pass. Image-capture runs are excluded from latency measurements.
Twelve serial clean DOA3 runs compare the old default and fixed default at 1x:
three app-cache-empty boots and three fresh-process boots using each own warm
persistent cache per build. All 7,200 fight callbacks, audio, presents and guest
progress checks pass, with zero fight callbacks exceeding 16.683 ms. Median
fight p99 is 9.575 / 9.736 ms old/fixed when app-cache-empty (+1.7%); persistent-
warm is 9.585 / 9.570 ms. These differences are within the observed run spread.
The first fixed-build boot has a 223.823 ms maximum callback, retained in the
results; its other two cold boot maxima are 66.935 / 64.177 ms, versus the old
build's 63.885--65.530 ms range. That initial startup spike is not removed as
warmup. Driver cache state is uncontrolled, and boot cameras/workloads differ.
These headless callback measurements do not prove physical driver-cold or
frontend/display latency recovery; the standalone fixture, work counters and
restore images are separate evidence.

The targeted cache-header, cache-file, stage-lifetime, program-namespace, native
fragment-owner and shader-layout contracts pass. Cache-string sanitizers run on
Linux: this Windows GCC lacks libasan/libubsan, so its failed sanitizer link is
retained as unsupported rather than reported as a native pass.

Shader/PSO cold-first-play smoothness remains **BAD and regressed**. Existing
synchronous misses, Vulkan draw-path PSO creation, GL coverage gaps and observed
buffer orphaning remain open. This correctness change does not certify recovery
or allocation-free gameplay.
