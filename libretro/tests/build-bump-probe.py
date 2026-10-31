#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build an emulator-only observation ROM; never flash it on a console."""
import argparse
import csv
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="new or empty scratch directory")
    parser.add_argument("--assembler", default="nasm", help="nasm or yasm")
    parser.add_argument("--color", action="store_true", help="test all signed color-channel combinations")
    parser.add_argument("--mixed", action="store_true", help="test mixed filters at varying footprints")
    parser.add_argument("--luminance-profile", choices=("scale_bias", "negative_scale", "overbright"),
                        help="test non-neutral bump luminance with every channel-sign mask")
    parser.add_argument("--mip-filter", type=int, choices=(3, 4, 5, 6),
                        help="test a three-level swizzled G8B8 mip chain")
    parser.add_argument("--mip-mixed", action="store_true",
                        help="use the opposite magnification filter with --mip-filter")
    parser.add_argument("--mip-clamp", action="store_true",
                        help="test clamp-to-edge on both axes with --mip-filter")
    parser.add_argument("--mip-clamp-axis", choices=("u", "v"),
                        help="clamp only this axis and wrap the other with --mip-filter")
    parser.add_argument("--mip-address-u", choices=("wrap", "edge", "mirror"))
    parser.add_argument("--mip-address-v", choices=("wrap", "edge", "mirror"))
    parser.add_argument("--min-lod", type=int, choices=(0, 1, 2), default=0,
                        help="guest lower mip clamp with --mip-filter")
    parser.add_argument("--sampler-transition", choices=("lod0-to-mip", "mip-to-lod0"),
                        help="prime the same image with the other filter class before drawing")
    parser.add_argument("--lod-bias", type=float, choices=(-4, 0, 0.25, 4), default=0,
                        help="sampler LOD bias for the mixed/mip-filter matrix")
    args = parser.parse_args()
    if args.luminance_profile and (args.color or args.mixed or args.mip_filter):
        parser.error("--luminance-profile is a separate bump fixture")
    if args.min_lod and not args.mip_filter:
        parser.error("--min-lod requires --mip-filter")
    if args.sampler_transition and not args.mip_filter:
        parser.error("--sampler-transition requires --mip-filter")
    if args.mixed and args.mip_filter:
        parser.error("--mixed and --mip-filter are mutually exclusive")
    if args.mip_mixed and not args.mip_filter:
        parser.error("--mip-mixed requires --mip-filter")
    explicit_address = args.mip_address_u is not None or args.mip_address_v is not None
    if explicit_address and (args.mip_clamp or args.mip_clamp_axis):
        parser.error("explicit addresses cannot be combined with mip clamp options")
    axis_data = args.mip_clamp or args.mip_clamp_axis is not None or explicit_address
    if axis_data and not args.mip_filter:
        parser.error("mip address options require --mip-filter")
    if args.mip_clamp and args.mip_clamp_axis:
        parser.error("choose --mip-clamp or --mip-clamp-axis")
    address_mode = "clamp_edge" if args.mip_clamp else (
        "clamp_" + args.mip_clamp_axis if args.mip_clamp_axis else "wrap")
    address_u, address_v = args.mip_address_u or "wrap", args.mip_address_v or "wrap"
    if explicit_address:
        address_mode = f"axes_{address_u}_{address_v}"
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error("output directory must be empty")
    for tool in ("gcc", "ld", "nm", "objcopy", args.assembler):
        if not shutil.which(tool):
            parser.error(f"missing build tool: {tool}")
    rom = Path(__file__).resolve().parent / "rom"
    system = output / "system" / "xemu"
    system.mkdir(parents=True)
    with tempfile.TemporaryDirectory(prefix="bump-probe-") as folder:
        folder = Path(folder)
        obj, elf, binary = [folder / name for name in ("gpu.o", "gpu.elf", "gpu.bin")]
        defines = ["-DBUMP_FILTER_PROBE"]
        if args.luminance_profile:
            scale, offset = {"scale_bias": (0x3f000000, 0x3e800000),
                             "negative_scale": (0xbf000000, 0x3f400000),
                             "overbright": (0x40400000, 0xbf000000)}[args.luminance_profile]
            defines += ["-DBUMP_FILTER_LUMINANCE_PROBE", f"-DBUMP_LUM_SCALE={scale}",
                        f"-DBUMP_LUM_OFFSET={offset}"]
        if args.color or args.mixed or args.mip_filter:
            defines.append("-DBUMP_FILTER_COLOR_PROBE")
        if args.mixed:
            defines.append("-DBUMP_FILTER_MIXED_PROBE")
            defines.append(f"-DBUMP_MIXED_BIAS={int(args.lod_bias * 256) & 0x1fff}")
        if args.mip_filter:
            if args.sampler_transition:
                defines.append("-DBUMP_SAMPLER_TRANSITION")
                if args.sampler_transition == "lod0-to-mip":
                    defines.append("-DBUMP_PRIME_LOD0")
            mag = (2 if args.mip_filter % 2 else 1) if args.mip_mixed else (1 if args.mip_filter % 2 else 2)
            defines += ["-DBUMP_FILTER_MIP_PROBE", f"-DBUMP_MIP_FILTER={args.mip_filter}",
                        f"-DBUMP_MIN_LOD={args.min_lod}",
                        f"-DBUMP_MIP_MAG={mag}",
                        f"-DBUMP_MIXED_BIAS={int(args.lod_bias * 256) & 0x1fff}"]
            if axis_data:
                modes = {"wrap": 1, "mirror": 2, "edge": 3}
                address = (0x10000 | modes[address_v] << 8 | modes[address_u]) if explicit_address else {
                    "clamp_edge": 0x30303, "clamp_u": 0x10103, "clamp_v": 0x10301}[address_mode]
                defines += ["-DBUMP_FILTER_CLAMP_PROBE", f"-DBUMP_CLAMP_ADDRESS={address}"]
        subprocess.run(["gcc", "-m32", "-ffreestanding", "-fno-pic", "-fno-pie",
                        "-fno-stack-protector", "-fno-asynchronous-unwind-tables",
                        "-mno-sse", "-mno-mmx", "-O2", "-Wall", "-Wextra", "-Werror",
                        *defines, "-c", str(rom / "gpu.c"),
                        "-o", str(obj)], check=True)
        subprocess.run(["ld", "-m", "elf_i386", "-Ttext=0xFFF10000", "-e", "gpu_main",
                        "-o", str(elf), str(obj)], check=True)
        symbols = subprocess.check_output(["nm", str(elf)], text=True)
        entry = next(line.split()[0] for line in symbols.splitlines()
                     if line.split()[-1] == "gpu_main")
        subprocess.run(["objcopy", "-O", "binary", "-j", ".text", "-j", ".rodata",
                        "-j", ".data", str(elf), str(binary)], check=True)
        data = binary.read_bytes()
        if len(data) >= 0xEFFF0:
            raise RuntimeError("probe exceeds the flash code area")
        include = ["%define GPU_BASE 0xFFF10000", f"%define GPU_ENTRY 0x{entry}",
                   "%macro gpu_bytes 0"]
        for start in range(0, len(data), 16):
            include.append("db " + ", ".join(f"0x{x:02x}" for x in data[start:start + 16]))
        include.append("%endmacro")
        (folder / "gpu.inc").write_text("\n".join(include) + "\n")
        # Some assemblers search the input file's directory before -I paths.
        # Keep flash.asm beside our generated include, not the stock gpu.inc.
        shutil.copyfile(rom / "flash.asm", folder / "flash.asm")
        subprocess.run([args.assembler, "-f", "bin", "-DGPU", "-I", str(folder) + "/",
                        "-I", str(rom) + "/", "-o", str(system / "Complex_4627v1.03.bin"),
                        str(folder / "flash.asm")], check=True, cwd=folder)
        subprocess.run([args.assembler, "-f", "bin", "-o", str(system / "mcpx_1.0.bin"),
                        str(rom / "bootrom.asm")], check=True)
    with (output / "cases.csv").open("w", newline="") as file:
        writer = csv.writer(file)
        writer.writerow(["id", "layout", "mode", "axis", "signed_channels", "filter",
                         "coordinate", "result_address", "lod_bias", "mip_filter",
                         "max_lod", "wrap_period", "mag_filter", "address_mode",
                         "sample_u", "sample_v", "min_lod", "sampler_transition"])
        for i in range(192):
            if args.luminance_profile:
                flags = (i // 6) % 16
                channels = "".join(ch for bit, ch in ((1, "B"), (2, "G"), (4, "R"), (8, "A")) if flags & bit) or "none"
                writer.writerow([i, ("linear", "swizzled")[i // 96],
                                 "bump_luminance_" + args.luminance_profile, "B", channels,
                                 ("point", "linear")[(i // 3) % 2],
                                 ("left_center", "midpoint", "right_center")[i % 3],
                                 f"0x{0x300204 + i * 8:08x}", 0])
                continue
            if args.color or args.mixed or args.mip_filter:
                flags = (i // 6) % 16
                if axis_data:
                    flags = (i // 3) % 16
                channels = "".join(ch for bit, ch in ((1, "B"), (2, "G"), (4, "R"), (8, "A")) if flags & bit) or "none"
                if args.mip_filter:
                    if axis_data:
                        u, v = ((1.25, 1.25), (-0.25, -0.25),
                                (4.25, 4.25), (-0.25, 4.25))[i // 48]
                        maximum = 1 if flags & 1 else 2
                    else:
                        u, v = 1.25 - 4 * (i // 96), 0
                        maximum = 1 if (i // 3) % 2 else 2
                    writer.writerow([i, "swizzled", "mixed_mip_color" if args.mip_mixed else "mip_color", "BG", channels,
                                     {3: "point_nearest_mip", 4: "linear_nearest_mip",
                                      5: "point_linear_mip", 6: "linear_linear_mip"}[args.mip_filter],
                                     ("magnified", "near_magnified", "minified")[i % 3],
                                     f"0x{0x300204 + i * 8:08x}", args.lod_bias,
                                     args.mip_filter, maximum, 0 if axis_data else -(i // 96), mag,
                                     address_mode, u, v, args.min_lod, args.sampler_transition or ""])
                    continue
                writer.writerow([i, ("linear", "swizzled")[i // 96],
                                 "mixed_color" if args.mixed else "color", "BG", channels,
                                 (("min_point_mag_linear", "min_linear_mag_point") if args.mixed
                                  else ("point", "linear"))[(i // 3) % 2],
                                 (("magnified", "near_magnified", "minified") if args.mixed
                                  else ("left_center", "midpoint", "right_center"))[i % 3],
                                 f"0x{0x300204 + i * 8:08x}", args.lod_bias if args.mixed else 0, 0])
                continue
            writer.writerow([i, ("linear", "swizzled")[i // 96],
                             ("bump", "bump_luminance")[(i // 48) % 2],
                             ("B", "G")[(i // 24) % 2],
                             ("none", "B", "G", "BG")[(i // 6) % 4],
                             ("point", "linear")[(i // 3) % 2],
                             ("left_center", "midpoint", "right_center")[i % 3],
                             f"0x{0x300204 + i * 8:08x}", 0])
    (output / "peek-args.txt").write_text("\n".join(
        ["--peek", "0x300100"] +
        [arg for i in range(192) for arg in ("--peek", f"0x{0x300204 + i * 8:x}")]) + "\n")
    print(f"Built {len(data)} bytes of probe code; 192 cases in {output}")


if __name__ == "__main__":
    main()
