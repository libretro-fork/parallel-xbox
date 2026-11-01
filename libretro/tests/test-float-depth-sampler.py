#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check GPU F16 decoding, filtering and RAM/native source changes."""
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
    root = Path(tempfile.mkdtemp(prefix="float-depth-", dir=args.scratch.resolve()))
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

    def check(output, force=False):
        run(args.candidate, output, force)
        command = [sys.executable, str(here / "check-bump-probe.py"), str(output)]
        if force:
            command.append("--force-lod0")
        subprocess.run(command, check=True)

    def compare(output, force=False):
        nonlocal scalar, controls
        run(args.parent, output, force, "parent-")
        run(args.candidate, output, force, "candidate-")
        cases = list(csv.DictReader((output / "cases.csv").open()))
        assert len(cases) == 192
        for api in ("gl", "vk"):
            a = read_log(output / f"parent-{api}.log")
            b = read_log(output / f"candidate-{api}.log")
            for case in cases:
                address = int(case["result_address"], 16)
                assert a[address] == b[address], (output.name,api,case["id"])
        controls += len(cases)

    scalar = controls = 0
    base=["--mixed","--native-color","rgba8","--native-depth","--native-depth-float"]
    for reference in (0,0.015625,0.125,1,2,16,32,256,511.9375):
        check(build(f"constant-{reference}",base+["--native-depth-float-constant",
                    "--native-depth-float-reference",str(reference)]))
        scalar += 192
    for reference in (0.125,1,2,16,32,256):
        for bias in (-4,0,1.25,4):
            check(build(f"ramp-{reference}-{bias}",base+
                        ["--native-depth-float-reference",str(reference),"--lod-bias",str(bias)]))
            scalar += 192
    for filter in (3,4,5,6):
        for force in (False,True):
            check(build(f"filter-{filter}-force-{force}",base+
                        ["--native-mip-filter",str(filter),"--native-mip-chain",
                         "--native-depth-float-reference","16","--lod-bias","1.25"]),force)
            scalar += 192
    for reference in (1,16,256):
        check(build(f"alias-{reference}",base+["--native-depth-float-alias",
                    "--native-depth-float-reference",str(reference)]))
        scalar += 192
        check(build(f"reuse-{reference}",base+["--native-depth-float-alias",
                    "--native-depth-float-reuse","--native-depth-float-reference",str(reference)]))
        scalar += 192
    for reference in (1,16,256,400):
        check(build(f"alias-ramp-{reference}",base+["--native-depth-float-alias",
                    "--native-depth-float-alias-ramp","--native-depth-float-reference",str(reference)]))
        scalar += 192
    check(build("reuse-ramp-400",base+["--native-depth-float-alias",
                "--native-depth-float-alias-ramp","--native-depth-float-reuse",
                "--native-depth-float-reference","400"]))
    scalar += 192
    for kind,extra in (("fixed-depth",["--native-depth"]),("2d",[]),
                        ("cube",["--native-cube-face","4"]),("volume",["--native-volume"])):
        compare(build(f"control-{kind}",["--mixed","--native-color","rgba8",
                      "--lod-bias","1.25",*extra]))
    compare(build("control-g8b8",["--mixed","--lod-bias","1.25"]))
    print(f"PASS: {scalar} float D16 scalar cases and {controls} exact parent pixels per API")


if __name__ == "__main__":
    main()
