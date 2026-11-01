#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check single-level G8B8 mip-filter requests and preserve existing paths."""
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
    parser.add_argument("--linear-layout", action="store_true",
                        help="exercise linear G8B8; multi-level controls stay swizzled")
    parser.add_argument("--custom-border-colors", action="store_true",
                        help="requires the existing Vulkan custom-border-color capability")
    args = parser.parse_args()
    here = args.checkout.resolve() / "libretro/tests"
    read_log = runpy.run_path(str(here / "check-bump-probe.py"))["read_log"]
    args.scratch.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="g8b8-linear-" if args.linear_layout else "g8b8-single-", dir=args.scratch.resolve()))
    env = dict(os.environ)
    env.setdefault("EGL_PLATFORM", "surfaceless")

    def build(name, u, v, filter, single=True, mixed=True, bias=0.25, color=0, guest=False, prime=False):
        output = root / name
        command = [sys.executable, str(here / "build-bump-probe.py"), str(output),
                   "--assembler", args.assembler, "--mip-filter", str(filter),
                   "--mip-address-u", u, "--mip-address-v", v, "--lod-bias", str(bias)]
        if prime:
            command.append("--prime-address")
        if single:
            command.append("--single-level")
            if args.linear_layout:
                command.append("--linear-level")
        if mixed:
            command.append("--mip-mixed")
        if guest:
            command.append("--base-only")
        if color:
            command += ["--border-color", hex(color)]
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
    for u, v in (("wrap", "wrap"), ("border", "border")):
        for filter in (3, 6):
            output = build(f"transition-{u}-{v}-{filter}", u, v, filter, prime=True)
            check(output)
            scalar += 192
    for u in ("wrap", "edge", "mirror", "border"):
        for v in ("wrap", "edge", "mirror", "border"):
            for filter in (3, 4, 5, 6):
                output = build(f"mixed-{u}-{v}-{filter}", u, v, filter)
                check(output)
                scalar += 192
    for u, v in (("wrap", "wrap"), ("border", "border")):
        for bias in (-4, 0, 4):
            for filter in (3, 6):
                output = build(f"bias-{u}-{v}-{filter}-{bias}", u, v, filter, bias=bias)
                check(output)
                scalar += 192
        for filter in (3, 6):
            output = build(f"force-{u}-{v}-{filter}", u, v, filter)
            check(output, True)
            scalar += 192
    colors = [0xffffffff]
    if args.custom_border_colors:
        colors += [0x40c02080, 0x9020c040]
    for color in colors:
        for filter in (3, 6):
            output = build(f"color-{color:08x}-{filter}", "border", "border", filter, color=color)
            check(output)
            scalar += 192
    for u, v in (("wrap", "wrap"), ("border", "border")):
        for filter in (3, 4, 5, 6):
            compare(build(f"matching-{u}-{v}-{filter}", u, v, filter, mixed=False))
            controls += 192
    for guest in (False, True):
        for u, v in (("wrap", "wrap"), ("border", "wrap")):
            for filter in (3, 6):
                output = build(f"multi-{'guest' if guest else 'force'}-{u}-{v}-{filter}",
                               u, v, filter, single=False, guest=guest)
                compare(output, not guest)
                controls += 192
    if args.linear_layout:
        for force in (False, True):
            output = root / f"existing-nonmip-{force}"
            subprocess.run([sys.executable, str(here / "build-bump-probe.py"), str(output),
                            "--assembler", args.assembler, "--mixed", "--lod-bias", "0.25"],
                           check=True)
            compare(output, force)
            controls += 192
    print(f"PASS: {scalar} single-level scalar cases and {controls} exact parent pixels "
          f"per renderer; logs in {root}")


if __name__ == "__main__":
    main()
