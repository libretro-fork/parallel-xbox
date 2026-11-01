#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check GL/Vulkan probe logs against a signed-before-filter scalar model.

This verifies the implemented model, not bit-exact NV2A behavior. Run the probe
with default filtering options; save gl.log and vk.log beside cases.csv.
"""
import argparse
import csv
import math
import struct
from pathlib import Path
import re


def signed_byte(value):
    return (value - 256 if value >= 128 else value) / 127


def mix(a, b, weight):
    return a * (1 - weight) + b * weight


def expected_mip(case, force_lod0):
    if case.get("sampler_transition") == "mip-to-lod0":
        raise ValueError("nonmip transition controls require per-backend parent comparison")
    mode = int(case["mip_filter"])
    linear = mode in (4, 6)
    base_size = int(case.get("base_size") or 4)
    lod = (-2, 0, 1)[("magnified", "near_magnified", "minified").index(case["coordinate"])]
    lod += math.log2(base_size / 4)
    computed_lod = lod + float(case["lod_bias"])
    maximum = int(case["max_lod"]) if case.get("max_lod") else (
        1 if (int(case["id"]) // 3) % 2 else 2)
    minimum = int(case.get("min_lod") or 0)
    minimum = min(minimum, maximum)
    if case["mode"] == "mixed_mip_color":
        minifying = (minimum > 0 and not force_lod0) or computed_lod > 0
        linear = linear if minifying else int(case["mag_filter"]) == 2
    lod = 0 if force_lod0 else min(maximum, max(minimum, lod + float(case["lod_bias"])))
    if mode in (3, 4):
        lod = math.floor(lod + 0.5)

    def sample(level, channel):
        size = base_size >> level
        address = case.get("address_mode", "wrap")
        axis_v = (address in ("clamp_edge", "clamp_u", "clamp_v") or address.startswith("axes_")) and channel in "GA"
        clamp = address == "clamp_edge" or (address == "clamp_v" if axis_v else address == "clamp_u")
        mode = address.split("_")[2 if axis_v else 1] if address.startswith("axes_") else (
            "edge" if clamp else "wrap")
        coordinate = "sample_v" if axis_v else "sample_u"
        u = float(case.get(coordinate) or "1.25") * size / 4
        if base_size >= 16:
            last = base_size.bit_length() - 1
            if channel in "RB":
                pairs = ((192, 64), (224, 32), (16, 240), (208, 48),
                         (224, 32), (192, 64), (208, 48))
                pair = (pairs[:last] + ((16, 16),))[level]
            else:
                pairs = ((64, 192), (96, 160), (112, 144), (80, 176),
                         (96, 160), (64, 192), (80, 176))
                pair = (pairs[:last] + ((112, 112),))[level]
        else:
            pair = ((192, 64), (224, 32), (16, 16))[level] if channel in "RB" else ((64, 192), (96, 160), (112, 112))[level]
        position = u - 0.5 if linear else u
        index = math.floor(position)
        weight = position - index if linear else 0
        def addressed(i):
            if mode == "border" and not 0 <= i < size:
                return None
            if mode == "edge":
                return min(size - 1, max(0, i))
            if mode == "mirror":
                i %= 2 * size
                return min(i, 2 * size - 1 - i)
            return i % size
        indices = [addressed(index + offset) for offset in (0, 1)]
        # G8B8's R/G image-view swizzle duplicates R into B and G into A,
        # including the native sampler border used by point/gather reads.
        border = int(case.get("border_color") or "0", 0)
        border = (border >> (16 if channel in "RB" else 8)) & 255
        values = [border if i is None else pair[i % 2] for i in indices]
        if channel in case["signed_channels"]:
            values = [signed_byte(v) * 255 for v in values]
            border = signed_byte(border) * 255
        coverage = 1
        if address.startswith("axes_"):
            other_mode = address.split("_")[1 if axis_v else 2]
            if other_mode == "border":
                other = float(case["sample_u" if axis_v else "sample_v"]) * size / 4
                other -= 0.5 if linear else 0
                other_index = math.floor(other)
                other_weight = other - other_index if linear else 0
                coverage = ((1 - other_weight) if 0 <= other_index < size else 0) + (
                    other_weight if 0 <= other_index + 1 < size else 0)
        return mix(*values, weight) * coverage + border * (1 - coverage)

    return [min(255, max(0, math.floor(mix(sample(math.floor(lod), channel),
                                         sample(math.ceil(lod), channel), lod % 1) + 0.5)))
            for channel in "ARGB"]


def expected(case, force_lod0=False):
    if case["mode"] in ("mip_color", "mixed_mip_color"):
        return expected_mip(case, force_lod0)
    mixed = case["mode"] in ("mixed_color", "native_mixed")
    if mixed:
        lod = ((-3,-1,-2)[int(case["id"]) % 3] if case.get("native_volume") == "1" else
               float(case["native_cube_lod"]) if case.get("native_cube_face") else {
            "magnified": -3, "near_magnified": -1, "minified": 2}[case["coordinate"]])
        minified = lod + float(case.get("lod_bias", 0)) > 0
        if case.get("native_min_filter"):
            if (int(case["native_min_filter"]) >= 3 and case["layout"] in ("swizzled","cube","volume") and
                    case.get("native_chain") == "1" and
                    case.get("native_base_only") != "1" and not force_lod0):
                raise ValueError("unclamped native mip controls require parent comparison")
            linear = int(case["native_min_filter"]) in (2,4,6) if minified else int(case["native_mag_filter"]) == 2
        else:
            linear = case["filter"] == ("min_linear_mag_point" if minified else "min_point_mag_linear")
        weight = 0.75 if linear else 1
    else:
        coordinate = ("left_center", "midpoint", "right_center").index(case["coordinate"])
        linear = case["filter"] == "linear"
        weight = coordinate / 2 if linear else int(coordinate != 0)
    if case.get("native_depth_reference"):
        if case.get("native_depth_float") == "1":
            codes=(0,1,0x0fff,0x1000,0x1001,0x3fff,0x4000,0x4001,
                   0x7fff,0x8000,0x8001,0xbfff,0xc000,0xc001,0xfffe,0xffff)
            def decode(code):
                return struct.unpack("<f",struct.pack("<I",(code<<11)+0x3c000000))[0] if code else 0.0
            palette=int(case["native_palette"])
            first=codes[palette]
            second=first if case.get("native_depth_constant") == "1" else codes[15-palette]
            if case.get("native_depth_grid") == "1":
                weight = 0.375 if linear else 0.0
            depth=mix(decode(first),decode(second),weight)
            return [255 if depth > float(case["native_depth_reference"]) else 0] * 4
        depth = mix(0x2000, 0xe000, weight)
        return [255 if depth > int(case["native_depth_reference"]) else 0] * 4
    if case["mode"] in ("native_color", "native_mixed"):
        palette = int(case["native_palette"])
        first = (255, 16 + palette * 11, 32 + palette * 7, 240 - palette * 9)
        second = (255, 224 - palette * 9, 208 - palette * 7, 16 + palette * 11)
        if case.get("native_volume") == "1":
            offset = int(case["native_volume_slice"]) * (14 if case.get("native_chain") == "1" else 16)
            first = (first[0], first[1], first[2] + offset, first[3])
            second = (second[0], second[1], second[2] + offset, second[3])
        if case.get("native_cube_face"):
            offset = int(case["native_cube_face"]) * 5 + int(case["native_cube_variant"]) * 16
            first = (first[0], first[1], first[2] + offset, first[3])
            second = (second[0], second[1], second[2] + offset, second[3])
        if case.get("native_format") == "rgb565":
            def quantized(color):
                return (255, (color[1] >> 3) * 255 / 31,
                        (color[2] >> 2) * 255 / 63, (color[3] >> 3) * 255 / 31)
            first, second = quantized(first), quantized(second)
        return [math.floor(mix(a, b, weight) + 0.5) for a, b in zip(first, second)]
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
    profiles = {"bump_luminance_scale_bias": (0.5, 0.25),
                "bump_luminance_negative_scale": (-0.5, 0.75),
                "bump_luminance_overbright": (3.0, -0.5)}
    if case["mode"] in profiles:
        scale, offset = profiles[case["mode"]]
        # Dependent luminance uses the original unsigned R sample, even if
        # R is signed later when it becomes a combiner input.
        factor = mix(255, 1, weight) / 255 * scale + offset
        return [min(255, max(0, math.floor(value * factor + 0.5)))
                for value in (255, red, green, 0)]
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
        raise ValueError(f"incomplete probe: {path}: completed={values.get(0x300100)!r}, expected=192")
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
        if case.get("native_depth_reference"):
            tolerance = 0 # Shadow comparisons are binary, not interpolated colors.
        pixels = []
        for api, values in logs.items():
            pixel = values[address]
            pixels.append(pixel)
            actual = [(pixel >> shift) & 255 for shift in (24, 16, 8, 0)]
            # Native sampler interpolation precision can move a color by one LSB.
            if any(abs(a - b) > tolerance for a, b in zip(actual, reference)):
                raise AssertionError(f"{api} case {case['id']}: {actual}, expected {reference}")
        # Cube/volume interpolation can round ties differently between APIs while
        # both results remain within the one-LSB scalar bound.
        backend_tolerance = tolerance if mip or case.get("native_cube_face") or case.get("native_volume") == "1" else 0
        if any(abs((pixels[0] >> shift & 255) - (pixels[1] >> shift & 255)) > backend_tolerance
               for shift in (24, 16, 8, 0)):
            raise AssertionError(f"backend mismatch in case {case['id']}: {pixels}")
    print(f"PASS: {len(cases)} cases match the scalar model and both backends agree within tolerance")


if __name__ == "__main__":
    main()
