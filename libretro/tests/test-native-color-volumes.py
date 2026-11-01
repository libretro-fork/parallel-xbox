#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check base-level color volume filtering and independent depth slices."""
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
    parser.add_argument("--mip-chains", action="store_true",
                        help="check stored 8x8x8 mip chains with contrasting lower mips")
    parser.add_argument("--windows-paths", action="store_true",
                        help="run a Windows harness through WSL with translated input paths")
    args = parser.parse_args()
    here = args.checkout.resolve() / "libretro/tests"
    read_log = runpy.run_path(str(here / "check-bump-probe.py"))["read_log"]
    args.scratch.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="volume-color-", dir=args.scratch.resolve()))
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
    for slice in (0,1):
        base=["--mixed","--native-color","rgba8","--native-volume",
              "--native-volume-slice",str(slice)]
        if args.mip_chains:
            base.append("--native-mip-chain")
        for bias in (-4,0,1,1.25,4):
            check(build(f"slice-{slice}-bias-{bias}",base+["--lod-bias",str(bias)]))
            scalar += 192
        for filter in (3,4,5,6):
            for force in (False,True):
                extra = ["--native-base-only"] if args.mip_chains and not force else []
                check(build(f"slice-{slice}-filter-{filter}-force-{force}",
                            base+["--native-mip-filter",str(filter),"--lod-bias","1.25",*extra]),force)
                scalar += 192
            if args.mip_chains:
                compare(build(f"full-{slice}-{filter}",base+
                        ["--native-mip-filter",str(filter),"--lod-bias","1.25"]))
                controls += 192
        compare(build(f"matching-{slice}",base+["--native-matching","--lod-bias","4"]))
        controls += 192
        if args.mip_chains:
            compare(build(f"lower-{slice}",base+["--native-mip-filter","6","--min-lod","1","--lod-bias","4"]))
            compare(build(f"single-{slice}",[x for x in base if x != "--native-mip-chain"]+["--lod-bias","1.25"]))
            controls += 384
    for shape,extra in (("2d",[]),("cube",["--native-cube-face","4"])):
        compare(build(f"control-{shape}",["--mixed","--native-color","rgba8",
                      "--lod-bias","1.25",*extra]))
        controls += 192
    compare(build("control-g8b8",["--mixed","--lod-bias","1.25"]))
    controls += 192
    kind = "mip-chain" if args.mip_chains else "single-level"
    print(f"PASS: {scalar} {kind} volume scalar cases and {controls} exact parent pixels per API")


if __name__ == "__main__":
    main()
