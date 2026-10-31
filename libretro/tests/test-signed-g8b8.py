#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run isolated guest G8B8 bump/color regressions on Linux GL and Vulkan."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkout", type=Path)
    parser.add_argument("core", type=Path)
    parser.add_argument("harness", type=Path)
    parser.add_argument("scratch", type=Path)
    parser.add_argument("--assembler", default="nasm")
    args = parser.parse_args()
    here = args.checkout.resolve() / "libretro/tests"
    args.scratch.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="g8b8-", dir=args.scratch.resolve()))
    env = dict(os.environ)
    env.setdefault("EGL_PLATFORM", "surfaceless")
    for mode in ("bump", "color", "mixed", "mixed_plus4", "mixed_minus4"):
        output = root / mode
        build = [sys.executable, str(here / "build-bump-probe.py"), str(output),
                 "--assembler", args.assembler]
        if mode == "color":
            build.append("--color")
        elif mode.startswith("mixed"):
            bias = 4 if mode.endswith("plus4") else -4 if mode.endswith("minus4") else 0
            build += ["--mixed", "--lod-bias", str(bias)]
        subprocess.run(build, check=True)
        with (output / "cases.csv").open() as file:
            # The manifest is checked in full by check-bump-probe.py.
            assert sum(1 for _ in file) == 193
        peeks = (output / "peek-args.txt").read_text().splitlines()
        for quality in (False, True):
            logs = output / "quality" if quality else output
            if quality:
                logs.mkdir()
                (logs / "cases.csv").write_bytes((output / "cases.csv").read_bytes())
            for api in ("gl", "vk"):
                command = [str(args.harness.resolve()), str(args.core.resolve()),
                           str(output / "system"), "--frames", "240", "--strict"]
                command += ["--opt", f"xemu_renderer={'vulkan' if api == 'vk' else 'opengl'}"]
                value = "enabled" if quality else "disabled"
                command += ["--opt", f"xemu_force_quality_filtering={value}",
                            "--opt", f"xemu_force_texture_lod0={value}"]
                if api == "vk":
                    command.append("--vulkan")
                command += peeks
                with (logs / f"{api}.log").open("w") as log:
                    subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                                   timeout=120, check=True, env=env)
            subprocess.run([sys.executable, str(here / "check-bump-probe.py"), str(logs)],
                           check=True)
    print(f"PASS: 1920 guest cases per renderer; logs in {root}")


if __name__ == "__main__":
    main()
