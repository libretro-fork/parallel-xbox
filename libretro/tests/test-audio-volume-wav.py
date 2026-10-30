# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare captured stereo S16 host attenuation against an exact decimal oracle.

Usage: python3 SCRIPT UNITY.wav SCALED.wav GAIN_Q16 [--all-s16]
The reference must come from the same DSP fixture and frame count at 100%.
"""
from decimal import Decimal, ROUND_HALF_UP
from pathlib import Path
import struct
import sys
import wave


def read_samples(path):
    with wave.open(str(path), 'rb') as stream:
        if (stream.getnchannels(), stream.getsampwidth(), stream.getframerate(),
                stream.getcomptype()) != (2, 2, 48000, 'NONE'):
            raise ValueError(f'{path}: expected stereo 48 kHz S16 PCM')
        frames = stream.getnframes()
        data = stream.readframes(frames)
        if len(data) != frames * 4:
            raise ValueError(f'{path}: truncated PCM data')
        return struct.unpack('<' + 'h' * (frames * 2), data)


def main():
    if len(sys.argv) not in (4, 5) or (len(sys.argv) == 5 and
                                      sys.argv[4] != '--all-s16'):
        raise SystemExit(__doc__)
    gain = int(sys.argv[3])
    if not 0 <= gain <= 65536:
        raise ValueError('gain must be in [0, 65536]')
    unity = read_samples(Path(sys.argv[1]))
    actual = read_samples(Path(sys.argv[2]))
    if not unity or len(unity) != len(actual):
        raise ValueError('captures must have identical nonzero frame counts')
    distinct = set(unity)
    if '--all-s16' in sys.argv and len(distinct) != 65536:
        raise ValueError(f'expected all S16 inputs, found {len(distinct)}')
    # Decimal division by 2^16 is exact here. Quantization uses the decimal
    # library, independently of the production integer sign/offset formula.
    expected = {sample: int((Decimal(sample) * gain / 65536).quantize(
        Decimal(1), rounding=ROUND_HALF_UP)) for sample in distinct}
    for index, (source, output) in enumerate(zip(unity, actual)):
        if output != expected[source]:
            raise ValueError(f'frame {index // 2}, channel {index % 2}: '
                             f'input {source}, got {output}, '
                             f'expected {expected[source]} at Q16 gain {gain}')
    print(f'host volume WAV: {len(unity)} samples, {len(distinct)} distinct '
          f'S16 inputs, gain={gain}, zero mismatches')


if __name__ == '__main__':
    main()
