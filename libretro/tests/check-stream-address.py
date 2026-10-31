# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare real guest SSL playback at physical zero and a nonzero address."""
import struct
import sys
import wave

recordings = []
for name in sys.argv[1:]:
    with wave.open(name, 'rb') as audio:
        assert audio.getnchannels() == 2 and audio.getsampwidth() == 2
        assert audio.getframerate() == 48000 and audio.getnframes() == 48048
        recordings.append(audio.readframes(audio.getnframes()))
assert len(recordings) == 2 and recordings[0] == recordings[1], 'SSL source address changed the audio'
left = struct.unpack('<' + 'h' * (len(recordings[0]) // 2), recordings[0])[::2]
peak = max(abs(x) for x in left)
crossings = sum(a <= 0 < b for a, b in zip(left, left[1:]))
assert peak > 128 and 800 <= crossings <= 1200, (peak, crossings)
print(f'SSL address zero/nonzero: 48048 stereo frames bit-identical; peak={peak}, positive crossings={crossings}')
