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
    parser.add_argument("--lod-bias", type=int, choices=(-4, 0, 4), default=0,
                        help="sampler LOD bias for the mixed-filter matrix")
    args = parser.parse_args()
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
        if args.color or args.mixed:
            defines.append("-DBUMP_FILTER_COLOR_PROBE")
        if args.mixed:
            defines.append("-DBUMP_FILTER_MIXED_PROBE")
            defines.append(f"-DBUMP_MIXED_BIAS={args.lod_bias * 256 & 0x1fff}")
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
                         "coordinate", "result_address", "lod_bias"])
        for i in range(192):
            if args.color or args.mixed:
                flags = (i // 6) % 16
                channels = "".join(ch for bit, ch in ((1, "B"), (2, "G"), (4, "R"), (8, "A")) if flags & bit) or "none"
                writer.writerow([i, ("linear", "swizzled")[i // 96],
                                 "mixed_color" if args.mixed else "color", "BG", channels,
                                 (("min_point_mag_linear", "min_linear_mag_point") if args.mixed
                                  else ("point", "linear"))[(i // 3) % 2],
                                 (("magnified", "near_magnified", "minified") if args.mixed
                                  else ("left_center", "midpoint", "right_center"))[i % 3],
                                 f"0x{0x300204 + i * 8:08x}", args.lod_bias if args.mixed else 0])
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
