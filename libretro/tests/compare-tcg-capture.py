#!/usr/bin/env python3
"""Compare actual emitted TB sizes, including unmatched keys and variants.

Input is produced by the private build-tcg-capture.py module with
XEMU_TCG_CAPTURE set to a fresh file. No elapsed-time measurements are used.
"""
import argparse
from collections import defaultdict
from pathlib import Path
import struct


def read_capture(path):
    data = path.read_bytes()
    blocks = defaultdict(set)
    offset = 0
    while offset < len(data):
        if len(data) - offset < 48:
            raise ValueError(f'{path}: truncated record header')
        *key, size = struct.unpack_from('<6Q', data, offset)
        offset += 48
        if size > len(data) - offset:
            raise ValueError(f'{path}: truncated code')
        blocks[tuple(key)].add(data[offset:offset + size])
        offset += size
    return blocks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('candidate', type=Path)
    args = parser.parse_args()
    old, new = map(read_capture, (args.baseline, args.candidate))
    common = old.keys() & new.keys()
    print(f'matched={len(common)} baseline_only={len(old.keys() - new.keys())} '
          f'candidate_only={len(new.keys() - old.keys())}')
    print('pc guest_bytes insns cflags flags baseline_sizes candidate_sizes')
    for key in sorted(old.keys() | new.keys()):
        sizes = [sorted({len(code) for code in blocks.get(key, ())})
                 for blocks in (old, new)]
        print(' '.join(hex(value) for value in key), *sizes)


if __name__ == '__main__':
    main()
