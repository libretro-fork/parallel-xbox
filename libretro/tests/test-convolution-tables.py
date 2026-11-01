#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare full production fragment generators with lazy convolution tables.

Requires a configured/built core and a saved parent glsl/psh.o. All other linked
production objects come from the candidate build; their interfaces are unchanged.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import shlex
import struct
import subprocess
import os

p=argparse.ArgumentParser(description=__doc__)
for name in ("checkout","build","parent_psh_object","scratch"):
    p.add_argument(name,type=Path)
p.add_argument("--make",default="make")
p.add_argument("--make-arg",action="append",default=[])
p.add_argument("--bash",default="bash")
p.add_argument("--objcopy",default="objcopy")
a=p.parse_args()
r,b,out=a.checkout.resolve(),a.build.resolve(),a.scratch.resolve()
out.mkdir(parents=True,exist_ok=True)
target="hw/xbox/nv2a/pgraph/glsl/psh"
printed=subprocess.check_output([a.make,*a.make_arg,"-n","-W",target+".c",target+".o"],cwd=b,text=True)
command=next(line for line in printed.splitlines() if " -c -o "+target+".o " in line)
assert " -MMD " in command
prefix=command.split(" -MMD ",1)[0]
objects=[b/name for name in ("hw/xbox/nv2a/pgraph/glsl/common.o",
         "hw/xbox/nv2a/pgraph/spv/glslc.o","hw/xbox/nv2a/pgraph/spv/glslc_lex.o",
         "libretro/glib/gcore.o","libretro/glib/gcontainers.o")]
for version,psh in (("parent",a.parent_psh_object.resolve()),("candidate",b/(target+".o"))):
    directory=out/version
    directory.mkdir(exist_ok=True)
    exe=out/(version+"-driver"+(".exe" if os.name=="nt" else ""))
    args=[r/"libretro/tests/test-convolution-tables.c",psh,*objects]
    if os.name=="nt":
        # PE unwind metadata otherwise retains unrelated emulator functions.
        # Strip only standalone test copies; this fixture never unwinds.
        copied=[]
        for i,path in enumerate(args[1:]):
            target_object=out/f"{version}-{i}.o"
            subprocess.run([a.objcopy,"-R",".pdata*","-R",".xdata*",str(path),str(target_object)],check=True)
            copied.append(target_object)
        args=[args[0],*copied]
    link=prefix+" -fno-asynchronous-unwind-tables "+" ".join(shlex.quote(path.as_posix()) for path in args)
    link+=" -Wl,--gc-sections -lm -o "+shlex.quote(exe.as_posix())
    subprocess.run([a.bash,"--noprofile","--norc","-c",link],cwd=b,check=True)
    subprocess.run([str(exe),str(directory)],check=True)

tables=re.compile(r"const float\[9\] gaussian3x3 = float\[9\]\([\s\S]*?const vec2\[9\] convolution3x3 = vec2\[9\]\([\s\S]*?\);\n")
def opcodes(path):
    data=path.read_bytes();words=struct.unpack("<"+"I"*(len(data)//4),data)
    assert words[0]==0x07230203
    counts=Counter();i=5
    while i<len(words):
        length=words[i]>>16;assert length;counts[words[i]&65535]+=1;i+=length
    assert i==len(words)
    return counts
count=omitted=source_saved=spv_saved=0
for old in sorted((out/"parent").glob("*.frag")):
    new=out/"candidate"/old.name
    x,y=old.read_text(),new.read_text()
    api,mode,rect,kernel,mask=map(int,old.stem.split("-"))
    needed=mode==1 and mask!=0
    assert len(tables.findall(x))==1
    assert len(tables.findall(y))==int(needed),(old.name,"table count")
    assert tables.sub("",x)==tables.sub("",y),(old.name,"shader body changed")
    if needed:
        assert tables.search(x).group()==tables.search(y).group()
    else:
        assert len(y)<len(x);source_saved+=len(x)-len(y);omitted+=1
    if api:
        a_spv,b_spv=old.with_suffix(".spv"),new.with_suffix(".spv")
        first,second=opcodes(a_spv),opcodes(b_spv)
        # All image operations, including queries, fetches and gathers, survive.
        assert {n:first[n] for n in range(86,108)}=={n:second[n] for n in range(86,108)}
        if not needed:
            assert b_spv.stat().st_size<=a_spv.stat().st_size
            spv_saved+=a_spv.stat().st_size-b_spv.stat().st_size
    count+=1
assert count==384 and omitted==264
result={"status":"PASS","fragment_variants":count,"omitted_table_pairs":omitted,
        "GLSL_bytes_removed_total":source_saved,"GLSL_bytes_removed_per_unused_shader":source_saved//omitted,
        "SPIRV_bytes_removed_total":spv_saved,"shader_bodies_unchanged":True,
        "Vulkan_image_operation_counts_unchanged":True}
(out/"validation.json").write_text(json.dumps(result,indent=2)+"\n")
print(json.dumps(result))
