# libretro-common

The parts of [libretro-common](https://github.com/libretro/RetroArch/tree/master/libretro-common)
the core uses, as RetroArch has them (RetroArch commit af62210): the
lock-free threading primitives (`retro_atomic.h`, `rthreads`,
`retro_eventcount`, `retro_spsc`). Each file carries its own license
(MIT). Fixes go to RetroArch's copy first and are brought over from
there.
