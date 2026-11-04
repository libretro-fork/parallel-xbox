# ParaLLEl Xbox

ParaLLEl Xbox is a [libretro](https://www.libretro.com/) core of [xemu](https://xemu.app), the original Xbox emulator, for RetroArch and other libretro frontends. The core is `parallel_xbox_libretro`.

The emulator is upstream's: this repository follows [xemu-project/xemu](https://github.com/xemu-project/xemu) and adds the libretro frontend in `ui/libretro.c`. It holds the core only; upstream's standalone UI, packaging and CI are not part of it (the paths are listed in `.upstream-excluded`). The version the core reports is upstream's, with the upstream commit it is merged to (`upstream.version`). The core is currently on xemu 0.8.136-53, commit 478b4f4961 of 28 September 2026.

## Downloads

The core is built by libretro's buildbot for Windows and Linux (x86_64, aarch64), and is installed from RetroArch: Online Updater > Core Downloader. Every push to the `libretro` branch is also built in [Actions](https://github.com/WizzardSK/xemu-libretro/actions/workflows/libretro.yml), macOS (Apple Silicon, Intel) and Android (arm64-v8a, x86_64) included; the builds are the artifacts of a run, and downloading them needs a GitHub login.

- **Linux:** `parallel_xbox_libretro.so`; put it into RetroArch's `cores` folder. A glibc build, it does not load into a musl frontend.
- **macOS:** `parallel_xbox_libretro.dylib` and the folder `parallel_xbox_libretro_libs`; put both into RetroArch's `cores` folder.
- **Windows:** `parallel_xbox_libretro.dll`.
- **Android:** `parallel_xbox_libretro_android.so`, Android 10 or newer.

## Setup

Put these files into RetroArch's system directory, under `system/xemu/`:

| File | What it is |
|---|---|
| `mcpx_1.0.bin` | optional: the MCPX boot ROM, 512 bytes, MD5 `d49c52a4102f6df7bcf8d0617ac475ed`. When absent, an experimental MCPX 1.0 HLE boot path runs instead |
| `Complex_4627v1.03.bin` | the flash BIOS, a 1 MB or 256 KB image |
| `xbox_hdd.img` or `xbox_hdd.qcow2` | optional: the hard disk, a raw image or a qcow2 one. A qcow2 is read and written as it is, so standalone xemu and the core can share one. Without either, the core writes the ready-made image it carries, [xemu-dashboard](https://github.com/xemu-project/xemu-dashboard)'s (MIT, an unsigned dashboard and no Microsoft software), the same one xemu offers: as a raw `xbox_hdd.img` (8 GB, of which only the data takes space), or as `xbox_hdd.qcow2` on FAT32 and exFAT, which have no sparse files |

The boot ROM and the BIOS are Microsoft's and have to be dumped from your own console; see xemu's [required files](https://xemu.app/docs/required-files/). Other names and locations can be set in the core options. The EEPROM (`xbox_eeprom.bin`) is created in the same folder when it is missing.

The HLE fallback independently implements the boot ROM's documented Xcode and RC4 behavior; it contains no Microsoft ROM instructions. It targets MCPX 1.0 BIOSes such as the configured Complex 4627 image. It does not replace the flash BIOS or implement MCPX 1.1/FBL boot, and is not instruction-, timing-, or register-exact. A supplied boot ROM keeps the existing emulated-ROM path; a malformed or unreadable supplied file is reported as an error. See [implementation and testing notes](docs/mcpx-hle.md).

For Sega Chihiro games, these files go under `system/xemu/Chihiro/` (MAME's names; the Chihiro BIOS includes its boot code, and the hard disk is written as for an Xbox):

| File | What it is |
|---|---|
| `chihiro_xbox_bios.bin` | the Chihiro BIOS, 512 KB |
| `fpr21042_m29w160et.bin` | the media board's flash, 2 MB |
| `ic10_g24lc64.bin`, `pc20_g24lc64.bin`, `ic11_24lc024.bin` | the EEPROMs of the I/O boards and the baseboard |
| `ver1305.bin` | optional: the network board's firmware, for linked cabinets |

## Content

Xbox games as `.iso` or `.xiso` (xemu's XISO format, as made by extract-xiso). Sega Chihiro games as `.bin` netboot images, as made by [Chihiro-netboot](https://github.com/Tovarichtch/chihiro-netboot): an image that is not an Xbox disc boots a Chihiro. The machine is built once per RetroArch start: to play a Chihiro game after an Xbox one, or the other way round, restart RetroArch. Another Chihiro game loads into the same machine, unless it is larger than the media board memory the first one was given (Gundam needs 1024 MB, the others fit 512 MB): then restart RetroArch. Chihiro games start in free play (Sega Chihiro > Free Play); with it off, Select inserts a coin.

For DOA2 Ultimate's long initial black screen and Otogi's startup, see [game startup checks](docs/game-startup.md). Runtime Disc-check Compatibility lets Otogi start from an unmodified image with Complex 4627; the offline preparation utility remains optional. The [PR #1659 audit](docs/xdvd-pr1659-audit.md) explains the signed-disc authentication requirements and the runtime fallback.

## Chihiro controls

The cabinet's controls are on RetroArch's pads, by the game:

| | |
|---|---|
| Every game | Start = Start, Select = coin, L3 = service, R3 = test; player 2's Start and Select on port 2 |
| Gun games (House of the Dead III, Virtua Cop 3, Ghost Squad) | Sega Chihiro > Light Gun Aim on Analog Stick (the default): aim with the left stick or D-pad; trigger = R2, the gun's button = L2, pedal or fire rate = L, off the screen = R. On Mouse: aim with the mouse (or RetroArch's light gun) on the player's port; trigger = left button (gun trigger), the gun's button - House of the Dead III's pump reload, Virtua Cop 3's change weapon, Ghost Squad's action - = right button (gun Aux A), Virtua Cop 3's pedal and Ghost Squad's fire rate = middle button (gun Aux B), off the screen = gun Reload. Ghost Squad's card = X. Each player's aim is drawn over the picture, player 1 red and player 2 blue (Sega Chihiro > Crosshair) |
| Driving games (Crazy Taxi, OutRun 2, Maximum Tune) | steer = left stick or D-pad, gas = R2, brake = L2; OutRun 2: R/L = shift up/down, Y = view; Crazy Taxi: R = drive, L = reverse, B = jump; Maximum Tune: R/L = shift up/down, Y = view, A = intrude, X = card |
| Ollie King | swing = left stick or D-pad left/right, board front/rear = up/down, L/R = grabs |
| Gundam | left stick or D-pad = left stick, L2/L = its trigger/button; right stick = right stick, R2/R = its trigger/button; B = pedal, X = card |

The driving games' force feedback (OutRun 2, Maximum Tune) can rumble port 1's pad: Sega Chihiro > Force Feedback Rumble, at 100% by default (off in standalone), from 25 to 200% or off. A pad gives the knocks and vibrations; the wheel's centering and weight have nothing to act on. Controls > Rumble (Player 1) and (Player 2) turn a player's rumble off altogether, and Controls > Analog Dead Zone sets how far each player's sticks can move before the game sees it (10% by default).

## Timing and sound

Each frame RetroArch asks for is one frame of the Xbox's time, 1001/60 ms (NTSC, 59.94 frames a second), and comes with exactly that frame's sound, 800 or 801 samples at 48 kHz. Between frames the Xbox stands still, so RetroArch's pause, menu, fast-forward and slow motion are the game's; fast-forward goes as fast as the host runs the game. The Xbox's time is counted in the instructions its CPU runs (1.977 ns each, a Pentium III's pace on game code), rather than elapsed host time; the frontend frame budget is independent of host speed. Waiting costs nothing: a verified kernel idle loop or a tight loop polling an unchanged device register is a wait, including a poll that stores its value on the stack; store-free computation and read-only scans keep running at their instruction budget; the Xbox's time moves on to the next event. The GPU draws alongside the CPU, as on the console, and each frame goes to RetroArch once the GPU has drawn it. Waiting for host GPU or disk completion does not advance the instruction clock; the frontend controls real-time pacing. The RTC and Chihiro calendars start at 2000-01-01 UTC and advance with guest time. Legacy states restore their saved calendar second and rearm RTC timers; their old format did not save the subsecond host-clock snapshot. New states preserve the virtual-clock phase. Audio > Audio Volume scales the sound before it goes to RetroArch.

Housekeeping timers use a runtime-only service clock advanced exactly once per active `retro_run`, with the same NTSC fraction as the guest frame budget. Pausing frontend calls stops this clock; loading a guest state does not rewind it. Socket polling waits for readiness notifications rather than sleeping for emulated timer deadlines. Positive polling durations check readiness immediately; they never arm a host timer. GPU report readiness checks yield and retry without a timed polling interval. Thread waits use fixed iteration counts instead of host-time calibration or hardware deadline spins. The core does not read or calibrate host timestamps.

## Video

Core Options > Video > Force Texture LOD0 (Restart) forces the largest texture level in both renderers. Compatible GPU render textures retain internal resolution; Vulkan also preserves GPU faces of eligible mixed RAM/GPU cubemaps at that resolution, expanding only the RAM faces on the GPU. Compatible Vulkan 2D surfaces are sampled directly without a texture copy. Byte-reinterpreted aliases, depth and incompatible layouts retain their existing fallback. It is disabled by default and can increase shimmering; restart content to apply.

S3TC textures keep native compressed storage when supported. Larger fallback textures can decode into final GPU images on hardware GL 4.3 (volumes) or Vulkan (2D, cube and volume textures); small textures, older GL and software renderers retain CPU decoding.

Core Options > Video > Force Quality Filtering (Restart) independently requests up to 16x anisotropic filtering on wrapping or mirrored, linearly filtered 2D color textures, capped by device support. Point/depth sampling, G8B8 data, cubemaps, clamped/bordered textures and unsupported formats retain game filtering to avoid changing data sampling or widening footprints across seams. Texture address modes remain unchanged. It is disabled by default; restart content to apply. The core cannot infer internal tile boundaries in a wrapping texture atlas, so such game-specific layouts still need visual validation.

Above native resolution, eligible depth-disabled 2D color rectangles with matched fixed-mip filtering constrain texture sampling to their native pixel-center range, reducing atlas-edge seams while retaining linear filtering. Rotated geometry, varying depth, varying projective texture coordinates and special texture paths retain their existing behavior. This does not snap all screen geometry or remove every possible seam.

Guest RGB gamma ramps are applied automatically during presentation in both renderers, after video overlay composition. Game brightness settings and gamma fades need no core option. Neutral ramps bypass the lookup.

The core renders with OpenGL or Vulkan, whichever RetroArch's video driver is (`glcore` or `vulkan`). On Android it is Vulkan only. On macOS it is OpenGL: MoltenVK has no geometry shaders, which xemu's Vulkan renderer needs, so use RetroArch's `glcore` driver there.

## Building

With GNU make alone: the tree has no meson, ninja or python build. `make` builds the core for
the system it runs on, in the source tree; `make DEBUG=1` builds it
unoptimized with debug information. Nothing is downloaded: the APU DSP's
JIT is the core's own (`hw/xbox/mcpx/apu/dsp/jit`).

**Windows**, in MSYS2's MINGW64 shell, with only the compiler installed:

```
pacman -S --needed git make mingw-w64-x86_64-gcc
git clone https://github.com/WizzardSK/xemu-libretro.git
cd xemu-libretro
make -j$(nproc)
```

The core uses libretro-common GL dispatch on Windows; libslirp is built from
source in `deps/` (`deps/README.md`), and
`parallel_xbox_libretro.dll` needs only Windows' own DLLs. From Linux:
`make platform=win CROSS_PREFIX=x86_64-w64-mingw32-` with MinGW-w64.

**Linux** (x86_64, aarch64), with libepoxy from the system:

```
sudo apt install build-essential pkg-config libepoxy-dev curl
make -j$(nproc)
```

**macOS** (arm64, x86_64), with Xcode's command line tools and the
libraries upstream xemu's build takes from MacPorts:

```
brew install pkgconf
pip3 install requests
python3 scripts/download-macos-libs.py arm64
PKG_CONFIG_LIBDIR=$PWD/macos-libs/arm64/opt/local/lib/pkgconfig make -j$(sysctl -n hw.ncpu)
```

For an Intel Mac's core, `x86_64` in place of `arm64`, and `ARCH=x86_64` to
`make`. The libraries are dylibs: the CI workflow puts them in
`parallel_xbox_libretro_libs` next to the core, with their paths pointed
there.

What QEMU's build generates is pregenerated in `libretro/gen` (its README).
GLib is not linked: QEMU and libslirp are built on the core's own
`libretro/glib` compatibility subset on libretro-common. Unused tree, datalist,
mapped-file, date-object, timer and process-launch services are omitted.

**Android** (arm64-v8a, x86_64), with the Android NDK and nothing else
(every library the core links is built from `deps/`):

```
make -j$(nproc) platform=android NDK=/path/to/android-ndk ANDROID_ABI=arm64-v8a
```

`ANDROID_ABI=x86_64` for the x86_64 core. It is
`parallel_xbox_libretro_android.so`, for Android 10 or newer.

## License

GPL-2.0, as xemu and QEMU. See [LICENSE](LICENSE).

Block completion regression tests (`libretro/tests/test-block-completion.py`) exercise
production request and drain functions with a modeled bottom-half scheduler. They
cover nested drain/cancel callbacks, callback-submitted work, slot retention and
balanced counters; backend destruction and media I/O are outside this model.

### Vulkan memory reservations

The Vulkan heap reserves its backing and metadata at renderer
startup, then normally suballocates and reuses that capacity without growing or
mapping new blocks. If an image cannot fit, completed cached resources are
reclaimed first, then overflow backing is allowed instead of treating the
reservation as a fatal limit. Empty overflow blocks are released. Host and device buffer reservations are
computed from the actual startup buffers' Vulkan requirements, including both
staging sets and vertex arenas. There is no guessed host-memory padding.
Conversion buffers retain min(32 MiB * scale^2, 800 MiB) each. Host memory
prefers CPU caching, with noncoherent flush/invalidate ranges isolated by atom.

The optimal-image reservation is max(128 MiB, 32 MiB * scale^2):
128 MiB at 1x/2x, 512 MiB at 4x, and 2 GiB at 8x. Cold boot temporarily
keeps boot render targets alongside game targets; NGB startup reached about
1.46 GiB of live image backing at 8x. Retired image objects can release their
spans under pressure;
unfinished GPU resources cannot be reclaimed. Capacity exhaustion, fragmentation,
incompatible memory types and driver-required dedicated images can use overflow
backing after reclamation; no new GPU wait or readback is added. Raising resolution beyond the startup reservation requires restarting.
The existing imported/exported memory and display-image allocation paths are
separate from this heap; fixed heap backing is not a blanket guarantee that no
Vulkan allocation can ever occur on a display-size change.

Guest main RAM and NV2A RAM already share one backing allocation. With
`VK_EXT_external_memory_host`, compatible systems expose those same pages as a
Vulkan buffer for direct texture uploads, GPU conversions and surface downloads.
This does not make upscaled/optimal images aliasable with guest RAM, or remove
the snapshots needed when guest writes overlap unfinished GPU work. Persistent
mapping and coherent memory do not replace CPU/GPU synchronization.

GP executes on the CPU owner; eligible EP ticks use a separate worker on
lock-free atomic targets. The worker owns its reachable guest-RAM pages until
completion. CPU writes, conflicting DMA and GPU imported-memory commands
retain that ownership ordering; incompatible cases execute inline. VP and GP
reuse the existing mix buffer, and EP DMA writes audio directly into the
frontend queue. Audio producer handoffs and state/unload boundaries drain the
worker. This is an ownership change, not a general frame-rate improvement.


The renderer uses an Xbox-specific C allocator instead of VMA, removing the
core's C++ runtime dependency. Measured startup driver allocation/map counts
and binary size improve; a general frame-rate improvement is not claimed.
The fixed image pool reserves spare memory upfront to avoid ordinary heap growth
during play, with reclaim-first overflow available for image-memory pressure. The earlier 384 MiB image pool and its 25-142 MiB overhead measurements only
covered loaded savestates and were insufficient for cold boot. The corrected
reservation is larger; allocation sizes do not measure resident VRAM.

VP-monitor worker output reduction is covered by 48 direct cases across active
worker masks and generation boundaries: only stereo monitor samples are reduced
in VP mode, while DSP mode reduces mix bins. Stale and owner buffers are excluded.
This removes 1,024 unused integer additions per participating worker in VP mode;
physical cache traffic and game frame-rate improvements have not been measured.
The existing VP completion barrier remains.

VP scheduling validates routing and forms units in a single scan, without a
256-entry routing-mask array. The threaded fixture checks 55,987 routing layouts
against independent symbolic bin reads, clears and additions, including reversed
unit execution. It also covers repeated handles and serial fallback. This changes
scheduler scratch use, not the existing worker completion barrier or frame queue.

VP list traversal rejects handles outside the 256 hardware voices before
descriptor or per-voice state access, including list-head values with upper bits
set. The capacity fixture checks 864 malformed heads and links across all lists,
monitor modes and active/idle/locked prefixes. Invalid entries stop that list;
valid prefixes and the other lists still run. Synthetic ROM modes
`VP_INVALID_HEAD` and `VP_INVALID_LINK` exercise these paths in the native core.

The DSP-off VP monitor computes its maximum eligible route gain once when
those gains stay constant across the frame. Ramping routes keep the per-sample
calculation; no persistent gain cache or additional audio frame is stored. The
threaded fixture checks 12,000 exact gain frames (stable and ramped) across
headroom, channel selection, spatial routes and multipass-bin exclusion.

Fresh loads and resets retain 64 samples (1.33 ms at 48 kHz) of audio preroll,
down from 320 samples. The existing frame-pull ownership fence retires DSP work
before queue access; EP bursts contain current and future samples. The two-tick
phase margin leaves at least 33 samples after a pull at the worst cadence phase.
This removes 5.33 ms of queue latency without changing the exact 800/801-sample
frontend cadence. Existing savestates retain their queued PCM and queue lead;
loading does not drop samples or add delay padding. Existing completion waits
remain; this is not a claim that all core paths are nonblocking.

HRTF peak detection shares the history/input loads instead of scanning the
linear buffer again. Unsigned magnitudes cover INT32_MIN without signed
negation. A 6,424-frame wide-integer oracle checks every ring position, tap
and delay ramps, narrow/wide boundaries and full-range samples. Filter storage,
savestate layout, frame cadence and completion waits are unchanged.

NVNet RX delivery reuses the descriptor read by its own readiness check,
performing one guest descriptor DMA read instead of two inside that receive
call. The backend readiness callback still reads fresh guest state, and no
descriptor is cached between callbacks. Payload staging retains snapshot
semantics for potentially guest-backed packet buffers. No lock, wait, timer
interval, state field or queue depth changes.

The resampler regression fixture checks 1,048,576 cases across every Q16
fraction, all eight ring heads and stereo sample ranges, including INT32_MIN/MAX,
against a wide-integer dot-product oracle. An exact-row specialization was
rejected after isolated timing and stack checks found tradeoffs; production
resampling, buffering and synchronization remain unchanged.

ADPCM fetches decode only the predictor prefix needed for the requested window
and write VP samples directly into the existing destination. The intermediate
65-sample PCM array is removed; encoded SGE fragments still use the necessary
bounded gather. A 1,424-byte read-only nibble-magnitude table preserves the
reference shift rounding. No source pointer, decoded cache or new per-voice
state survives the call. The ADPCM change reduced Win64 voice stack use; with PCM SGE boundary handling
it is 1,168 bytes.
Local decoder timing favors typical paired 32-sample windows; very short late
windows can be slower. Game throughput and hardware cache misses are unmeasured.

PCM frames spanning SGE pages use both logical mappings, including a stereo
channel beginning on the next page. Same-page and adjacent physical backing
retain direct scalar loads; only discontiguous backing needs a bounded gather
of at most eight bytes. No persistent map cache, frame buffer, lock or wait
is added. The boundary helper has a 160-byte Win64 frame; the ordinary voice
frame grows by 32 bytes, with HRTF remaining outlined and worker/owner stacks
unchanged. Hardware cache misses, register spills and game speed are unmeasured.

NVNet MAC filtering compares exact 32-bit and 16-bit little-endian fields
instead of temporary address arrays. It preserves existing acceptance and
trace behavior, ignores reserved upper register bits, and reads only the six
address bytes. A byte-wise oracle checks 200,000 packet/filter combinations.
Compiled Win64 receive stack sizes are unchanged; no throughput or hardware
cache/spill improvement is claimed. Payload staging and synchronization stay
unchanged.

Paused VP voices retain their descriptor and filter state while list traversal
continues. They do not enter the worker queue, so a fully paused frame publishes
no VP work. Unpausing takes effect on the next VP frame; GP/EP processing and
the frontend audio cadence remain unchanged. This removes redundant VP work,
not the completion barrier for frames that actually dispatch voices.

OpenGL mirrors its effective clamped line width alongside the existing draw
state cache. Repeated equivalent widths reuse driver state; display/conversion
draw invalidation forces the next update. This does not add shader or pipeline
variants, or change wide-line geometry expansion. No FPS or hardware cache-miss
improvement is claimed from the reduced driver-call count alone.

LPC GPIO ports remain decoded at 0x80c0-0x80d9 independently of PM BAR enable
or relocation. Early firmware encoder probes therefore see the emulated GPIO
value before PCI setup. The existing PM region owns the mapping; this adds no
new device, serialized fields, waits or frame scheduling changes.

OpenGL cache persistence uses one libretro-common worker and two bounded slots.
The owning context exports module binaries directly into reusable slot storage;
legacy saves transfer their immutable snapshots. Cache pressure defers saves
without waiting for disk progress, and eviction holds no writer handle. Driver
metadata/path snapshots survive binding reuse. The existing cache file formats
and shader/PSO keys are unchanged. The binding pool is 400 KiB smaller on Win64
from removing per-entry thread handles, while two variable-capacity module
slabs are retained for safe reuse. Renderer teardown drains and joins the worker.
Driver binary extraction, first-use compilation, cache reads/list writeback and
other existing GPU/resource waits can still stall; this is not a global
nonblocking claim or a measured FPS/cache-miss improvement.

Cache-enabled GL links explicitly request retrievable program binaries (the
legacy path checks API/extension support first). This changes cache policy,
not generated shader state or its keys.

Short POSIX vector transfers feed their remaining first span into the existing
adjacent-buffer coalescer, avoiding a separate native call when those remaining
buffers are contiguous. Descriptors stay immutable and no staging copy is added.
The fault-injection fixture checks 3,875 short/EINTR/EOF/error cases, including
residual call counts; this is not a measured disk-throughput or FPS gain.

Empty vector descriptors no longer split adjacent mapped/native fallback
buffers. The 3,898-case syscall fixture covers interspersed NULL/non-NULL
empty spans, short transfers and the >IOV_MAX scalar fallback; a full
contiguous fallback transfers in one successful call instead of three.
Descriptor arrays remain immutable, with existing EOF and range checks.

Read-only QCOW2 opens omit the write-cluster workspace and release temporary
metadata decode storage after loading tables. This retains two fewer cluster
sizes per image (128 KiB at 64 KiB clusters); compressed input/output buffers
and eager inflater creation remain available for reads. Writable workspace
behavior is unchanged. Allocation-failure and byte checks cover this saving;
no startup-time or frame-rate improvement is claimed.

QCOW2 sparse reads group consecutive hole/zero clusters before filling their
destination runs. A contiguous 16-cluster zero request uses one fill instead
of sixteen; fragmented destinations retain separate fills. Allocated data
and compressed clusters end the run, and fills remain bounded by SIZE_MAX
on 32-bit hosts. This changes operation counts, with no measured FPS or
storage-throughput claim.

Frontend VFS reads ignore NULL/non-NULL empty descriptors when coalescing
adjacent destinations. A three-span contiguous request separated by empty
entries needs one provider read instead of three when it completes fully.
Short-read, EOF, cursor recovery and descriptor immutability remain covered;
no staging buffer, allocation or throughput/FPS claim is added.

Xbox EEPROM hashing uses libretro-common streaming SHA-1 with the original
custom initial states and 512-bit prefix count. The duplicate util/sha1
engine is removed. Deterministic reference checks cover 4,100 hashes and
400 full EEPROM images across all four versions; OS entropy remains in
production. No new CPU requirement or measured speedup is claimed.
