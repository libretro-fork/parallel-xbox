#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check effective linear mip counts and unchanged swizzled/count-one paths."""
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
    root = Path(tempfile.mkdtemp(prefix="g8b8-linear-count-", dir=args.scratch.resolve()))
    env = dict(os.environ)
    env.setdefault("EGL_PLATFORM", "surfaceless")

    def build(name, count, filter, u="wrap", v="wrap", mixed=True, bias=0.25,
              transition=False, linear=True):
        output = root / name
        command = [sys.executable, str(here / "build-bump-probe.py"), str(output),
                   "--assembler", args.assembler, "--mip-filter", str(filter),
                   "--mip-address-u", u, "--mip-address-v", v, "--lod-bias", str(bias)]
        if linear:
            command += ["--single-level", "--linear-level", "--linear-mip-count", str(count)]
        if mixed:
            command.append("--mip-mixed")
        if transition:
            command += ["--sampler-transition", "mip-to-lod0"]
        subprocess.run(command, check=True)
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

    def compare(output, canonical=None):
        canonical = canonical or output
        run(args.parent, canonical, prefix="parent-")
        run(args.candidate, output, prefix="candidate-")
        cases = list(csv.DictReader((output / "cases.csv").open()))
        assert len(cases) == 192
        for api in ("gl", "vk"):
            a = read_log(canonical / f"parent-{api}.log")
            b = read_log(output / f"candidate-{api}.log")
            for case in cases:
                address = int(case["result_address"], 16)
                assert a[address] == b[address], (output.name, api, case["id"])

    scalar = aliases = controls = 0
    for count in (0, 2, 15):
        for u, v in (("wrap", "wrap"), ("border", "border")):
            for filter in (3, 4, 5, 6):
                output = build(f"mixed-{count}-{u}-{filter}", count, filter, u, v)
                check(output)
                scalar += 192
            for filter in (3, 6):
                output = build(f"nonmip-{count}-{u}-{filter}", count, filter, u, v,
                               transition=True)
                canonical = build(f"canonical-{count}-{u}-{filter}", 1, filter, u, v,
                                  transition=True)
                compare(output, canonical)
                aliases += 192
    for u, v in (("wrap", "wrap"), ("border", "border")):
        for filter in (3, 4, 5, 6):
            output = build(f"matching-2-{u}-{filter}", 2, filter, u, v, mixed=False)
            check(output)
            scalar += 192
        for filter in (3, 6):
            for mixed in (False, True):
                output = build(f"control-1-{u}-{filter}-{mixed}", 1, filter, u, v,
                               mixed=mixed)
                compare(output)
                controls += 192
    for filter in (3, 6):
        output = build(f"force-15-border-{filter}", 15, filter, "border", "border")
        check(output, True)
        scalar += 192
        for bias in (-4, 4):
            output = build(f"bias-0-wrap-{filter}-{bias}", 0, filter, bias=bias)
            check(output)
            scalar += 192
        output = build(f"control-swizzled-{filter}", 3, filter, linear=False)
        compare(output)
        controls += 192
    print(f"PASS: {scalar} linear-count scalar cases, {aliases} count-one aliases and "
          f"{controls} exact parent control pixels per renderer; logs in {root}")


if __name__ == "__main__":
    main()
