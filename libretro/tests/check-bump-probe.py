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


def expected(case):
    coordinate = ("left_center", "midpoint", "right_center").index(case["coordinate"])
    linear = case["filter"] == "linear"
    weight = coordinate / 2 if linear else int(coordinate != 0)
    signs = case["signed_channels"]
    if case["mode"] == "color":
        result = []
        for channel in "ARGB":
            pair = (255, 1) if channel in "RB" else (64, 192)
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
    args = parser.parse_args()
    with (args.directory / "cases.csv").open(newline="") as file:
        cases = list(csv.DictReader(file))
    if len(cases) != 192 or {int(c["id"]) for c in cases} != set(range(192)):
        raise ValueError("manifest must contain exactly cases 0 through 191")
    logs = {api: read_log(args.directory / f"{api}.log") for api in ("gl", "vk")}
    for case in cases:
        address = int(case["result_address"], 16)
        reference = expected(case)
        pixels = []
        for api, values in logs.items():
            pixel = values[address]
            pixels.append(pixel)
            actual = [(pixel >> shift) & 255 for shift in (24, 16, 8, 0)]
            # Native sampler interpolation precision can move a color by one LSB.
            if any(abs(a - b) > 1 for a, b in zip(actual, reference)):
                raise AssertionError(f"{api} case {case['id']}: {actual}, expected {reference}")
        if pixels[0] != pixels[1]:
            raise AssertionError(f"backend mismatch in case {case['id']}: {pixels}")
    print(f"PASS: {len(cases)} cases match the scalar model and both backends agree")


if __name__ == "__main__":
    main()
