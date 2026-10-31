#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check GL/Vulkan probe logs against a signed-before-filter scalar model.

This verifies the implemented model, not bit-exact NV2A behavior. Run the probe
with default filtering options; save gl.log and vk.log beside cases.csv.
"""
import argparse
import csv
import math
from pathlib import Path
import re


def signed_byte(value):
    return (value - 256 if value >= 128 else value) / 127


def mix(a, b, weight):
    return a * (1 - weight) + b * weight


def expected_mip(case, force_lod0):
    mode = int(case["mip_filter"])
    linear = mode in (4, 6)
    lod = (-2, 0, 1)[("magnified", "near_magnified", "minified").index(case["coordinate"])]
    computed_lod = lod + float(case["lod_bias"])
    if case["mode"] == "mixed_mip_color":
        linear = linear if computed_lod > 0 else int(case["mag_filter"]) == 2
    maximum = 1 if (int(case["id"]) // 3) % 2 else 2
    lod = 0 if force_lod0 else min(maximum, max(0, lod + float(case["lod_bias"])))
    if mode in (3, 4):
        lod = math.floor(lod + 0.5)

    def sample(level, channel):
        size = 4 >> level
        u = 1.25 * size / 4
        pair = ((192, 64), (224, 32), (16, 16))[level] if channel in "RB" else ((64, 192), (96, 160), (112, 112))[level]
        position = u - 0.5 if linear else u
        index = math.floor(position)
        weight = position - index if linear else 0
        values = [pair[(index + offset) % size % 2] for offset in (0, 1)]
        if channel in case["signed_channels"]:
            values = [signed_byte(v) * 255 for v in values]
        return mix(*values, weight)

    return [min(255, max(0, math.floor(mix(sample(math.floor(lod), channel),
                                         sample(math.ceil(lod), channel), lod % 1) + 0.5)))
            for channel in "ARGB"]


def expected(case, force_lod0=False):
    if case["mode"] in ("mip_color", "mixed_mip_color"):
        return expected_mip(case, force_lod0)
    mixed = case["mode"] == "mixed_color"
    if mixed:
        lod = {"magnified": -3, "near_magnified": -1, "minified": 2}[case["coordinate"]]
        minified = lod + float(case.get("lod_bias", 0)) > 0
        linear = case["filter"] == ("min_linear_mag_point" if minified else "min_point_mag_linear")
        weight = 0.75 if linear else 1
    else:
        coordinate = ("left_center", "midpoint", "right_center").index(case["coordinate"])
        linear = case["filter"] == "linear"
        weight = coordinate / 2 if linear else int(coordinate != 0)
    signs = case["signed_channels"]
    if case["mode"] in ("color", "mixed_color"):
        result = []
        for channel in "ARGB":
            pair = ((192, 64) if mixed else (255, 1)) if channel in "RB" else (64, 192)
            if channel in signs:
                value = mix(*(signed_byte(v) for v in pair), weight) * 255
            else:
                value = mix(*pair, weight)
            result.append(min(255, max(0, math.floor(value + 0.5))))
        return result
    if linear and case["axis"] in signs:
        displacement = mix(signed_byte(255), signed_byte(1), weight)
    else:
        displacement = signed_byte(mix(255, 1, weight))
    u = 16 + (8 * displacement if case["axis"] == "B" else 0)
    v = 8 + (8 * displacement if case["axis"] == "G" else 0)
    red = min(248, max(0, (u - 0.5) * 8))
    green = min(240, max(0, (v - 0.5) * 16))
    return [255, math.floor(red + 0.5), math.floor(green + 0.5), 0]


def read_log(path):
    values = {}
    for address, value in re.findall(r"^peek_0x([0-9a-f]+)=0x([0-9a-f]+)$",
                                   path.read_text(), re.MULTILINE):
        address = int(address, 16)
        if address in values:
            raise ValueError(f"duplicate result address in {path}: {address:#x}")
        values[address] = int(value, 16)
    if values.get(0x300100) != 192:
        raise ValueError(f"incomplete probe: {path}")
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--force-lod0", action="store_true")
    args = parser.parse_args()
    with (args.directory / "cases.csv").open(newline="") as file:
        cases = list(csv.DictReader(file))
    if len(cases) != 192 or {int(c["id"]) for c in cases} != set(range(192)):
        raise ValueError("manifest must contain exactly cases 0 through 191")
    logs = {api: read_log(args.directory / f"{api}.log") for api in ("gl", "vk")}
    for case in cases:
        address = int(case["result_address"], 16)
        reference = expected(case, args.force_lod0)
        # Native LOD approximation can move a cross-mip color by two LSBs.
        mip = case["mode"] in ("mip_color", "mixed_mip_color")
        tolerance = 2 if mip and int(case["mip_filter"]) in (5, 6) else 1
        pixels = []
        for api, values in logs.items():
            pixel = values[address]
            pixels.append(pixel)
            actual = [(pixel >> shift) & 255 for shift in (24, 16, 8, 0)]
            # Native sampler interpolation precision can move a color by one LSB.
            if any(abs(a - b) > tolerance for a, b in zip(actual, reference)):
                raise AssertionError(f"{api} case {case['id']}: {actual}, expected {reference}")
        backend_tolerance = tolerance if mip else 0
        if any(abs((pixels[0] >> shift & 255) - (pixels[1] >> shift & 255)) > backend_tolerance
               for shift in (24, 16, 8, 0)):
            raise AssertionError(f"backend mismatch in case {case['id']}: {pixels}")
    print(f"PASS: {len(cases)} cases match the scalar model and both backends agree within tolerance")


if __name__ == "__main__":
    main()
