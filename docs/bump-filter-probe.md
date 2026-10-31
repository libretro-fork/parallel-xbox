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
Filtering off and on (768 cases per renderer). The probe wraps its bump/data
texture so it exercises the anisotropy eligibility rule. G8B8 data keeps
the game's sampling footprint even when quality filtering is requested; forcing
AF on its unsigned backing sampler can corrupt dependent and color results.

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
border rules, anisotropy, other formats and ordinary-combiner use are not covered.

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
