#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare unaffected nonmip G8B8 transitions against a parent core on Linux."""
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
    parser.add_argument("checkout", type=Path)
    parser.add_argument("parent", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("harness", type=Path)
    parser.add_argument("scratch", type=Path)
    parser.add_argument("--assembler", default="nasm")
    args = parser.parse_args()
    here = args.checkout.resolve() / "libretro/tests"
    read_log = runpy.run_path(str(here / "check-bump-probe.py"))["read_log"]
    args.scratch.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="g8b8-nonmip-", dir=args.scratch.resolve()))
    env = dict(os.environ)
    env.setdefault("EGL_PLATFORM", "surfaceless")
    cases = 0
    for lower in (1, 2, 3):
        for filter in (3, 4):
            for mixed in (False, True):
                output = root / f"{lower}-{filter}-{int(mixed)}"
                build = [sys.executable, str(here / "build-bump-probe.py"), str(output),
                         "--assembler", args.assembler, "--mip-filter", str(filter),
                         "--min-lod", str(lower), "--lod-bias", "0.25",
                         "--mip-address-u", "wrap", "--mip-address-v", "wrap",
                         "--sampler-transition", "mip-to-lod0"]
                if mixed:
                    build.append("--mip-mixed")
                if lower == 3:
                    build += ["--mip-base-size", "16"]
                subprocess.run(build, check=True)
                with (output / "cases.csv").open() as file:
                    addresses = [int(c["result_address"], 16) for c in csv.DictReader(file)]
                assert len(addresses) == 192
                peeks = (output / "peek-args.txt").read_text().splitlines()
                for api in ("gl", "vk"):
                    results = []
                    for name, core in (("parent", args.parent), ("candidate", args.candidate)):
                        command = [str(args.harness.resolve()), str(core.resolve()),
                                   str(output / "system"), "--frames", "240", "--strict",
                                   "--opt", f"xemu_renderer={'vulkan' if api == 'vk' else 'opengl'}",
                                   "--opt", "xemu_force_texture_lod0=disabled",
                                   "--opt", "xemu_force_quality_filtering=disabled"]
                        if api == "vk":
                            command.append("--vulkan")
                        command += peeks
                        path = output / f"{name}-{api}.log"
                        with path.open("w") as log:
                            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                                           check=True, timeout=120, env=env)
                        results.append(read_log(path))
                    for address in addresses:
                        assert results[0][address] == results[1][address], (
                            f"{api} nonmip transition {output.name} at {address:#x}: "
                            f"{results[0][address]:#x} -> {results[1][address]:#x}")
                cases += len(addresses)
                print(f"PASS: {output.name} nonmip transition matches parent on both APIs")
    print(f"PASS: {cases} nonmip transition pixels per renderer; logs in {root}")


if __name__ == "__main__":
    main()
