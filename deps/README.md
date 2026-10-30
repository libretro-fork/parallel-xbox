# Third-party code the core compiles

Each folder is the part of the library the core builds, with its license.
The Windows-only ones come from the old core (LibretroAdmin/xemu-libretro,
libretro/deps/src), which built them the same way.

| Folder | What | Version | License | Built on |
|---|---|---|---|---|
| `volk` | Vulkan's function loader | 3ca312a | MIT | all |
| `VulkanMemoryAllocator` | Vulkan memory allocation (header) | 3aa9212 | MIT | all |
| `SPIRV-Reflect` | Shader reflection for the Vulkan renderer | 9137a7e | Apache-2.0 | all |
| `nv2a_vsh_cpu` | The NV2A's vertex shader on the CPU | 561fe80 | Unlicense (public domain) | all |
| `libslirp` | The NAT network backend, on the core's GLib (`libretro/glib`) | 4.9.1 | BSD-3-Clause | all |
| `libepoxy` | OpenGL function loading (WGL) | as in the old core | MIT | Windows |
| `vulkan-headers` | Vulkan's headers | 1.4.350 | Apache-2.0 OR MIT | Windows |

On Windows the Makefile builds the libraries marked Windows from here
(`Makefile.deps`); on Linux and macOS it takes libepoxy from the system
through pkg-config. libslirp is built from here everywhere. libepoxy's
configuration header (`libepoxy/src/config.h`) is written for Windows.
`libslirp/include` holds slirp's two public headers, so that its private
ones (a `debug.h`, for one) are not on the core's include path.

`dsp56300/` is not here until the first build: the DSP JIT's prebuilt
library, which the Makefile fetches (`HAVE_DSP_JIT`).
