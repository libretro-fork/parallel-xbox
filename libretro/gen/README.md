# Pregenerated sources

QEMU's build generates part of the code it compiles: the QAPI types,
visitors, commands and events (from `qapi/*.json`), the trace points (from
the `trace-events` files), the configuration headers, the monitor's command
tables (from the `*.hx` files), the keymaps, `block/block-gen.c` and the
i386 GDB XML. The core's Makefile has no meson, ninja or python, so they
are here, made once by meson and committed: the files the core's objects
compile or include, under the paths meson gives them in its build
directory (the Makefile puts this directory first on the include path).

`config-host.h` chooses between `config-host-linux.h` and
`config-host-win32.h` (the only generated file that differs between the
two; Windows' was made by a MinGW-w64 cross configure, with
`--cross-prefix=x86_64-w64-mingw32- --static` added to the line below).

They were made by a meson build directory configured as the core's:

```
mkdir build-gen && cd build-gen
../configure --extra-cflags="-DXBOX=1 -Wno-error" --target-list=i386-softmmu \
    -Dlibretro=true -Dpipewire=disabled -Db_staticpic=true \
    --enable-trace-backends=nop --disable-libusb
ninja <each file below>
```

The `nop` trace backend: QEMU's trace points compile to nothing (with the
default `log` backend each one tested at run time whether to log).

Then, by hand:

- `i386-softmmu-config-devices.h`, `config-poison.h`: the ivshmem-flat
  device (Linux only, shared memory; no Xbox has one) taken out, so the two
  OSes share them.
- `config-host-*.h`: `CONFIG_DSP_JIT` taken out (the Makefile defines it
  when it links the DSP JIT, `HAVE_DSP_JIT`); `CONFIG_USB_LIBUSB` undefined
  (meson's libusb check ignores `--disable-libusb`; the core has no USB
  passthrough); the host CPU's defines (`HOST_X86_64` and the x86 features)
  moved to the end of the file, chosen by the compiler's own macros, so the
  file serves x86-64 and AArch64 hosts. The rest is what meson found on an
  x86-64 host.
- `xemu-version-macro.h` is not here: the version and commit the core logs
  change with every commit, so the Makefile writes it at each build
  (`libretro/version.sh`, as `scripts/xemu-version.py` does for meson).
- `data/*.h`: the declarations meson's `scripts/pack-file.py` writes next to
  the data files' C arrays; the Makefile assembles the data files
  themselves (`libretro/data.S`) instead of compiling those arrays.

When a change touches what these come from (a `qapi/*.json` schema, a
`trace-events` file, `config_spec.yml`, a `*.hx` file, the device
configuration), make them again the same way and copy the changed ones
here.
