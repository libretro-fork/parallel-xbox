# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent cooked-DVD CHD fixtures from a small mkdisc image.
Usage: python3 make-chd-fixture.py RAW_ISO OUT_CHD [v3-zlib|v5-zstd] [PARTITION_BASE]
Needs the reference zstd CLI for v5-zstd. Not a general CHD authoring tool.
"""
from pathlib import Path
import binascii,struct,subprocess,sys,zlib
raw=Path(sys.argv[1]).read_bytes();out=Path(sys.argv[2]);codec=sys.argv[3] if len(sys.argv)>3 else 'v5-zstd'
base=int(sys.argv[4],0) if len(sys.argv)>4 else 0
assert raw and len(raw)%2048==0 and len(raw)<=16*1024*1024
assert 0<=base<=64*1024*1024 and base%2048==0
raw=bytes(base)+raw;hb=65536;count=(len(raw)+hb-1)//hb
if codec=='v3-zlib':
 h=bytearray(120);h[:8]=b'MComprHD';struct.pack_into('>IIIIIQQ',h,8,120,3,0,2,count,len(raw),0);struct.pack_into('>I',h,76,hb)
 offset=120+count*16+16;table=bytearray();blobs=bytearray()
 for start in range(0,len(raw),hb):
  block=raw[start:start+hb].ljust(hb,b'\0');c=zlib.compressobj(6,zlib.DEFLATED,-15);blob=c.compress(block)+c.flush()
  table+=struct.pack('>QIHB',offset,zlib.crc32(block),len(blob)&65535,len(blob)>>16)+b'\x01';offset+=len(blob);blobs+=blob
 # Pre-v5 unit size is inferred from sector metadata, not from hunk geometry.
 data=f'CYLS:1,HEADS:1,SECS:{len(raw)//2048},BPS:2048\0'.encode();struct.pack_into('>Q',h,36,offset)
 out.write_bytes(h+table+b'EndOfListCookie\0'+blobs+struct.pack('>IIQ',0x47444444,len(data),0)+data)
elif codec=='v5-zstd':
 blobs=bytearray();entries=bytearray();fields=''
 for start in range(0,len(raw),hb):
  block=raw[start:start+hb].ljust(hb,b'\0')
  blob=subprocess.run(['zstd','-q','--check','-3','-c'],input=block,stdout=subprocess.PIPE,check=True).stdout
  crc=binascii.crc_hqx(block,0xffff)
  entries+=b'\0'+len(blob).to_bytes(3,'big')+(124+len(blobs)).to_bytes(6,'big')+crc.to_bytes(2,'big')
  fields+=f'{len(blob):017b}{crc:016b}';blobs+=blob
 # Uniform four-bit canonical Huffman tree; compressor slot zero for all hunks.
 bits='0100'*16+'0000'*count+fields;bits+='0'*((-len(bits))%8);bits=int(bits,2).to_bytes(len(bits)//8,'big')
 mh=len(bits).to_bytes(4,'big')+(124).to_bytes(6,'big')+binascii.crc_hqx(entries,0xffff).to_bytes(2,'big')+bytes([17,0,0,0])
 h=bytearray(124);h[:8]=b'MComprHD';struct.pack_into('>II',h,8,124,5);h[16:20]=b'zstd'
 struct.pack_into('>QQQII',h,32,len(raw),124+len(blobs),0,hb,2048);out.write_bytes(h+blobs+mh+bits)
else:raise SystemExit('Expected v3-zlib or v5-zstd')
print(f'{codec}: {len(raw)} logical bytes, partition base {base:#x}, {out.stat().st_size} physical bytes')
