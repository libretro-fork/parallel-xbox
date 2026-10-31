# Bump filtering observation probe

This isolated test is not a hardware oracle. It uses the
synthetic flash test environment. **Never flash these ROMs onto a physical Xbox.**
They are emulator fixtures, not a console application.

Build on Linux with GCC/binutils and NASM (or append `--assembler yasm`):

```
python3 libretro/tests/build-bump-probe.py /tmp/bump-probe
```

The output directory must be new or empty. The builder leaves the committed
`gpu.inc` unchanged. With a built Linux headless test harness, run both renderers
in separate processes, using the same core commit and default texture options:

```
mapfile -t peeks < /tmp/bump-probe/peek-args.txt
timeout 120 ./harness /absolute/path/parallel_xbox_libretro.so \
  /tmp/bump-probe/system --frames 240 --strict "${peeks[@]}" \
  > /tmp/bump-probe/gl.log 2>&1
timeout 120 ./harness /absolute/path/parallel_xbox_libretro.so \
  /tmp/bump-probe/system --vulkan --opt xemu_renderer=vulkan \
  --frames 240 --strict "${peeks[@]}" > /tmp/bump-probe/vk.log 2>&1
```

Check completion at `0x00300100`: it must be 192. Incomplete runs are failures,
not sample results. `cases.csv` describes each pixel's RAM address. Record core
commit, renderer, driver and warnings. `run.sh` runs the scalar-model regression
on Linux where Vulkan is available. The implemented model does not claim to
establish every NV2A rounding rule or register combination.

For an automated run of both the bump and color matrices:

```
python3 libretro/tests/test-signed-g8b8.py . /absolute/path/parallel_xbox_libretro.so \
  /absolute/path/harness /tmp/g8b8-regression
```

The color matrix adds 192 cases covering all 16 combinations of channel signs,
point/linear sampling, both layouts and three coordinates. It reads T0 directly
through the final combiner. B/R use bytes 255 and 1; G/A use 64 and 192. Thus
unsigned and signed channels can be checked independently even though G8B8
duplicates the underlying bytes. `check-bump-probe.py` requires complete logs,
compares each pixel with an independent scalar model within one color LSB, and
requires exact agreement between GL and Vulkan. No test logging is compiled
into normal emulator execution.

The automated run repeats both matrices with Force LOD0 and Force Quality
Filtering off and on. The probe wraps its bump/data
texture so it exercises the anisotropy eligibility rule. G8B8 data keeps
the game's sampling footprint even when quality filtering is requested; forcing
AF on its unsigned backing sampler can corrupt dependent and color results.

## Mixed LOD0 filters

`build-bump-probe.py OUTPUT --mixed [--lod-bias -4|0|4]` builds a direct-color
matrix with point-min/linear-mag and linear-min/point-mag filtering. An inline
texture-coordinate stream produces footprints of 1/8, 1/2 and 4 texels per
screen pixel. The sampled pixel stays at u=1.25 texels; slopes and sample
positions are binary-exact. The cases avoid the precise LOD transition, whose
console rounding has not been established. B/R contain bytes 192 and 64;
G/A contain 64 and 192, so point and linear sampling give distinct signed results.

The complete regression repeats all three mixed matrices at biases -4, 0 and
+4, plus the original bump/color matrices, with quality overrides off and on:
1920 cases per renderer. `textureQueryLod(...).y` selects reconstruction from
the sampler's computed LOD, including its bias. Matching-filter shaders do not
emit the query. Mixed mipmapped pairs use the mip reconstruction below.

For the single-level Vulkan G8B8 LOD0 sampler, maxLod=0.25 permits a positive
LOD to select minification while nearest mip selection still uses level zero.
maxLod=0 would always select magnification, making the two filters ineffective.
See the [Khronos sampler mapping](https://docs.vulkan.org/spec/latest/chapters/samplers.html)
and [GLSL LOD query](https://github.khronos.org/Vulkan-Site/glsl/latest/chapters/builtinfunctions.html#texture-query-lod).

## Mipmapped signed G8B8

Swizzled, projective 2D G8B8 with wrap U/V and a zero lower mip clamp now decodes
texels before filtering with matching or mixed point/linear min/mag reconstruction
within and between mip levels. All four nearest/linear mip modes are supported.
The accessed-mip component of `textureQueryLod` supplies sampler bias, nearest
mip selection and the upper clamp. Explicit `texelFetch` reads wrapped integer
coordinates, so the backing unsigned sampler cannot interpolate encoded bytes.
Matching point min/mag with nearest mip selection retains the hardware sample.
Shader dirty-state tracking covers channel signs, mag filtering, mip count/size,
and address changes for signed mipmapped G8B8, so cached shaders can switch paths.
Force LOD0 emits a zero level directly: Mesa Vulkan can otherwise report a
nonzero accessed mip despite its texture sample honoring a zero maximum.

`build-bump-probe.py OUTPUT --mip-filter 3|4|5|6 [--lod-bias -4|0|0.25|4]`
builds a 4x4/2x2/1x1 mip chain with distinct byte patterns. Its 192 cases combine
16 channel-sign masks, three exact footprints (1/4, 1 and 2 texels/pixel), upper
clamps 1/2 and positive/negative wrap periods. Fractional bias 0.25 exercises
interpolation between levels without using an approximate fractional footprint.
The full runner checks 14208 cases per renderer with both quality overrides off
and on. Original cases retain a one-LSB scalar tolerance and exact GL/Vulkan
agreement. Mip cases allow one LSB, or two for cross-mip interpolation, in the
scalar and backend comparison because native LOD approximation can shift a
high-contrast signed color. This is not a physical-console precision oracle.

Nonzero lower clamps, other address
modes, cubemaps, embedded borders, color keys and signed alpha-kill remain on
their previous paths. Linear-layout textures do not support mip chains.

Add `--mip-mixed` to the mip fixture command to choose the opposite magnification
filter. The 6144 extra cases cover both directions for every mip mode and bias,
including a zero computed LOD and a fractional positive LOD. A single LOD query
selects point/bilinear reconstruction from its computed component and mip levels
from its accessed component; bias is applied once. Only the bilinear branch
fetches the additional three taps. Matching-filter shader text is unchanged.

For base-clamped mixed Vulkan G8B8 samplers, maxLod=0.25 and nearest mip selection
retain minification without accessing mip one. This applies to the unsigned
channels too. A zero ceiling would force magnification. The shader independently
forces the accessed mip to zero while retaining the query for filter selection.
The stage mask reuses the never-set universal byte, preserving shader seed layout
and existing matching-filter keys. The layout test checks size and field offsets.

The selection threshold is zero on both tested renderers, consistent with the
[OpenGL 4.6 specification, section 8.14.1](https://registry.khronos.org/OpenGL/specs/gl/glspec46.core.pdf)
and the [Vulkan sampling rules](https://docs.vulkan.org/spec/latest/chapters/textures.html).
Historical GL rules are not used as an Xbox oracle. Physical NV2A transition and
rounding behavior still requires console measurements.

192 cases cover linear/swizzled G8B8, BUMPENVMAP/BUMPENVMAP_LUM, B/G displacement,
signed flags none/B/G/BG, point/linear sampling, and left/midpoint/right texel
coordinates. The bump texture contains bytes 255 and 1 along one axis and zero
along the other; both rows are identical and only one mip level exists. Stage 1
reads an RGB ramp (R reports U, G reports V), with a diagonal bump matrix of 8.
Luminance scale 0 and offset 1 isolate displacement. A GPU semaphore completes
each draw before CPU readback. Each case has a unique bump texture address to
keep texture rewrite/invalidation behavior out of the observation.

Signed-before-filter predicts cancellation at the linear midpoint; the previous
unsigned-filter-then-`sign3` model maps the average byte 128 to -128/127. These
are competing models, not asserted console results. Point samples and both axes
help expose coordinate/channel errors before interpreting the midpoint. Mips,
border rules, anisotropy and other formats are not covered by this bump matrix;
the color and mip matrices above cover ordinary-combiner sampling separately.

## Hardware comparison still required

NVIDIA's related specification describes signed texture-offset components:
https://registry.khronos.org/OpenGL/extensions/NV/NV_texture_shader.txt
It does not establish every NV2A register combination.

Console-side references are `BumpMapTests`, `BumpEnvLumTests` and
`TextureSignedComponentTests` in https://github.com/abaire/nxdk_pgraph_tests.
Reproduce this midpoint matrix as an nxdk XBE for a physical console; no real
Xbox result is claimed here. Do not globally replace G8B8 with SNORM solely on
the scalar reproduction: ordinary color sampling, per-channel signedness,
luminance and shader-result conversions must remain consistent.

## Observed emulator baseline

On base commit `b1d01ea2db677d87c185a17cb842b344d16d5db4`, with this
diagnostic applied, Mesa software OpenGL and Vulkan both completed all 192
cases and returned identical pixels. With no signed flags, the linear G8B8
B-displacement samples were `ff7c7800` (left), `ff3c7800` (midpoint), and
`ff7d7800` (right). G-displacement samples were `ff7c7700`, `ff7c0000`, and
`ff7c7900`. Both layouts and bump modes reproduced the large midpoint offset.
All four signedness combinations produced the same observations. These results
characterize the old emulation; they do not establish the correct console output.
