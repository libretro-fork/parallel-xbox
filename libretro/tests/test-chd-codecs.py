# SPDX-License-Identifier: GPL-2.0-or-later
"""Pinned rchd/rzstd with independent CHD layouts and reference zstd frames.
Needs the reference zstd CLI to generate fixtures; no MAME/ROM input required.
"""
from pathlib import Path
import binascii, lzma, os, random, shlex, struct, subprocess, sys, tempfile, zlib
root = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else 'cc'
with tempfile.TemporaryDirectory(prefix='chd-codecs-') as temporary:
    work = Path(os.environ.get('CHD_CODEC_WORK', temporary)).resolve()
    work.mkdir(parents=True, exist_ok=True)
    rng = random.Random(749); hb = 4096
    chunks = [bytes(hb), bytes(range(256)) * 16,
              rng.randbytes(hb), b'Xbox CHD ownership\0' * 215 + b'x' * 11]
    chunks[3] = (chunks[3] * 2)[:hb]
    raw = b''.join(chunks[i % 4] for i in range(32))[:-713]
    (work/'raw.bin').write_bytes(raw)
    padded = raw + bytes((-len(raw)) % hb)
    count = len(padded) // hb
    h = bytearray(120); h[:8] = b'MComprHD'
    struct.pack_into('>IIIIIQQ', h, 8, 120, 3, 0, 2, count, len(raw), 0)
    struct.pack_into('>I', h, 76, hb)
    offset = 120 + count * 16 + 16
    table = bytearray(); blobs = bytearray()
    for start in range(0, len(padded), hb):
        block = padded[start:start+hb]
        z = zlib.compressobj(6, zlib.DEFLATED, -15)
        blob = z.compress(block) + z.flush()
        table += struct.pack('>QIHB', offset, zlib.crc32(block), len(blob) & 65535,
                             len(blob) >> 16) + b'\x01'
        offset += len(blob); blobs += blob
    (work/'v3.chd').write_bytes(h + table + b'EndOfListCookie\0' + blobs)
    h = bytearray(124); h[:8] = b'MComprHD'
    struct.pack_into('>II', h, 8, 124, 5)
    struct.pack_into('>QQQII', h, 32, len(raw), 124, 0, hb, 2048)
    prefix = hb
    table = bytearray(); blobs = bytearray()
    for start in range(0, len(padded), hb):
        block = padded[start:start+hb]
        if not any(block): table += bytes(4)
        else:
            table += struct.pack('>I', (prefix + len(blobs)) // hb)
            blobs += block
    (work/'v5.chd').write_bytes(h + table + bytes(prefix-len(h)-len(table)) + blobs)
    for codec in ['zstd', 'lzma']:
        blobs = bytearray(); entries = bytearray(); fields = ''
        for start in range(0, len(padded), hb):
            block = padded[start:start+hb]
            if codec == 'zstd':
                blob = subprocess.run(['zstd', '-q', '--check', '-3', '-c'],
                                      input=block, stdout=subprocess.PIPE, check=True).stdout
            else:
                blob = lzma.compress(block, format=lzma.FORMAT_RAW, filters=[{
                    'id': lzma.FILTER_LZMA1, 'dict_size': 4096, 'lc': 3, 'lp': 0, 'pb': 2}])
            crc = binascii.crc_hqx(block, 0xffff)
            entries += (bytes(1) + len(blob).to_bytes(3, 'big') +
                        (124 + len(blobs)).to_bytes(6, 'big') + crc.to_bytes(2, 'big'))
            fields += f'{len(blob):016b}{crc:016b}'
            blobs += blob
        # All sixteen canonical Huffman codes have length four. Symbol zero
        # names compressor slot zero for every hunk; no encoder is reused.
        bits = '0100' * 16 + '0000' * count + fields
        bits += '0' * ((-len(bits)) % 8)
        bits = int(bits, 2).to_bytes(len(bits)//8, 'big')
        mapheader = (len(bits).to_bytes(4, 'big') + (124).to_bytes(6, 'big') +
                     binascii.crc_hqx(entries, 0xffff).to_bytes(2, 'big') + bytes([16, 0, 0, 0]))
        h = bytearray(124); h[:8] = b'MComprHD'
        struct.pack_into('>II', h, 8, 124, 5); h[16:20] = codec.encode('ascii')
        struct.pack_into('>QQQII', h, 32, len(raw), 124 + len(blobs), 0, hb, 2048)
        (work/f'v5-{codec}.chd').write_bytes(h + blobs + mapheader + bits)
    for i in range(12):
        n = [0, 1, 4096, 65536, 131072, 270013][i % 6]
        data = (rng.randbytes(n) if i < 6 else
                (b'Xbox compressed hunks and repeated offsets\0' * (n//42+1))[:n])
        (work/f'z{i}.raw').write_bytes(data)
        subprocess.run(['zstd', '-q', '-f', '--check', '-'+str(1+i%9),
                        str(work/f'z{i}.raw'), '-o', str(work/f'z{i}.zst')], check=True)
    sources = ['formats/chd/rchd.c','formats/flac/rflac.c','formats/7z/r7z_lzma.c',
               'encodings/encoding_rzstd.c','encodings/encoding_huffman.c',
               'encodings/encoding_xxh64.c','encodings/encoding_deflate.c',
               'encodings/encoding_crc32.c','features/features_cpu.c']
    binary = os.environ.get('CHD_CODEC_BINARY', str(work/'test'))
    flags = shlex.split(os.environ.get('CHD_CODEC_CFLAGS',
                        '-static' if 'mingw' in cc else '-fsanitize=address,undefined'))
    subprocess.run([cc, '-O2', '-std=gnu11', '-g', '-ffunction-sections',
                    '-Wl,--gc-sections', *flags,
                    '-DHAVE_RCHD_DEFLATE', '-DHAVE_RCHD_LZMA',
                    '-DHAVE_RCHD_FLAC', '-DHAVE_RCHD_ZSTD',
                    '-I'+str(root/'libretro-common/include'),
                    str(root/'libretro/tests/test-chd-codecs.c'),
                    *[str(root/'libretro-common'/s) for s in sources],
                    '-lm', '-o', binary], check=True)
    if not os.environ.get('CHD_CODEC_SKIP_RUN'):
        subprocess.run([binary, str(work)], check=True, timeout=60)
