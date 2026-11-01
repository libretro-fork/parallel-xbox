#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check base-only signed borders and preserve excluded paths on Linux GPUs."""
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
    parser.add_argument("--custom-border-colors", action="store_true",
                        help="also test custom colors; requires Vulkan custom-border-color support")
    args = parser.parse_args()
    here = args.checkout.resolve() / "libretro/tests"
    read_log = runpy.run_path(str(here / "check-bump-probe.py"))["read_log"]
    args.scratch.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="g8b8-base-border-", dir=args.scratch.resolve()))
    env = dict(os.environ)
    env.setdefault("EGL_PLATFORM", "surfaceless")

    def build(name, u, v, filter, guest=False, mixed=False, color=0):
        output = root / name
        command = [sys.executable, str(here / "build-bump-probe.py"), str(output),
                   "--assembler", args.assembler, "--mip-filter", str(filter),
                   "--mip-address-u", u, "--mip-address-v", v, "--lod-bias", "0.25"]
        if guest:
            command.append("--base-only")
        if mixed:
            command.append("--mip-mixed")
        if color:
            command += ["--border-color", hex(color)]
        subprocess.run(command, check=True)
        return output

    def run(core, output, force, prefix=""):
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
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                               check=True, timeout=120, env=env)

    def compare(output, force):
        run(args.parent, output, force, "parent-")
        run(args.candidate, output, force, "candidate-")
        addresses = [int(c["result_address"], 16)
                     for c in csv.DictReader((output / "cases.csv").open())]
        assert len(addresses) == 192
        for api in ("gl", "vk"):
            a = read_log(output / f"parent-{api}.log")
            b = read_log(output / f"candidate-{api}.log")
            for address in addresses:
                assert a[address] == b[address], (output.name, api, hex(address))

    pairs = [(u, v) for u in ("wrap", "edge", "mirror", "border")
             for v in ("wrap", "edge", "mirror", "border") if "border" in (u, v)]
    scalar = 0
    for guest in (False, True):
        matrices = [(u, v, f) for u, v in pairs for f in (4, 6)]
        matrices += [("border", "border", f) for f in (3, 5)]
        for u, v, filter in matrices:
            output = build(f"{'guest' if guest else 'force'}-{u}-{v}-{filter}", u, v, filter, guest)
            run(args.candidate, output, not guest)
            check = [sys.executable, str(here / "check-bump-probe.py"), str(output)]
            if not guest:
                check.append("--force-lod0")
            subprocess.run(check, check=True)
            scalar += 192
    colors = [0xffffffff]
    if args.custom_border_colors:
        colors += [0x40c02080, 0x9020c040]
    for color in colors:
        for guest in (False, True):
            for filter in (4, 6):
                output = build(f"color-{color:08x}-{'guest' if guest else 'force'}-{filter}",
                               "border", "border", filter, guest, color=color)
                run(args.candidate, output, not guest)
                check = [sys.executable, str(here / "check-bump-probe.py"), str(output)]
                if not guest:
                    check.append("--force-lod0")
                subprocess.run(check, check=True)
                scalar += 192
    controls = 0
    for u, v in (("border", "wrap"), ("wrap", "border"), ("border", "border")):
        for filter in (4, 6):
            output = build(f"full-{u}-{v}-{filter}", u, v, filter)
            compare(output, False)
            controls += 192
    for guest in (False, True):
        for filter in (4, 5):
            output = build(f"mixed-{'guest' if guest else 'force'}-{filter}",
                           "border", "wrap", filter, guest, True)
            compare(output, not guest)
            controls += 192
    print(f"PASS: {scalar} scalar cases and {controls} unchanged excluded-path pixels "
          f"per renderer; logs in {root}")


if __name__ == "__main__":
    main()
