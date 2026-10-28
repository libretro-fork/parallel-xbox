# The core's tests

`run.sh <core.so>` runs the core in a headless frontend of its own
(`harness.c`: OpenGL core on a surfaceless EGL display, Mesa's llvmpipe
will do) with test ROMs in place of the Xbox's boot ROM and BIOS, so no
Microsoft file is needed. CI runs it after every Linux build.

- `rom/bootrom.asm`: 512 bytes where the MCPX boot ROM goes; enters
  protected mode and jumps to the flash.
- `rom/flash.asm`: the flash. It starts one looping voice on the APU's voice
  processor, a 1 kHz sine at 48 kHz and 0 dB (`sine.inc`, 40 periods), and
  halts; built with `-DBUSY` it counts in RAM at 0x300000 instead, so the
  CPU never rests; built with `-DDSP` it also loads `gp.a56` into the APU's
  GP DSP and starts it (`-DGP_PASSES=n` sets its load, 1 by default);
  built with `-DSPIN` it spins on a word in RAM that nothing writes; built
  with `-DPAD` it runs `pad.c`.
- `rom/pad.c`: the pad through the USB controller, as the kernel reads it:
  the two hubs found by their status-change reports and configured, their
  ports reset, the pad's reports taken over its interrupt endpoint on the
  periodic list and copied to RAM. Freestanding 32-bit C; `pad.sh` builds
  it into `pad.inc` (committed, so the tests need no 32-bit compiler).
- `rom/gp.a56`: the GP DSP's program. Every 32-sample frame it rebuilds
  mixbins 0 and 1 from 64 exact products a sample, n times over, so the tone
  comes out of the DSP unchanged after n x 4096 MACs. `gp.inc` is it
  assembled with a56 (a DSP56001 assembler; the 56300 runs that code), so
  the tests do not need a56.
- `sinecheck.c`: checks the audio the harness wrote: the tone unbroken, its
  level, the number of samples.

What `run.sh` checks: every `retro_run` hands over one video frame and
exactly one frame's worth of audio (800 or 801 samples at 60000/1001 fps),
idle and busy; the tone has no gap; an idle guest runs far faster than real
time when the frontend does not pace it (fast-forward), and so does one
that spins waiting; the busy guest does the same work paced and unpaced;
the pad's buttons reach the guest over USB; with APU DSP
Emulation on, the tone comes through the GP DSP whole on the interpreter and
on the JIT, the interpreter does not fall behind on a heavy program, and
the JIT's state survives a savestate; savestates, loads and resets between
frames, and right after a load the guest's RAM is byte for byte what it was
when the state was saved, in the same process and in another one.

The harness's options are listed at the top of `harness.c`.
