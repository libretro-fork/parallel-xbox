# The core's tests

`rom/rep.asm` checks REP MOVS in a bare-metal guest with CPU Boost on and
off. It covers byte/word/dword copies, direction and overlap, zero counts,
16-bit address/count handling and wrap, TF single-stepping, RF and execution
breakpoints, data watchpoints, a pending interrupt after STI, page-fault
restart, self-modifying code and a REP opcode spanning two code pages.
`run.sh` checks its completion marker, trap counts and remaining counts.

`python libretro/tests/test-io-recompile.py . gcc` executes production
I/O learning and block invalidation against controlled cache/page/jump
ownership. It prevents an unchanged old I/O block from being resurrected,
while checking ordinary block reuse, PC-relative hashes, page/jump cleanup,
duplicate invalidation and one-shot blocks in 513 cases with UB traps.

`python libretro/tests/test-uniform-stage-uploads.py . gcc` exercises the
production Vulkan uniform tracker, upload planning and descriptor-reuse
decision against bounded upload storage. It checks independent stages,
module switches, alignment, resets and descriptor exhaustion in 100,000
mixed draws, with GCC's undefined-behavior traps enabled.

`python libretro/tests/test-tlb-dirty.py . gcc` checks the production TLB
dirty-range functions against a full-scan reference in 1,655,360 cases:
fast/slow flags, aliases, wrapped/empty ranges, active MMU modes and victim
entries. Another 10,000 states compare first-write dirty promotion against
an exhaustive scan, including primary/victim aliases and preservation of
other page flags. GCC's undefined-behavior traps are enabled.

`python libretro/tests/test-dma-map.py . gcc` compiles the production direct
DMA mapping function against bounded RAM and a pending GPU writeback. It
checks callback ordering for reads/writes, preservation of newly written
DMA data, translated and shortened mapping ranges, failed/empty maps and
the separate bounce path. GCC's undefined-behavior traps are enabled.

`python libretro/tests/test-voice-tail.py . gcc` runs the decoder-to-mix
regression without booting a complete core. It compiles the checkout's
actual PCM/ADPCM decoder, resampler, envelope/filter/routing functions and
voice-list active gate against bounded guest RAM and notification writes.
Worker execution and platform/debug glue are fixtures. The 944 finite-source
cases cover source lengths around frame/block boundaries, slow/fast pitch,
mono/stereo and DSP-off output; additional cases cover loops, SSL bank
completion, persistent underrun/refill, explicit stop, pause, restart and
VP reset/pre-load. Another 24,576 ADPCM cases cover every page offset for
mono/stereo buffers and streams, discontiguous SGE pages, address-space fallback, exact decoded
samples and the number of SGE translations per block. Pitch cases check
steady and changing fractional/extreme steps, reset and multipass-to-source
cache invalidation. Set `TAIL_TEST_CFLAGS` to
`-fsanitize=undefined -fsanitize-undefined-trap-on-error` for trapping under
the core's `-fwrapv` semantics. Python and GCC are required; no Xbox files
are needed. These are source-level tests, not a hardware timing comparison.

`run.sh <core.so>` runs the core in a headless frontend of its own
(`harness.c`: OpenGL core on a surfaceless EGL display, Mesa's llvmpipe
will do; with `--vulkan` a Vulkan context as RetroArch's vulkan driver
gives one, Mesa's lavapipe will do) with test ROMs in place of the Xbox's
boot ROM and BIOS, so no Microsoft file is needed. CI runs it after every
Linux build.

- `rom/bootrom.asm`: 512 bytes where the MCPX boot ROM goes; enters
  protected mode and jumps to the flash.
- `rom/flash.asm`: the flash. It starts one looping voice on the APU's voice
  processor, a 1 kHz sine at 48 kHz and 0 dB (`sine.inc`, 40 periods), and
  halts; built with `-DBUSY` it counts in RAM at 0x300000 instead, so the
  CPU never rests; built with `-DDSP` it also loads `gp.a56` into the APU's
  GP DSP and starts it (`-DGP_PASSES=n` sets its load, 1 by default);
  built with `-DSPIN` it spins on a word in RAM that nothing writes; built
  with `-DPAD` it runs `pad.c`; built with `-DGPU` it runs `gpu.c`; built
  with `-DDVD` it runs `dvd.c`.
- `rom/pad.c`: the pad through the USB controller, as the kernel reads it:
  the two hubs found by their status-change reports and configured, their
  ports reset, the pad's reports taken over its interrupt endpoint on the
  periodic list and copied to RAM. Freestanding 32-bit C; `pad.sh` builds
  it into `pad.inc` (committed, so the tests need no 32-bit compiler).
- `rom/gpu.c`: the NV2A from bare metal: the AGP bridge's windows and the
  GPU's BARs, a 640x480 32-bit display, one PFIFO channel in DMA mode with
  its objects in RAMIN, and a scenario the harness reads the picture of
  (`--pixel`): a surface cleared red and shown, left alone while another is
  drawn and flips are counted, read back by the CPU (the renderer
  downloads it for that), then written green by the CPU. `gpu.sh`
  builds it into `gpu.inc` (committed, like `pad.inc`).
- `rom/dvd.c`: DVD reads as the kernel sends them: READ(10) packets to the
  primary IDE channel's slave, by bus-master DMA through the PIIX3's PRD
  tables (one entry, three scattered, ten for 600 KB), every word read
  checked. `dvd.sh` builds it into `dvd.inc` (committed, like `pad.inc`);
  `mkdisc.c` makes its disc, whose every 32-bit word says which sector and
  word it is.
- `rom/gp.a56`: the GP DSP's program. Every 32-sample frame it rebuilds
  mixbins 0 and 1 from 64 exact products a sample, n times over, so the tone
  comes out of the DSP unchanged after n x 4096 MACs. `gp.inc` is it
  assembled with a56 (a DSP56001 assembler; the 56300 runs that code), so
  the tests do not need a56.
- `sinecheck.c`: checks the audio the harness wrote: the tone unbroken, its
  level, the number of samples.
- `dspdiff.c`: the DSP JIT against the interpreter, built from the core's
  DSP sources alone (`make dspdiff`, for another host `make dspdiff
  CC=aarch64-linux-gnu-gcc`): a DSP program (`gp.inc`, or a dump of a
  game's DSP: `DSPD`, then P, X and Y as 32-bit words) run frame by frame
  on both, from the same state, with the same random memory around them,
  the whole DSP state compared after every frame.

What `run.sh` checks: every `retro_run` hands over one video frame and
exactly one frame's worth of audio (800 or 801 samples at 60000/1001 fps),
idle and busy; the tone has no gap; an idle guest runs far faster than real
time when the frontend does not pace it (fast-forward), and so does one
that spins waiting; the busy guest does the same work paced and unpaced;
the pad's buttons reach the guest over USB; DVD reads by DMA land in the
guest's memory byte for byte; the idle guest runs the same
on a Vulkan context, where there is a Vulkan device, through version 2 of
the context negotiation interface and as a frontend that knows only
version 1; with APU DSP
Emulation on, the JIT leaves the DSP in the same state as the interpreter
after every frame of the GP program, the tone comes through the GP DSP
whole on the interpreter and on the JIT, the interpreter does not fall behind on a heavy program, and
the JIT's state survives a savestate; savestates, loads and resets between
frames, and right after a load the guest's RAM is byte for byte what it was
when the state was saved, in the same process and in another one.

The harness's options are listed at the top of `harness.c`.
