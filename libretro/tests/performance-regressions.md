# Throughput regression checks

Correct output alone does not establish acceptable performance. CPU/JIT,
renderer and scheduling changes should also run paired checkpoint benchmarks
against the current release before delivery. Keep a known-good core binary
and private checkpoints for representative scenes, including a cheap screen
such as DOA3 copyright, a CPU-heavy scene and a GPU-heavy gameplay scene.
Never commit game files, BIOS files or retail checkpoints.

The November 6 carry-entry change `d12b464101` passed arithmetic/output tests
and improved JSRF, but reduced DOA3 copyright throughput from about 203 to
167 FPS. A specialized-carry lookup guard now runs in the existing GitHub
regression job. It compiles the actual lookup functions, checks 128,000
lookup cases, and requires a mutation restoring the bad generic-entry path
to fail. Incoming-carry arithmetic checks cover 524,288 cases for each target
width. These are deterministic guards, not substitutes for end-to-end timing.

## End-to-end gate

Build `harness.c` as described in its header. Capture a checkpoint with the
older baseline core using `--save FRAME --state-out FILE`; a newer checkpoint
may contain subsections the old core cannot load. Use the same machine,
renderer, fixed core-options file and private system/content files for both
cores. Exit other emulator processes. The script never changes frontend
configuration. Both cores may update the selected private emulator system
directory through normal emulation; use a dedicated test copy.

```text
python libretro/tests/benchmark-core-throughput.py --harness PATH/harness.exe --baseline PATH/good.dll --candidate PATH/new.dll --system PATH/private-system --content PATH/game.iso --state PATH/copyright.state --options PATH/fixed.opt --output PATH/new-results
```

The default is two ABBA blocks (eight sequential processes), 600 callbacks
per process, restoring the identical checkpoint every 200 callbacks. The
first 30 callbacks after each restoration are warmup. Measured means include
all subsequent callback times, including real stalls. No screenshots, video
hash readbacks, realtime throttling or Vulkan validation are requested during
timing. The output directory must be new. Each process saves its log and raw
CSV. The JSON report includes core/checkpoint/options/harness SHA-256 values,
core commit IDs, settings, block ratios and throughput.

- Exit 0: pass within the default 8% callback-time budget.
- Exit 1: regression beyond that budget, reproduced in every ABBA block.
- Exit 2: invalid or inconclusive experiment. Failed checkpoint loads, missing
  samples, validation failures, over 5% run-to-run spread, or an inconsistent
  large drop must not be treated as a pass.

Do not increase the budget to make a regression pass. Investigate, repeat an
inconclusive experiment on an idle machine, or explicitly document a justified
tradeoff. FPS is callback throughput here, not a measurement of RetroArch's
display thread. A small budget pass cannot rule out problems in other scenes.

`test-throughput-gate.py` tests the evaluator's rejection of the observed large
drop, noisy/inconsistent runs, malformed timings and incorrect sample windows.
Run the real benchmark against both the known bad build and the proposed fix:
the bad build must exit 1, while the fixed build must exit 0. Preserve both
reports. Public CI runs the deterministic guards and evaluator tests; retail
timing requires local game assets and dedicated hardware and is not silently
represented as a public CI benchmark.
