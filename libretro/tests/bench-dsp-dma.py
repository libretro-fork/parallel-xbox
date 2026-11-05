# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare isolated DSP DMA loops against a Git revision; no game FPS claims."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import statistics
import subprocess
import tempfile

MODES = ["direct-in16", "direct-out16", "direct-in32", "direct-out32",
         "direct-interleave16", "direct-interleave32", "callback-out32",
         "callback-in32", "callback-interleave16", "callback-interleave32"]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("checkout", type=Path)
    p.add_argument("--baseline", required=True, help="Git revision, e.g. a83e6ba^")
    p.add_argument("--cc", default="gcc", help="Compiler command, as accepted by make CC=")
    p.add_argument("--iterations", type=int, default=100000)
    p.add_argument("--runs", type=int, default=5)
    p.add_argument("--counts", type=int, nargs="+", default=[32, 128, 512, 2048])
    p.add_argument("--json", type=Path, help="Optional full results, including individual timings")
    args = p.parse_args()
    if not 0 < args.iterations <= 0xffffffff or args.runs <= 0 or any(n <= 0 or n > 2048 for n in args.counts):
        p.error("positive runs, iterations in 1..4294967295 and counts in 1..2048 required")
    root = args.checkout.resolve()
    def git(*cmd):
        return subprocess.check_output(["git", "-C", str(root), *cmd])
    baseline = git("rev-parse", "--verify", args.baseline + "^{commit}").decode().strip()
    head = git("rev-parse", "HEAD").decode().strip()
    path = "hw/xbox/mcpx/apu/dsp/dsp_dma.c"
    source = root / "libretro/tests/bench-dsp-dma.c"
    module_hashes = {"baseline": hashlib.sha256(git("show", f"{baseline}:{path}")).hexdigest(),
                     "current": hashlib.sha256((root / path).read_bytes()).hexdigest()}
    compiler_version = subprocess.check_output(shlex.split(args.cc) + ["--version"], text=True).splitlines()[0]
    results = []
    with tempfile.TemporaryDirectory(prefix="dsp-dma-bench-") as tmp:
        tmp = Path(tmp)
        before = tmp / "before.c"
        before.write_bytes(git("show", f"{baseline}:{path}"))
        current = tmp / "current.c"
        current.write_bytes((root / path).read_bytes())
        # Reuse the real target's include paths, platform and architecture flags.
        dry = subprocess.check_output(["make", "-B", "-n", "dspdma", "CC=" + args.cc,
                                       "DSPDMATEST=" + (tmp / "unused").as_posix()], cwd=root, text=True)
        command = next(line for line in dry.splitlines() if "test-dsp-dma.c" in line and " -o " in line)
        flags = shlex.split(command)
        binaries = []
        for name, module in [("before", before), ("current", current)]:
            binary = tmp / (name + (".exe" if os.name == "nt" else ""))
            cmd = flags.copy()
            cmd[cmd.index("-o") + 1] = str(binary)
            cmd[-1] = str(source)
            cmd += ["-I" + str(root / "hw/xbox/mcpx/apu/dsp"),
                    '-DDSP_DMA_SOURCE="' + module.as_posix() + '"']
            subprocess.run(cmd, cwd=root, check=True)
            binaries.append(binary)
        for mode, label in enumerate(MODES):
            for count in args.counts:
                samples = [[], []]
                states = set()
                for trial in range(args.runs):
                    for which in ([0, 1] if trial % 2 == 0 else [1, 0]):
                        out = subprocess.check_output([str(binaries[which]), str(mode), str(count),
                                                       str(args.iterations), "1"], text=True).split()
                        samples[which].append(float(out[0]))
                        states.add(tuple(out[1:]))
                if len(states) != 1:
                    raise RuntimeError(f"state mismatch: {label} count={count}: {states}")
                old, new = map(statistics.median, samples)
                change = 100 * (new / old - 1) if old else None
                row = dict(mode=label, count=count, before_seconds=samples[0], current_seconds=samples[1],
                           median_before=old, median_current=new, elapsed_change_percent=change,
                           state=list(next(iter(states))))
                results.append(row)
                print(f"{label:24s} {count:4d}: {old:.6f} -> {new:.6f}s ({change:+.2f}%) state agrees")
    report = dict(baseline=baseline, head=head, compiler=args.cc, iterations=args.iterations,
                  runs=args.runs, counts=args.counts, results=results,
                  module_sha256=module_hashes, driver_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                  compiler_version=compiler_version,
                  scope="isolated warm conversion loops; excludes guest walkers, fencing, dirty tracking and game execution")
    if args.json:
        args.json.write_text(json.dumps(report, indent=2) + "\n")
    print(report["scope"])

if __name__ == "__main__":
    main()
