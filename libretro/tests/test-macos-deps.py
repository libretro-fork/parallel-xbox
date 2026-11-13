#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build-only MacPorts failover, exact archive identity and download ownership."""
from pathlib import Path
import argparse, contextlib, importlib.util, io, os, subprocess, tempfile
from unittest.mock import patch
from urllib.error import HTTPError
from http.client import IncompleteRead

ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('checkout',type=Path);ap.add_argument('--network',action='store_true')
a=ap.parse_args();r=a.checkout.resolve()
spec=importlib.util.spec_from_file_location('mac_deps',r/'scripts/download-macos-libs.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
assert m.REQUIRED_PACKAGES==('libepoxy',)
assert all(x.startswith('https://') for x in m.MIRRORS)
source=(r/'scripts/download-macos-libs.py').read_text()
install=source[source.index('\tdef install_pkg('):source.index('\tdef install_pkgs(')]
assert install.index('self.verify_pkg(')<install.index('TarFile.open(')
# Static Makefile root dependencies, not standalone xemu's old dependency set.
mac=(r/'Makefile').read_text().split('else ifeq ($(platform),osx)',1)[1].split('else ifeq ($(platform),android)',1)[0]
assert 'PKGS := epoxy' in mac
mirrors=('https://primary.example','https://second.example','https://third.example')
closed=[]
class Response(io.BytesIO):
 def __init__(self,data,label,fail=False,binary=False):
  super().__init__(data);self.label=label;self.fail=fail;self.binary=binary;self.reads=0;self.headers={}
 def read(self,n=-1):
  if self.binary:assert 0<n<=65536,('unbounded binary read',n)
  if self.fail and self.reads:raise OSError('interrupted body')
  self.reads+=1
  return super().read(n)
 def close(self):
  if not self.closed:closed.append(self.label)
  super().close()

with tempfile.TemporaryDirectory(prefix='mac-deps-') as folder:
 old=Path.cwd();os.chdir(folder)
 try:
  with patch.object(m,'MIRRORS',mirrors):
   for arch,target in [('arm64','darwin_23'),('x86_64','darwin_21')]:
    li=m.LibInstaller(arch);calls=[]
    filename=f'libepoxy-1.5.10_4+x11.{target}.{arch}.tbz2'
    listing=('libepoxy-9.darwin_99.wrong.tbz2 '+filename+' '+filename+'.rmd160').encode()
    def opener(url,timeout):
     assert timeout==m.URL_TIMEOUT;calls.append(url)
     if url.startswith(mirrors[0]):raise HTTPError(url,501,'primary unavailable',{},None)
     return Response(listing,url)
    with patch.object(m,'urlopen',opener):
     got,url=li.get_latest_pkg_filename_url('libepoxy')
    assert got==filename and url==mirrors[1]+'/libepoxy/'+filename
    assert calls==[x+'/libepoxy/' for x in mirrors[:2]]
    # An available mirror with no compatible package is not a final failure.
    def incompatible(url,timeout):
     return Response(b'libepoxy-9.darwin_99.wrong.tbz2' if url.startswith(mirrors[0]) else listing,url)
    with patch.object(m,'urlopen',incompatible):assert li.get_latest_pkg_filename_url('libepoxy')[0]==filename
    # Retry the exact archive, retaining bounded streaming and closed owners.
    dst=Path(folder)/'cache owner'/filename;dst.parent.mkdir(exist_ok=True);data=b'payload\0'*20000;calls=[]
    def interrupted(url,timeout):
     assert timeout==m.URL_TIMEOUT;calls.append(url)
     return Response(data,url,fail=len(calls)==1,binary=True)
    before=set(dst.parent.iterdir())
    with patch.object(m,'urlopen',interrupted):li.download_file('archive',url,str(dst))
    assert dst.read_bytes()==data and len(calls)==2
    assert all(x.endswith('/libepoxy/'+filename) for x in calls)
    assert set(dst.parent.iterdir())==before|{dst} and all(x in closed for x in calls)
    def forbidden(*args,**kw):raise AssertionError('cached complete download was fetched again')
    with patch.object(m,'urlopen',forbidden):li.download_file('archive',url,str(dst))
    # An incomplete body never becomes a completed cache entry or leaked temp.
    missing=dst.with_name(arch+'-failed.tbz2');before=set(dst.parent.iterdir())
    def always_interrupted(url,timeout):return Response(data,url,fail=True,binary=True)
    with patch.object(m,'urlopen',always_interrupted):
     try:li.download_file('failed',url,str(missing))
     except RuntimeError:pass
     else:raise AssertionError('failed transfer was published')
    assert not missing.exists() and set(dst.parent.iterdir())==before
    # A short EOF can be silent with bounded reads; compare declared length.
    def short_body(url,timeout):
     response=Response(b'truncated',url,binary=True);response.headers={'Content-Length':'999'};return response
    with patch.object(m,'urlopen',short_body):
     try:li.download_file('short EOF',url,str(missing))
     except RuntimeError:pass
     else:raise AssertionError('short Content-Length body cached')
    assert not missing.exists() and set(dst.parent.iterdir())==before
    def bad_chunks(url,timeout):raise IncompleteRead(b'partial',99)
    with patch.object(m,'urlopen',bad_chunks):
     try:li.download_file('broken chunks',url,str(missing))
     except RuntimeError:pass
     else:raise AssertionError('chunk failure suppressed')
    assert not missing.exists() and set(dst.parent.iterdir())==before
    # Empty leftovers from older downloaders are not accepted as complete.
    missing.touch()
    with patch.object(m,'urlopen',lambda url,timeout:Response(b'complete',url,binary=True)):
     li.download_file('retry zero bytes',url,str(missing))
    assert missing.read_bytes()==b'complete'
    assert list(li.download_urls('https://key.example/pinned.pem'))==['https://key.example/pinned.pem']
   # All directory failures remain failures, with each mirror tried once.
   def dead(url,timeout):raise OSError('offline')
   with patch.object(m,'urlopen',dead):
    try:li.get_latest_pkg_filename_url('libepoxy')
    except RuntimeError as e:assert all(x in str(e) for x in mirrors)
    else:raise AssertionError('missing archive accepted')
  # Trust checks remain on the build tool's actual verification path.
  def verify_run(command,**kw):
   assert kw.get('check') and kw.get('shell')
   if 'openssl rmd160 ' in command:output=b'd3a22f5be7184d6575afcc1be6fdb82fd25562e8\n'
   elif 'openssl sha1 ' in command:output=b'214baa965af76ff71187e6c1ac91c559547f48ab\n'
   else:
    assert 'openssl dgst -ripemd160 -verify ' in command
    raise subprocess.CalledProcessError(1,command)
   return subprocess.CompletedProcess(command,0,stdout=output)
  key_urls=[]
  def key_open(url,timeout):key_urls.append(url);return Response(b'key fixture',url,binary=True)
  with patch.object(m,'urlopen',key_open),patch.object(m.subprocess,'run',verify_run):
   try:li.verify_pkg(str(dst),str(dst)+'.rmd160')
   except subprocess.CalledProcessError:pass
   else:raise AssertionError('signature failure suppressed')
  assert key_urls==['https://raw.githubusercontent.com/macports/macports-base/master/macports-pubkey.pem']
  def wrong_key(command,**kw):return subprocess.CompletedProcess(command,0,stdout=b'wrong\n')
  with patch.object(m.subprocess,'run',wrong_key):
   try:li.verify_pkg(str(dst),str(dst)+'.rmd160')
   except AssertionError as e:assert str(e)=='Invalid MacPorts key'
   else:raise AssertionError('untrusted key accepted')
  print('Mac bootstrap: both architectures, 501/no-compatible fallback, exact filenames, bounded stream, interrupted/empty/cache ownership and unchanged key/signature rejection PASS')
 finally:os.chdir(old)

if a.network:
 # Optional integration: verify real mirrored archives without extracting or
 # executing foreign binaries. Signature verification is the production method.
 with tempfile.TemporaryDirectory(prefix='mac-deps-network-') as folder:
  old=Path.cwd();os.chdir(folder)
  try:
   for arch in ('arm64','x86_64'):
    li=m.LibInstaller(arch);name,url=li.get_latest_pkg_filename_url('libepoxy')
    archive=Path(li._pkgs_path)/name;sig=Path(str(archive)+'.rmd160')
    li.download_file(name,url,str(archive));li.download_file('signature',url+'.rmd160',str(sig));li.verify_pkg(str(archive),str(sig))
    print('Verified real signed archive',arch,name,flush=True)
    # Same named archive with changed payload must still be rejected.
    with archive.open('r+b') as f:b=f.read(1);f.seek(0);f.write(bytes([b[0]^1]))
    try:li.verify_pkg(str(archive),str(sig))
    except subprocess.CalledProcessError:print('Required tampered-archive control rejected',arch,flush=True)
    else:raise AssertionError('tampered archive verified')
  finally:os.chdir(old)
