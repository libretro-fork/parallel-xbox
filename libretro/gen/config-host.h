/*
 * QEMU's host configuration, as meson found it for the host OS: Linux and
 * Windows (MinGW-w64) are pregenerated (libretro/gen/README.md)
 */
#if defined(_WIN32)
#include "config-host-win32.h"
#elif defined(__linux__)
#include "config-host-linux.h"
#else
#error "no pregenerated config-host.h for this OS: libretro/gen/README.md"
#endif
