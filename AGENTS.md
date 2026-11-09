# Rules for coding agents

These apply to any AI agent working in this repository (Claude Code reads them through `CLAUDE.md`). They came out of testing the libretro cores with their testers.

## Branches and history

- The `libretro` branch is never deleted, and nothing that rewrites its history is run on it: no force-push, no rebase, no amending or squashing of commits that are already pushed. It is the fork's only long-lived branch.
- New features and experimental changes go on their own branch, and testers test builds of that branch. When testing is done, the related commits are squashed and merged into `libretro`, so the main branch does not collect commits that were superseded midway.
- A fix that users of the `libretro` builds need before the branch is merged is cherry-picked into `libretro`, in a way that does not break the later merge of the branch.

## Merging upstream xemu

This repository builds the libretro core and nothing else. The standalone's parts - its UI, packaging, upstream's CI and its contributor docs - were deleted on purpose, and must not come back with a merge. The paths are listed in `.upstream-excluded`.

- Upstream is Tovarichtch's xemu fork, Tovarichtch/xemu, branches `main` and `perf-experimental` (remote `tovarichtch`), which carries xemu-project/xemu `master` (remote `upstream`) with Chihiro and renderer work on top. Its commits come in by full merges; xemu `master` commits the fork has not merged can be merged directly, keeping the fork's work in `hw/xbox/nv2a` conflicts. Only full merges, no cherry-picks.
- Right after `git merge` of upstream, before resolving anything else, run:
  `git rm -r -q --ignore-unmatch --pathspec-from-file=.upstream-excluded`
  This settles the modify/delete conflicts in favour of the deletion, and it also removes files upstream newly added under those paths, which git would otherwise bring in without any conflict.
- Never resolve a conflict on one of those paths by restoring the file.
- When upstream changes a build file around one of the removed parts, keep them removed and take the rest of the change.
- When something new is deleted for the same reason, add its path to `.upstream-excluded` in the same commit.
- This `AGENTS.md` replaces upstream xemu's own. When a merge conflicts on it, keep this one.
- After a merge, `upstream.version` names the upstream version merged; the core reports that version, and a new release is built.
- `CHANGELOG.md` is this fork's own. When a merge conflicts on it, keep this one.

## These files

- Only the developer writes to or deletes `AGENTS.md` and `CLAUDE.md`. An agent does not change them on its own; it proposes the change to the developer instead.

## Changelog and README

- `CHANGELOG.md` is updated in the same commit as the change it describes: what changed for someone using the core, newest first. Not a copy of the commit message.
- A change on a feature branch gets its entry on that branch, under "Unreleased"; it reaches `libretro` with the squash merge.
- `README.md` is kept true in the same way: when a change makes a sentence in it wrong (a limit lifted, an option added, a file moved), fix the sentence in that commit.

## Code

- Keep comments true. When a change makes a comment describe behaviour that no longer exists, fix or remove the comment in the same commit.
- Look at the big picture, not just the function being changed. For example, resetting the content and closing it have to release the game's resources through the same path; a reset is an unload and a load.
- Stop and join threads the way upstream xemu does in its own shutdown, not with methods made up for the libretro port. Ad-hoc ones are what cause shutdown and reset bugs.
- Every core option has to be connected to something. Do not add an option the core does not read, and remove one that turns out to do nothing. Option defaults follow the standalone's defaults unless there is a reason, written down beside the option.
- xemu's own `xemu.toml` is not where the core's settings live. If you need a setting, use a core option (the `.opt` file) instead.

## Testing

- Changes confined to DSP/DSP JIT code and their tests or documentation do not require shader/PSO regression testing, shader CI comparisons or compiler timings, or shader/PSO cold/warm gameplay qualification. DSP-specific correctness, sanitizer and architecture checks still apply. Mixed changes that also affect shaders or PSOs retain the shader/PSO requirements for those changes.
- The core logs, at start, its version and the commit of this repository it was built from. Ask testers for RetroArch's log, and check which commit a log came from before drawing conclusions from it.
- Compare with standalone xemu at the same upstream version before calling something a core bug; when standalone fails the same way, it is upstream's.

## Cold first play, shader/PSO performance and ownership

- As of 2026-11-08, parallel-xbox shader/PSO cold-first-play smoothness is **BAD and regressed**. DOA3's first fights visibly stutter. Keep this an open regression until repeatable cold-boot gameplay evidence proves recovery. Do not silently promote current timings, a warmed cache or a newer commit into an accepted baseline.
- The target is first startup and first gameplay with no pre-existing compiled shader/pipeline cache being nearly imperceptibly different from fully warmed play: no obvious compilation interruptions. Average FPS, valid SPIR-V, faster CPU translation, a large cache-hit rate or smaller modules alone do not establish this.
- The hard recovery targets are zero post-boot draw-critical shader/program/PSO creations and zero compile waits, including clear, copy, texture conversion, reinterpretation, depth, palette and other utility pipelines. Every draw needs an already-correct ready path. These are targets the current regressed implementation does not yet satisfy, not permission to skip unknown work or a claim that all host API calls can move to an arbitrary thread.
- Except for DSP/DSP JIT work exempted under Testing above, every commit must be compared against the shader/PSO regression suite. Run deterministic key, compiler, fallback, worker-publication, file-I/O and lifetime tests in GitHub CI; retain per-commit compiler timings and artifacts against the parent and a separately pinned, verified last-known-good reference. Runtime changes also require repeated cold/warm gameplay qualification on reference hardware before delivery. Missing fixtures, missing hardware, unsupported baseline schemas, noisy results and unrun tests are **unqualified**, never passes. Documentation-only commits may reference byte-identical runtime evidence but must preserve any existing failed/unqualified status.
- Compare fresh boot through first fights/scene transitions, not only a loaded or repeatedly restored checkpoint. Keep cache-empty, seeds-only, warm persistent-cache and warm in-process results distinct. Disclose bundled seed coverage and private driver-cache state. Never delete user caches to manufacture a cold test.
- Measure first encounters, per-stage compilation and module adoption, full PSO creation/GPL linking, fallback readiness, queue pressure, cache I/O and presented-frame latency separately. Keep p50/p95/p99/p99.9/max, worst-frame IDs, deadline misses and correlated compile/PSO stalls. Do not discard cold-start spikes as warmup or reuse the steady-throughput 8% budget as a stutter allowance. A new reproducible visible hitch or shader-correlated deadline miss fails even if average FPS improves.
- Preserve game progress, correct draws, audio cadence and frontend-driven pacing. Never hide a stall by skipping draws, slowing guest time, showing incomplete frames, adding pacing sleeps, or accepting an inaccurate placeholder. Preserve a correct uncovered-state path while fixing coverage; report remaining synchronous misses honestly.
- Shader/PSO optimization, zero-copy, borrowed lends and file I/O are one ownership/performance requirement. Do not introduce whole-file slurps, a second byte-for-byte payload buffer, payload clone during queue handoff, eager full-cache scans, or synchronous cache writes on the gameplay critical path. Prefer validated mapped/borrowed spans and reads directly into final owned storage. Mapping is not proof the driver performs no internal copying.
- Each lend must name its owner, bounds, generation, publication and last CPU/GPU consumer. Queue jobs must retain an owner/reference or stable immutable slot until completion; a pointer alone does not extend lifetime. Do not retain guest pointers, stack create-info, recyclable queue views or mutable descriptors beyond their valid window. End mapping/arena lends only after their last consumer. Any indispensable snapshot or conversion must be bounded, measured and explicitly justified rather than labelled zero-copy; remove unnecessary copies without removing correctness ownership.
- A prepared SPIR-V module, a VkShaderModule, a driver cache blob and a ready PSO are different milestones. Keep correct dynamic fallback available while specialization runs. Full speculative queues defer optional work without draw-side waits or spins. Preserve native retirement, cancellation, reset/load, renderer/device change and unload ordering.
- Use opt-in diagnostics only; remove them and rebuild clean objects before timing/delivery. No unconditional release logging, per-draw profiling or extra GPU readbacks. Timing runs must report zero readback allocations and must not overlap builds or other emulator benchmarks. Run separate image/value and lifecycle checks.
- Performance may improve or remain indistinguishable within established measurement noise; a reproducible regression is not accepted as normal engineering drift. Correctness fixes with unavoidable cost must state the tradeoff and retain an open performance issue rather than erase evidence or relax thresholds silently. Never change cache defaults, inflate reservations/worker counts, or change key/version rules merely to make a result pass.
- Ratchet the verified baseline: measured improvements lower the matching per-corpus budgets; nothing automatically raises them. Any intentional exception needs a separate, explicit user-approved change with retained before/after evidence. Track compile/PSO counts, emitted words/instructions, hot-path allocations, bytes copied/scanned and cache I/O alongside latency. Compiler speedups that worsen GPU shader execution are regressions too.
- Keys must cover every consumed static field, backend, compiler/generator target and relevant host option; hash collisions require exact-key handling. Zero padding and canonicalize only proven-unused/dynamic fields. Never use pointer identity for persistent keys. A semantic key/schema change requires an explicit compatibility/version decision even when struct size is unchanged. Corrupt/incompatible caches are recoverable misses, not a reason to reuse wrong code.
- The recovery plan, cache matrix, CI limits and acceptance requirements are in `docs/cold-first-play-recovery.md`. Public CPU/software-GPU CI is necessary but does not certify physical-GPU cold gameplay. A red or unqualified cold-play gate must not be described as smooth or complete.

## Libretro pitfalls already hit in the other cores

Each of these was a bug in at least one of the cemu, rpcs3, vita3k or xenia cores. Check new code against them before asking testers.

- Audio: each retro_run hands the frontend exactly one frame's worth (sample rate / declared fps), never "whatever is queued". The game's audio thread waits once about 64 ms is queued; no bigger buffer on the core's side. Several streams (audio ports, clients, a music player) are mixed, not appended one after another, and each is taken in its own format and sample rate.
- Geometry: the size the frame is handed over at goes to the frontend: base geometry at load, SET_GEOMETRY when it changes, SET_SYSTEM_AV_INFO when it would exceed the max. The max has to cover the highest internal resolution.
- Pacing: the guest's vblank follows retro_run, and the core declares the guest's refresh rate; the frontend paces display and audio, the core does not sleep to pace itself.
- Files: everything the core writes goes under system/<core>/ (or saves/); nothing next to the RetroArch executable. The emulator's own log goes there too, and its warnings and errors also go to RetroArch's log.
- No window: the standalone's UI (on-screen keyboard, notifications, message boxes, profile dialogs) has no window in a core. Every path that reaches for the window or its UI thread needs a headless branch that answers the way the user most likely would.
- Unload: never end the process (no exit, no TerminateTitle-style shutdown). Stop and join every thread in upstream's order, and never wait without a timeout on a fence, event or thread the frontend has to drive. No vkQueueWaitIdle/vkDeviceWaitIdle while RetroArch holds its queue lock.
- Libraries that pin themselves (statically linked OpenSSL) keep the core loaded after dlclose; build them so they do not.
  Exception: threads upstream itself never joins. QEMU's RCU, vCPU and nv2a threads are ended by standalone's exit(), and its atexit handlers stay registered, so this core pins itself in retro_init (RTLD_NODELETE, a pinned module on Windows); unloaded, those threads ran on unmapped code and RetroArch crashed on close. Everything that can be stopped and joined the way upstream does still is.
- No downloads at run time; data files ship with the core.
