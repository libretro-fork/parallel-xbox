# Bump filtering observation probe

This opt-in diagnostic is not a renderer fix or a hardware oracle. It uses the
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
commit, renderer, driver and warnings. This probe is not included in `run.sh`:
hardware results have not been established, and signed-channel cases exercise
unsupported state.

192 cases cover linear/swizzled G8B8, BUMPENVMAP/BUMPENVMAP_LUM, B/G displacement,
signed flags none/B/G/BG, point/linear sampling, and left/midpoint/right texel
coordinates. The bump texture contains bytes 255 and 1 along one axis and zero
along the other; both rows are identical and only one mip level exists. Stage 1
reads an RGB ramp (R reports U, G reports V), with a diagonal bump matrix of 8.
Luminance scale 0 and offset 1 isolate displacement. A GPU semaphore completes
each draw before CPU readback. Each case has a unique bump texture address to
keep texture rewrite/invalidation behavior out of the observation.

Signed-before-filter predicts cancellation at the linear midpoint; the current
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
characterize current emulation; they do not establish the correct console output.
