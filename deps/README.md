# Third-party code the core compiles

Each folder is the part of the library the core builds, with its license.
The Windows-only ones come from the old core (LibretroAdmin/xemu-libretro,
libretro/deps/src), which built them the same way; glslang is Khronos's
16.6.0 release.

| Folder | What | Version | License | Built on |
|---|---|---|---|---|
| `xxhash` | Hashing (QEMU's utility code) | 0.8.3 | BSD-2-Clause | all |
| `volk` | Vulkan's function loader | 3ca312a | MIT | all |
| `VulkanMemoryAllocator` | Vulkan memory allocation (header) | 3aa9212 | MIT | all |
| `SPIRV-Reflect` | Shader reflection for the Vulkan renderer | 9137a7e | Apache-2.0 | all |
| `nv2a_vsh_cpu` | The NV2A's vertex shader on the CPU | 561fe80 | Unlicense (public domain) | all |
| `glib` | GLib's base library | 2.88.2 | LGPL-2.1+ | Windows |
| `pcre2` | Regular expressions, for GLib (8-bit, no JIT) | 10.47 | BSD-3-Clause | Windows |
| `proxy-libintl` | The gettext API, translating nothing, for GLib | as in the old core | LGPL-2.0+ | Windows |
| `libslirp` | The NAT network backend | 4.9.1 | BSD-3-Clause | Windows |
| `libepoxy` | OpenGL function loading (WGL) | as in the old core | MIT | Windows |
| `libsamplerate` | The APU's voice resampling (the fastest sinc converter) | 0.2.2 | BSD-2-Clause | Windows |
| `zlib` | Compression (qcow2, savestates) | 1.3.1 | Zlib | Windows |
| `vulkan-headers` | Vulkan's headers | 1.4.350 | Apache-2.0 OR MIT | Windows |
| `glslang` | GLSL to SPIR-V for the Vulkan renderer (no HLSL, no SPIRV-Tools) | 16.6.0 | BSD-3-Clause and others (`LICENSE.txt`) | Windows |

On Windows the Makefile builds the libraries marked Windows from here
(`Makefile.deps`); on Linux it takes them from the system through
pkg-config. GLib's, PCRE2's and libepoxy's configuration headers
(`glib/cfg`, `pcre2/src/config.h`, `libepoxy/src/config.h`) are written
for Windows. `glib/gmodule.h` and `glib/gmodule/gmodule-visibility.h` are
GLib's, the second as its generator writes it: QEMU includes `gmodule.h`
without loading modules. `libslirp/include` holds slirp's two public
headers, so that its private ones (a `debug.h`, for one) are not on the
core's include path.

`dsp56300/` is not here until the first build: the DSP JIT's prebuilt
library, which the Makefile fetches (`HAVE_DSP_JIT`).
