# libretro-common

The parts of [libretro-common](https://github.com/libretro/RetroArch/tree/master/libretro-common)
the core uses, as RetroArch has them (RetroArch commit af62210): the
lock-free threading primitives (`retro_atomic.h`, `rthreads`,
`retro_eventcount`, `retro_spsc`). Each file carries its own license
(MIT). Fixes go to RetroArch's copy first and are brought over from
there.

`memmap/memmap.c` is copied unchanged from the same RetroArch commit
(`af622105435d776c1ce203c28387cbc27e0c7925`) and compiled on macOS for its
`memjit_write_begin/end` implementation. QEMU's absolute write/execute
switches are adapted to those nested scopes in `util/libretro-jit.c`.
