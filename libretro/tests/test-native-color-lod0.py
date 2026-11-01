#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check native mixed color sampling with nonmip filters on both APIs."""
import argparse
import csv
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("checkout", "parent", "candidate", "harness", "scratch"):
        parser.add_argument(name, type=Path)
    parser.add_argument("--assembler", default="nasm")
    args = parser.parse_args()
    here = args.checkout.resolve() / "libretro/tests"
    read_log = runpy.run_path(str(here / "check-bump-probe.py"))["read_log"]
    args.scratch.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="color-lod0-", dir=args.scratch.resolve()))
    env = dict(os.environ)
    env.setdefault("EGL_PLATFORM", "surfaceless")

    def build(name, options):
        output = root / name
        subprocess.run([sys.executable, str(here / "build-bump-probe.py"), str(output),
                        "--assembler", args.assembler, *options], check=True)
        return output

    def run(core, output, force=False, prefix=""):
        value = "enabled" if force else "disabled"
        for api in ("gl", "vk"):
            command = [str(args.harness.resolve()), str(core.resolve()), str(output / "system"),
                       "--frames", "240", "--strict", "--opt",
                       f"xemu_renderer={'vulkan' if api == 'vk' else 'opengl'}",
                       "--opt", f"xemu_force_texture_lod0={value}",
                       "--opt", f"xemu_force_quality_filtering={value}"]
            if api == "vk":
                command.append("--vulkan")
            command += (output / "peek-args.txt").read_text().splitlines()
            with (output / f"{prefix}{api}.log").open("w") as log:
                subprocess.run(command, check=True, timeout=120, env=env,
                               stdout=log, stderr=subprocess.STDOUT)

    def check(output, force=False):
        run(args.candidate, output, force)
        command = [sys.executable, str(here / "check-bump-probe.py"), str(output)]
        if force:
            command.append("--force-lod0")
        subprocess.run(command, check=True)

    def compare(output, force=False):
        run(args.parent, output, force, "parent-")
        run(args.candidate, output, force, "candidate-")
        cases = list(csv.DictReader((output / "cases.csv").open()))
        assert len(cases) == 192
        for api in ("gl", "vk"):
            a = read_log(output / f"parent-{api}.log")
            b = read_log(output / f"candidate-{api}.log")
            for case in cases:
                address = int(case["result_address"], 16)
                assert a[address] == b[address], (output.name, api, case["id"])

    scalar = controls = 0
    for format in ("rgba8", "rgb565"):
        for chain in (False, True):
            base = ["--native-color", format]
            if chain:
                base.append("--native-mip-chain")
            for bias in (-4, 0, 0.25, 1, 1.25, 4):
                output = build(f"mixed-{format}-{chain}-{bias}",
                               [*base, "--mixed", "--lod-bias", str(bias)])
                check(output)
                scalar += 192
            output = build(f"force-{format}-{chain}",
                           [*base, "--mixed", "--lod-bias", "0.25"])
            check(output, True)
            scalar += 192
            compare(build(f"matching-{format}-{chain}", [*base, "--color"]))
            controls += 192
    regressions = [
        ["--mixed", "--lod-bias", "0.25"],
        ["--mip-filter", "4", "--mip-mixed", "--single-level",
         "--mip-address-u", "border", "--mip-address-v", "border", "--lod-bias", "0.25"],
        ["--mip-filter", "6", "--mip-mixed", "--base-only",
         "--mip-address-u", "border", "--mip-address-v", "wrap", "--lod-bias", "0.25"],
    ]
    for i, options in enumerate(regressions):
        compare(build(f"g8b8-control-{i}", options))
        controls += 192
    print(f"PASS: {scalar} native-color scalar cases and {controls} exact parent pixels "
          f"per renderer; logs in {root}")


if __name__ == "__main__":
    main()
