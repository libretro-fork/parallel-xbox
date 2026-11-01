#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check centered signed G8B8 gathers and preserve ordinary parent pixels."""
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
    parser.add_argument("--windows-paths", action="store_true",
                        help="run a Windows harness through WSL with translated input paths")
    args = parser.parse_args()
    here = args.checkout.resolve() / "libretro/tests"
    model = runpy.run_path(str(here / "check-bump-probe.py"))
    read_log, expected = model["read_log"], model["expected"]
    args.scratch.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="g8b8-center-", dir=args.scratch.resolve()))
    env = dict(os.environ)
    env.setdefault("EGL_PLATFORM", "surfaceless")

    def build(name, options):
        output = root / name
        subprocess.run([sys.executable, str(here / "build-bump-probe.py"), str(output),
                        "--assembler", args.assembler, *options], check=True)
        return output

    def host_path(path):
        if args.windows_paths:
            return subprocess.check_output(["wslpath","-w",str(path.resolve())],text=True).strip()
        return str(path.resolve())

    def run(core, output, force=False, prefix=""):
        value = "enabled" if force else "disabled"
        for api in ("gl", "vk"):
            command = [str(args.harness.resolve()), host_path(core), host_path(output / "system"),
                       "--frames", "240", "--strict", "--opt",
                       f"xemu_renderer={'vulkan' if api == 'vk' else 'opengl'}",
                       "--opt", f"xemu_force_texture_lod0={value}",
                       "--opt", "xemu_force_quality_filtering=disabled"]
            if api == "vk":
                command.append("--vulkan")
            command += (output / "peek-args.txt").read_text().splitlines()
            with (output / f"{prefix}{api}.log").open("w") as log, (output / f"{prefix}{api}-stderr.log").open("w") as errors:
                subprocess.run(command, check=True, timeout=120, env=env,
                               stdout=log, stderr=errors)

    def compare(output, force=False, boundary=False):
        nonlocal scalar, repaired
        run(args.parent, output, force, "parent-")
        run(args.candidate, output, force, "candidate-")
        cases = list(csv.DictReader((output / "cases.csv").open()))
        assert len(cases) == 192
        for api in ("gl", "vk"):
            a = read_log(output / f"parent-{api}.log")
            b = read_log(output / f"candidate-{api}.log")
            for case in cases:
                address = int(case["result_address"],16)
                if not boundary:
                    assert a[address] == b[address], (output.name,api,case["id"],"parent")
                elif a[address] != b[address]:
                    repaired += 1
                pixel = [b[address] >> shift & 255 for shift in (24,16,8,0)]
                wanted = expected(case, force)
                assert all(abs(x-y) <= 2 for x,y in zip(pixel,wanted)), (output.name,api,case["id"],pixel,wanted)
        scalar += len(cases)
        print(f"PASS: {output.name}: 192 scalar pixels per API",flush=True)

    scalar = repaired = 0
    for side in ("before","after"):
        compare(build(f"boundary-{side}",["--mixed","--gather-boundary",side]),boundary=True)
    for name,options in (("bump",[]),("color",["--color"]),("mixed",["--mixed"]),
                         ("mixed-minus",["--mixed","--lod-bias","-4"]),
                         ("mixed-plus",["--mixed","--lod-bias","4"])):
        compare(build(name,options))
    for profile in ("scale_bias","negative_scale","overbright"):
        compare(build(profile,["--luminance-profile",profile]))
    for name,options,force in (
        ("single-wrap",["--mip-filter","4","--single-level"],False),
        ("single-edge-mixed",["--mip-filter","4","--single-level","--mip-mixed","--mip-address-u","edge","--mip-address-v","edge"],False),
        ("single-border-mixed",["--mip-filter","4","--single-level","--mip-mixed","--mip-address-u","border","--mip-address-v","border"],False),
        ("linear-wrap",["--mip-filter","6","--single-level","--linear-level","--mip-address-u","wrap","--mip-address-v","wrap"],False),
        ("mip-control",["--mip-filter","6","--mip-mixed"],False),
        ("mip-point-control",["--mip-filter","3"],False),
        ("force-border",["--mip-filter","6","--mip-mixed","--mip-address-u","border","--mip-address-v","border"],True),
        ("force-wrap",["--mip-filter","6","--mip-mixed"],True)):
        compare(build(name,options),force)
    print(f"PASS: {scalar} scalar pixels per API; {repaired} parent pixel differences in boundary cases; logs: {root}")


if __name__ == "__main__":
    main()
