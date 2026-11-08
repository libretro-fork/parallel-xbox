# Trusted-runner callback evidence

`libretro/tests/gate-cold-play-frames.py` independently checks the callback
evidence from the administrator-provisioned hardware qualifier. The workflow
runs it even if that service fails. A zero service exit code with no raw
evidence cannot pass. Exit codes are 0 for this callback gate, 1 for a
regression and 2 for unqualified evidence; `callback-gate.json` retains the
result, percentiles, maxima, comparison outcomes, total hitch counts and the
64 worst hitch callback IDs per run. Published raw CSVs retain every frame.

This is an additional gate, **not full physical-GPU qualification**. It does
not measure presentation, GPU execution, draw-critical API creation or compile
waits, resource owners, skipped guest work or all scenes/cache modes. The
qualifier still has to implement the complete recovery plan, its separate
diagnostic/image/lifecycle tests and correct capture adapters. A producer's
binary/driver-cache/clean-core assertions need actual verification there;
this parser cannot independently authenticate them from timing metadata.
The accepted reference and trusted physical service remain unprovisioned.
Current cold-first-play smoothness stays BAD/regressed.

## Invocation and matrix

The qualifier writes `public-report/callback-runs.jsonl` and the CSV files it
references. The workflow passes the same `--revisions-json` matrix and pinned
`--accepted` SHA to both the qualifier and the repository gate:

```sh
python3 libretro/tests/gate-cold-play-frames.py \
  --report "$OUTPUT/public-report" \
  --revisions-json "$REVISIONS" --accepted "$ACCEPTED"
```

Every requested candidate needs its parent and pinned accepted comparison;
one pair covers both when the parent is the accepted reference. Every pair
requires OpenGL and Vulkan, each with `cache-empty` and `warm-persistent`
runs of the `doa3-first-fights` fixture. Each renderer/cache group contains
two ABBA blocks: block 0/1, slot 0/1/2/3, revisions reference/candidate/
candidate/reference. That is 32 fresh processes per pair. No missing run,
duplicate, reused CSV, wrong revision, changed host/options/binary or absent
accepted comparison becomes a pass. The full qualifier additionally needs
seeds-only and warm-in-process coverage; these are not silently substituted
for either fresh-process mode.

Uncontrolled driver caches are unqualified. `cache-empty` needs a verified
isolated empty driver-cache scope and an empty application cache;
`warm-persistent` needs a controlled warmed driver cache and a fresh process
reusing its verified application cache. Do not delete user caches or assume
an undocumented Windows environment variable isolates them. Record bundled
seed coverage and actual cache verification in the qualifier's other reports.

## JSONL schema 1

Each line is one object with exactly these fields. Blank/oversized records,
duplicate JSON keys, nonfinite values and unknown schema/fields are rejected.

| Fields | Meaning |
| --- | --- |
| `schema` | Integer 1. |
| `commit`, `reference`, `kind` | Exact requested pair; kind `parent` or `accepted`. Full lowercase commit SHAs. |
| `revision` | Actual built revision selected by the ABBA slot. |
| `renderer` | `opengl` or `vulkan`. |
| `cache` | `cache-empty` or `warm-persistent`. |
| `block`, `slot` | Integer ABBA position, not booleans. |
| `case` | `doa3-first-fights`. |
| `file` | Unique relative `.csv` path confined to `public-report`, including resolved symlinks. |
| `hardware` | Stable host/GPU/driver fixture ID, 1-128 ASCII letters/digits/`_.-`. |
| `options_sha256` | SHA-256 of actual effective options, identical within each renderer. |
| `binary_sha256` | SHA-256 of the actual clean core, identical for each revision. |
| `clean_core`, `fresh_process` | Boolean true; no per-draw instrumentation in timed cores. |
| `overlapping_work` | Boolean false; no overlapping builds/tests/emulator benchmarks. |
| `image_readbacks`, `image_allocations` | Integer zero; booleans do not satisfy them. |
| `driver_cache` | `isolated-empty` for cache-empty, `warmed-controlled` for warm-persistent. |

The capture adapter uses the supported DOA3 build's passive progression
metadata outside `retro_run`. It must verify the executable and addresses;
the parser does not infer game identity from a title string. Retail assets,
states and memory dumps stay private. The fixture service produces only
sanitized metadata/timings in the published report directory.

## Headerless CSV schema 1

Each row has these eleven integer fields, captured from callback 0 onward:

```text
callback,audio_frames,video_flag,callback_us,begin_qpc_us,game_state,scene,cursor,round_phase,life,live_count
```

Callbacks must be consecutive with positive durations and nonoverlapping
monotonic clocks. At the fixture's 48 kHz/59.94 Hz cadence, audio counts must
match the exact 800/801 schedule (4004 samples every five callbacks),
and video flags must be 1 (new) or 2 (repeat); full batch/padding and image
correctness remain the qualifier's responsibility. All boot callbacks are
retained. A menu state (0 or 2) and fight intro must precede 600 consecutive
live-count increments; increments require game state 1, scene 10, round phase
1. Truncated files, counter jumps and checkpoint-only replays are unqualified.

Each fight callback (state 1/scene 10) over 16,683 us fails, including intro
and reference runs. Faster averages or noisy repeats do not erase a hitch
or bless an already-regressed reference. Whole-boot candidate maxima over
the reference maximum by 5% also fail; startup spikes are retained. Paired
whole/fight/live run-mean checks reuse the existing throughput assessor with
unchanged 5% slowdown and 5% noise limits. Noise means unqualified. No timing
sample is discarded as warmup, and no automated baseline promotion occurs.

The parser streams bounded metadata lines, rejects escaping/oversized files
and keeps only bounded duration scalars for percentile calculation. It does
not slurp raw files, retain views into recyclable input buffers, or read/copy
geometry, shader binaries or cache payloads. This is offline test processing,
not a new runtime path or telemetry feature.

## Rejection controls and remaining evidence

`test-cold-play-frame-gate.py CHECKOUT` covers complete parent/accepted and
multi-commit matrices, stable throughput regression, noise, missing evidence,
invalid progression, instrumented/readback timing, identity changes, escaping
paths and CLI exit codes. The retained 116.504/170.166 ms live and 17.421 ms
intro hitch magnitudes must fail even when a median hides them. Synthetic
passing fixtures only validate the parser; they never certify a core or set
an accepted gameplay baseline. Shader/PSO contract CI runs these controls on
every candidate.

Private, opt-in frontend OpenGL diagnostics on 2026-11-08 also recorded
14.185/14.939 ms `GL_LINK_STATUS` calls inside 29.035/26.588 ms fight-intro
callbacks. Program-binary export was short in those captures. A later
stage-aware repeat, after earlier runs, still created fragment and vertex
programs during fight intros, including persistent-application-cache play,
but no longer reproduced the long link waits. Driver-cache state was
uncontrolled and run order differed: do not attribute a speedup, a source
regression or driver-cold recovery from these diagnostic runs. The first
long-wait trace did not collect caller CPU cycles, so it does not separate
driver waiting from caller descheduling. Native GL 1.1 imports were not all
intercepted; absent wrapper records are not proof of absent API work.

Both experimental GL purpose-coalescing timing matrices remain unqualified;
that runtime change is still withheld. This commit changes tests/workflow/
documentation only. It neither fixes uncovered GL/Vulkan first-use work nor
turns public CPU/software-GPU CI into physical qualification.
