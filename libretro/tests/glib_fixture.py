# SPDX-License-Identifier: GPL-2.0-or-later
"""Link fixtures to the core's compatibility API, never system GLib."""
import subprocess


def compat_glib_flags(repo, compiler, main_context=False):
    sources = ["libretro/glib/gcore.c", "libretro/glib/gcontainers.c"]
    if main_context:
        sources += ["libretro/glib/gmain.c", "libretro-common/time/rtime.c",
                    "libretro-common/features/features_cpu.c",
                    "libretro-common/compat/compat_strl.c"]
    # PE unwind tables otherwise retain unrelated functions and providers.
    # These standalone C fixtures do not throw or unwind Windows exceptions.
    target = subprocess.check_output([compiler, "-dumpmachine"], text=True).lower()
    unwind = ["-fno-asynchronous-unwind-tables"] if "mingw" in target or "-windows-" in target else []
    collect = "-Wl,-dead_strip" if "darwin" in target else "-Wl,--gc-sections"
    return ["-I" + str(repo / "libretro/glib"),
            "-I" + str(repo / "libretro-common/include"),
            "-ffunction-sections", "-fdata-sections", collect,
            "-Wno-unused-parameter", "-pthread", *unwind,
            *[str(repo / name) for name in sources]]
