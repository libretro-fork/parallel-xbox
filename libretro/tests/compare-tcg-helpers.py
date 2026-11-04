#!/usr/bin/env python3
"""Compare helper descriptors in unstripped old/new Linux ELF64 cores.

Check every linked helper's function target and all metadata bytes while
excluding only the removed diagnostic name pointer. Requires binutils.
"""
import argparse
from collections import defaultdict
from pathlib import Path
import struct
import subprocess


def output(*args):
    return subprocess.check_output(args, text=True)


def read_core(path):
    data = path.read_bytes()
    if data[:6] != b'\x7fELF\x02\x01':
        raise ValueError('expected a little-endian ELF64 core')
    symbols = [line.split() for line in output('nm', '-S', str(path)).splitlines()]
    functions = defaultdict(set)
    helpers = {}
    for row in symbols:
        if len(row) != 4:
            continue
        address, size = (int(value, 16) for value in row[:2])
        if row[2] in ('t', 'T', 'w', 'W'):
            functions[address].add(row[3])
        if row[3].startswith(('helper_info_', 'info_helper_')):
            helpers[row[3]] = (address, size)
    if not helpers:
        raise ValueError('no helper descriptors found')
    sections = []
    for line in output('objdump', '-h', str(path)).splitlines():
        row = line.split()
        if len(row) >= 7 and row[0].isdigit():
            sections.append(tuple(int(row[i], 16) for i in (3, 2, 5)))
    descriptors = {}
    for name, (address, size) in helpers.items():
        for start, length, offset in sections:
            if start <= address and address + size <= start + length:
                offset += address - start
                raw = data[offset:offset + size]
                break
        else:
            raise ValueError(f'{name}: missing file-backed section')
        target, = struct.unpack_from('<Q', raw)
        if target and not functions[target]:
            raise ValueError(f'{name}: unknown function target')
        descriptors[name] = (raw, functions[target] if target else {None})
    relocations = []
    for line in output('readelf', '-rW', str(path)).splitlines():
        try:
            relocations.append(int(line.split()[0], 16))
        except (ValueError, IndexError):
            pass
    count = sum(any(address <= location < address + size
                    for address, size in helpers.values())
                for location in relocations)
    return descriptors, count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('candidate', type=Path)
    args = parser.parse_args()
    old, old_relocs = read_core(args.baseline)
    new, new_relocs = read_core(args.candidate)
    if old.keys() != new.keys():
        raise ValueError('linked helper set changed')
    for name, (before, targets) in old.items():
        after, new_targets = new[name]
        if len(before) != len(after) + 8:
            raise ValueError(f'{name}: unexpected descriptor size')
        if not targets.intersection(new_targets) or before[16:] != after[8:]:
            raise ValueError(f'{name}: function target or functional metadata changed')
    print(f'All {len(old)} linked helpers preserve function targets and functional metadata')
    print('Descriptor sizes:', sorted({len(raw) for raw, _ in old.values()}),
          '->', sorted({len(raw) for raw, _ in new.values()}))
    print('Descriptor bytes:', sum(len(raw) for raw, _ in old.values()),
          '->', sum(len(raw) for raw, _ in new.values()))
    print(f'Dynamic relocations within descriptors: {old_relocs} -> {new_relocs}')


if __name__ == '__main__':
    main()
