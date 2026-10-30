#!/usr/bin/env python3
"""Copy an Xbox game partition, optionally clearing its runtime disc check.

The input is always read-only. Existing output files are never overwritten.
"""
import argparse
from pathlib import Path
import shutil
import struct


def read_at(stream, offset, size):
    stream.seek(offset)
    value = stream.read(size)
    if len(value) != size:
        raise ValueError("truncated Xbox image")
    return value


def u32(value, offset):
    return struct.unpack_from("<I", value, offset)[0]


def inspect(stream, disable_check):
    for base in (0, 405798912):
        stream.seek(base + 65536)
        header = stream.read(32)
        if header[:20] == b"MICROSOFT*XBOX*MEDIA":
            break
    else:
        raise ValueError("Xbox filesystem not found at a supported partition offset")
    if len(header) != 32:
        raise ValueError("truncated Xbox volume descriptor")
    if not disable_check:
        return base, None, None
    size = u32(header, 24)
    if not 14 <= size <= 8 * 1024 * 1024:
        raise ValueError("invalid root directory size")
    table = read_at(stream, base + u32(header, 20) * 2048, size)
    pending, seen = [0], set()
    while pending:
        offset = pending.pop()
        if offset in seen:
            raise ValueError("cyclic root directory")
        seen.add(offset)
        if offset + 14 > len(table):
            raise ValueError("root directory entry out of bounds")
        left, right = struct.unpack_from("<HH", table, offset)
        pending.extend(child * 4 for child in (left, right) if child)
        length = table[offset + 13]
        if offset + 14 + length > len(table):
            raise ValueError("root directory filename out of bounds")
        if table[offset + 14:offset + 14 + length].lower() != b"default.xbe":
            continue
        xbe_offset = u32(table, offset + 4) * 2048
        xbe_size = u32(table, offset + 8)
        if xbe_size < 0x178:
            raise ValueError("truncated default.xbe")
        xbe = read_at(stream, base + xbe_offset, 0x178)
        if xbe[:4] != b"XBEH":
            raise ValueError("invalid default.xbe signature")
        cert_offset = u32(xbe, 0x118) - u32(xbe, 0x104)
        if not 0x178 <= cert_offset <= min(u32(xbe, 0x108), xbe_size) - 4:
            raise ValueError("XBE certificate out of bounds")
        cert_size = u32(read_at(stream, base + xbe_offset + cert_offset, 4), 0)
        if cert_size < 0x1dc:
            raise ValueError("default.xbe has no runtime security flags")
        if cert_offset + cert_size > min(u32(xbe, 0x108), xbe_size):
            raise ValueError("XBE certificate extends beyond its headers")
        flag_offset = xbe_offset + cert_offset + 0x1d8
        flags = u32(read_at(stream, base + flag_offset, 4), 0)
        return base, flag_offset, flags
    raise ValueError("default.xbe not found")


def prepare(source, target, disable_check=False):
    with Path(source).open("rb") as src:
        base, offset, flags = inspect(src, disable_check)
        # Exclusive creation also rejects the input path and hard-link aliases.
        with Path(target).open("xb") as dst:
            src.seek(base)
            shutil.copyfileobj(src, dst, 8 * 1024 * 1024)
            if offset is not None:
                dst.seek(offset)
                dst.write(struct.pack("<I", flags & ~1))
    return base, flags


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--disable-runtime-disc-check", action="store_true",
                        help="clear default.xbe's physical-disc check flag in the copy (tested with Otogi and Complex 4627)")
    args = parser.parse_args()
    try:
        base, flags = prepare(args.input, args.output, args.disable_runtime_disc_check)
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")
    print(f"Copied Xbox partition at offset {base} to {args.output}")
    if flags is not None:
        print(f"default.xbe runtime security flags: {flags:#x} -> {flags & ~1:#x}")


if __name__ == "__main__":
    main()
