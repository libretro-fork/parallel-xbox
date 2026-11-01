#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check mixed base-level color cube filtering, face selection and mip isolation."""
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
    root = Path(tempfile.mkdtemp(prefix="cube-color-", dir=args.scratch.resolve()))
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
                       "--opt", "xemu_force_quality_filtering=disabled"]
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
    def options(format, face, bias=1.25, filter=None, source="single", matching=False):
        result=["--mixed","--native-color",format,"--native-cube-face",str(face),
                "--lod-bias",str(bias)]
        if filter:
            result += ["--native-mip-filter",str(filter)]
        if source != "single":
            result.append("--native-mip-chain")
        if source == "guest":
            result.append("--native-base-only")
        if matching:
            result.append("--native-matching")
        return result

    for format in ("rgba8","rgb565"):
        for face in range(6):
            check(build(f"face-{format}-{face}", options(format,face)))
            scalar += 192
            check(build(f"forceface-{format}-{face}",
                        options(format,face,4,6,"force")),True)
            scalar += 192
        for bias in (-4,0,1):
            check(build(f"boundary-{format}-{bias}", options(format,4,bias)))
            scalar += 192
        for filter in (3,4,5,6):
            for source in ("single","guest","force"):
                check(build(f"{source}-{format}-{filter}",
                            options(format,4,1.25,filter,source)),source=="force")
                scalar += 192
            compare(build(f"full-{format}-{filter}",
                          options(format,4,1.25,filter,"full")))
            controls += 192
        for face in (1,4):
            compare(build(f"matching-{format}-{face}",
                          options(format,face,4,source="full",matching=True)))
            controls += 192
        compare(build(f"lower-{format}",
                      [*options(format,4,4,6,"full"),"--min-lod","1"]))
        controls += 192
        compare(build(f"2d-{format}",
                      ["--mixed","--native-color",format,"--native-mip-filter","6",
                       "--native-mip-chain","--lod-bias","1.25"]),True)
        controls += 192
    regressions=[
        ["--mixed","--lod-bias","0.25"],
        ["--mip-filter","4","--mip-mixed","--single-level","--mip-address-u","border",
         "--mip-address-v","border","--lod-bias","0.25"],
        ["--mip-filter","6","--mip-mixed","--base-only","--mip-address-u","border",
         "--mip-address-v","wrap","--lod-bias","0.25"],
    ]
    for i,opts in enumerate(regressions):
        compare(build(f"g8b8-{i}",opts))
        controls += 192
    print(f"PASS: {scalar} cube scalar cases and {controls} exact parent pixels "
          f"per renderer; logs in {root}")


if __name__ == "__main__":
    main()
