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
    parser.add_argument("--extended-address-biases", action="store_true",
                        help="repeat new mixed-axis matrices at -4/0/+0.25/+4 instead of +0.25")
    parser.add_argument("--extended-mip-minima", action="store_true",
                        help="also exercise minimum mip levels five and six on larger textures")
    args = parser.parse_args()
    here = args.checkout.resolve() / "libretro/tests"
    args.scratch.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="g8b8-", dir=args.scratch.resolve()))
    env = dict(os.environ)
    env.setdefault("EGL_PLATFORM", "surfaceless")
    modes = ["bump", "color", "mixed", "mixed_plus4", "mixed_minus4"]
    modes += [f"luminance_{profile}" for profile in ("scale_bias", "negative_scale", "overbright")]
    modes += [f"mip_{filter}_{bias}" for filter in (3, 4, 5, 6)
              for bias in ("-4", "0", "0.25", "4")]
    modes += [f"mipmixed_{filter}_{bias}" for filter in (3, 4, 5, 6)
              for bias in ("-4", "0", "0.25", "4")]
    modes += [f"{family}_{filter}_{bias}" for family in ("mipclamp", "mipclampmixed")
              for filter in (3, 4, 5, 6) for bias in ("-4", "0", "0.25", "4")]
    modes += [f"{family}_{filter}_{bias}" for family in
              ("mipclampu", "mipclampv", "mipclampumixed", "mipclampvmixed")
              for filter in (3, 4, 5, 6) for bias in
              (("-4", "0", "0.25", "4") if args.extended_address_biases else ("0.25",))]
    modes += [f"mipaddr{'mixed' if mixed else ''}-{u}-{v}_{filter}_{bias}"
              for u, v in (("mirror", "wrap"), ("wrap", "mirror"),
                           ("mirror", "mirror"), ("mirror", "edge"), ("edge", "mirror"))
              for mixed in (False, True) for filter in (3, 4, 5, 6)
              for bias in (("-4", "0", "0.25", "4") if args.extended_address_biases else ("0.25",))]
    lower = [(1, u, v, 6) for u in ("wrap", "edge", "mirror")
             for v in ("wrap", "edge", "mirror")]
    lower += [(1, "wrap", "wrap", f) for f in (3, 4, 5)]
    lower += [(2, "wrap", "wrap", f) for f in (3, 4, 5, 6)]
    modes += [f"miplower-{level}-{u}-{v}_{filter}_0.25" for level, u, v, filter in lower]
    modes += [f"miplowermixed-{level}-{u}-{v}_{filter}_{bias}"
              for level, u, v, filter in lower for bias in
              (("-4", "0", "0.25", "4") if args.extended_address_biases else ("0.25",))]
    third = [(True, u, v, 6) for u in ("wrap", "edge", "mirror")
             for v in ("wrap", "edge", "mirror")]
    third += [(False, "wrap", "wrap", f) for f in (3, 4, 5, 6)]
    third += [(True, "wrap", "wrap", f) for f in (3, 4, 5)]
    modes += [f"mipthree{'mixed' if mixed else ''}-{u}-{v}_{filter}_{bias}"
              for mixed, u, v, filter in third for bias in
              (("-4", "0", "0.25", "4") if args.extended_address_biases else ("0.25",))]
    if not args.extended_address_biases:
        modes += [f"mipthreemixed-wrap-wrap_{f}_-4" for f in (3, 6)]
    modes += [f"miphigher{'mixed' if mixed else ''}-{u}-{v}_{filter}_{bias}"
              for mixed, u, v, filter in third for bias in
              (("-4", "0", "0.25", "4") if args.extended_address_biases else ("0.25",))]
    if not args.extended_address_biases:
        modes += [f"miphighermixed-wrap-wrap_{f}_-4" for f in (3, 6)]
    if args.extended_mip_minima:
        modes += [f"mipdeep{'mixed' if mixed else ''}-{level}-{u}-{v}_{filter}_{bias}"
                  for level in (5, 6) for mixed, u, v, filter in third for bias in
                  (("-4", "0", "0.25", "4") if args.extended_address_biases else ("0.25",))]
        if not args.extended_address_biases:
            modes += [f"mipdeepmixed-{level}-wrap-wrap_{f}_-4"
                      for level in (5, 6) for f in (3, 6)]
    for mode in modes:
        output = root / mode
        build = [sys.executable, str(here / "build-bump-probe.py"), str(output),
                 "--assembler", args.assembler]
        if mode == "color":
            build.append("--color")
        elif mode.startswith("luminance_"):
            build += ["--luminance-profile", mode.split("_", 1)[1]]
        elif mode.startswith("mixed"):
            bias = 4 if mode.endswith("plus4") else -4 if mode.endswith("minus4") else 0
            build += ["--mixed", "--lod-bias", str(bias)]
        elif mode.startswith(("mip_", "mipmixed_", "mipclamp", "mipaddr", "miplower", "mipthree", "miphigher", "mipdeep")):
            family, filter, bias = mode.split("_")
            build += ["--mip-filter", filter, "--lod-bias", bias]
            if "mixed" in family:
                build.append("--mip-mixed")
            if family.startswith("mipdeep"):
                _, level, u, v = family.split("-")
                build += ["--min-lod", level, "--mip-base-size", str(1 << (int(level) + 1)),
                          "--mip-address-u", u, "--mip-address-v", v,
                          "--sampler-transition", "lod0-to-mip"]
            elif family.startswith(("mipthree", "miphigher")):
                _, u, v = family.split("-")
                higher = family.startswith("miphigher")
                build += ["--min-lod", "4" if higher else "3",
                          "--mip-base-size", "32" if higher else "16",
                          "--mip-address-u", u, "--mip-address-v", v,
                          "--sampler-transition", "lod0-to-mip"]
            elif family.startswith("miplower"):
                _, level, u, v = family.split("-")
                build += ["--min-lod", level, "--mip-address-u", u, "--mip-address-v", v,
                          "--sampler-transition", "lod0-to-mip"]
            elif family.startswith("mipaddr"):
                _, u, v = family.split("-")
                build += ["--mip-address-u", u, "--mip-address-v", v]
            elif family.startswith("mipclampu"):
                build += ["--mip-clamp-axis", "u"]
            elif family.startswith("mipclampv"):
                build += ["--mip-clamp-axis", "v"]
            elif family.startswith("mipclamp"):
                build.append("--mip-clamp")
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
                # A fixed-frame oracle must not charge host GPU wait time
                # against the guest work needed to finish all 192 cases.
                command = [str(args.harness.resolve()), str(args.core.resolve()),
                           str(output / "system"), "--frames", "240", "--strict",
                           "--opt", "xemu_deterministic=enabled"]
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
            check = [sys.executable, str(here / "check-bump-probe.py"), str(logs)]
            if quality:
                check.append("--force-lod0")
            subprocess.run(check,
                           check=True)
    print(f"PASS: {len(modes) * 384} guest cases per renderer; logs in {root}")


if __name__ == "__main__":
    main()
