#!/usr/bin/env python3
"""Compare opcode metadata in old/new Linux x86-64 core binaries.

Reads the linked table, rather than reconstructing it from source macros.
The old table has 16-byte entries with an 8-byte name pointer; the compact
table has 8-byte entries and no pointer. Requires binutils nm/objdump/readelf.
"""
import argparse
from pathlib import Path
import struct
import subprocess


def command(*args):
    return subprocess.check_output(args, text=True)


def read_table(path, stride, field_offset):
    data = path.read_bytes()
    if data[:6] != b'\x7fELF\x02\x01':
        raise ValueError('expected a little-endian ELF64 core')
    symbols = [line.split() for line in command('nm', '-S', str(path)).splitlines()]
    symbol, = [row for row in symbols if row and row[-1] == 'tcg_op_defs']
    address, size = (int(value, 16) for value in symbol[:2])
    if size % stride:
        raise ValueError('opcode table size is not a multiple of its stride')
    for line in command('objdump', '-h', str(path)).splitlines():
        row = line.split()
        if len(row) < 7 or not row[0].isdigit():
            continue
        length, start, offset = (int(row[i], 16) for i in (2, 3, 5))
        if start <= address and address + size <= start + length:
            offset += address - start
            break
    else:
        raise ValueError('opcode table has no file-backed section')
    entries = []
    for index in range(size // stride):
        position = offset + index * stride
        entry = struct.unpack_from('<4BH', data, position + field_offset)
        if entry[3] != sum(entry[:3]):
            raise ValueError(f'opcode {index}: inconsistent total arity')
        if field_offset == 0 and data[position + 6:position + 8] != b'\0\0':
            raise ValueError(f'opcode {index}: nonzero reserved bytes')
        entries.append(entry)
    relocations = 0
    for line in command('readelf', '-rW', str(path)).splitlines():
        try:
            location = int(line.split()[0], 16)
        except (ValueError, IndexError):
            continue
        relocations += address <= location < address + size
    return entries, size, relocations


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('candidate', type=Path)
    args = parser.parse_args()
    old, old_size, old_relocs = read_table(args.baseline, 16, 8)
    new, new_size, new_relocs = read_table(args.candidate, 8, 0)
    if old != new:
        raise ValueError('opcode count, argument counts or flags changed')
    print(f'All {len(old)} opcode entries preserve argument counts and flags')
    print(f'Table bytes: {old_size} -> {new_size}')
    print(f'Dynamic relocations within table: {old_relocs} -> {new_relocs}')


if __name__ == '__main__':
    main()
