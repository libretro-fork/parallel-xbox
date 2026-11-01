#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check fixed D16 filtering before shadow comparison at mip zero."""
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
    root = Path(tempfile.mkdtemp(prefix="fixed-depth-", dir=args.scratch.resolve()))
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
                linear_fixed_depth = (case.get("native_depth_reference") and
                    case["layout"] == "linear" and case.get("native_chain") == "1" and
                    int(case.get("native_min_filter") or 0) >= 3)
                if linear_fixed_depth:
                    reference = expected(case, force)
                    pixel = b[address]
                    assert [(pixel >> s) & 255 for s in (24,16,8,0)] == reference, (output.name,api,case["id"])
                else:
                    assert a[address] == b[address], (output.name, api, case["id"])
        changed = sum(bool(case.get("native_depth_reference")) and case["layout"] == "linear"
                      and case.get("native_chain") == "1" and int(case.get("native_min_filter") or 0) >= 3
                      for case in cases)
        scalar += changed
        controls += len(cases) - changed

    scalar = controls = 0
    base=["--mixed","--native-color","rgba8","--native-depth"]
    for reference in (0x1000,0x4000,0x8000,0xc000,0xf000):
        for bias in (-4,0,1.25,4):
            check(build(f"ref-{reference}-bias-{bias}",base+
                        ["--native-depth-reference",str(reference),"--lod-bias",str(bias)]))
            scalar += 192
    for filter in (3,4,5,6):
        for source in ("single","guest","force"):
            extra=[] if source=="single" else ["--native-mip-chain"]
            if source=="guest":extra.append("--native-base-only")
            check(build(f"filter-{filter}-{source}",base+
                        ["--native-mip-filter",str(filter),"--lod-bias","1.25",*extra]),source=="force")
            scalar += 192
        compare(build(f"full-{filter}",base+["--native-mip-filter",str(filter),
                      "--native-mip-chain","--lod-bias","1.25"]))
    for chain in (False,True):
        compare(build(f"matching-{chain}",base+["--native-matching","--lod-bias","4"]+
                      (["--native-mip-chain"] if chain else [])))
    compare(build("lower",base+["--native-mip-filter","6","--native-mip-chain",
                  "--min-lod","1","--lod-bias","4"]))
    for shape,extra in (("2d",[]),("cube",["--native-cube-face","4"]),
                        ("volume",["--native-volume"])):
        compare(build(f"control-{shape}",["--mixed","--native-color","rgba8",
                      "--lod-bias","1.25",*extra]))
    compare(build("control-g8b8",["--mixed","--lod-bias","1.25"]))
    print(f"PASS: {scalar} fixed D16 scalar cases and {controls} exact parent pixels per API")


if __name__ == "__main__":
    main()
