/*
 * QEMU's host configuration, as meson found it for the host OS: Linux and
 * Windows (MinGW-w64) are pregenerated, macOS and Android written from
 * meson's probes (libretro/gen/README.md)
 */
#if defined(_WIN32)
#include "config-host-win32.h"
#elif defined(__ANDROID__)
#include "config-host-android.h"
#elif defined(__linux__)
#include "config-host-linux.h"
#elif defined(__APPLE__)
#include "config-host-darwin.h"
#else
#error "no pregenerated config-host.h for this OS: libretro/gen/README.md"
#endif
