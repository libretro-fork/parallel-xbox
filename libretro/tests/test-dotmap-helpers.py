#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare full fragment generators with lazy dot-mapping helper definitions.

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
    args=[r/"libretro/tests/test-dotmap-helpers.c",psh,*objects]
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


function=re.compile(r"vec3 (dotmap_\w+)\(vec4 col\) \{\n[\s\S]*?\n\}\n")
names=("dotmap_zero_to_one","dotmap_minus1_to_1_d3d","dotmap_minus1_to_1_gl",
       "dotmap_minus1_to_1","dotmap_hilo_1","dotmap_hilo_hemisphere_d3d",
       "dotmap_hilo_hemisphere_gl","dotmap_hilo_hemisphere")
def opcodes(path):
    data=path.read_bytes();words=struct.unpack("<"+"I"*(len(data)//4),data)
    assert words[0]==0x07230203
    counts=Counter();i=5
    while i<len(words):
        length=words[i]>>16;assert length;counts[words[i]&65535]+=1;i+=length
    assert i==len(words)
    return counts
count=source_saved=spv_saved=ordinary_source=ordinary_spv=0
for old in sorted((out/"parent").glob("*.frag")):
    new=out/"candidate"/old.name;x,y=old.read_text(),new.read_text()
    api,mode,mapping,n,cube=map(int,old.stem.split("-"))
    needed=set()
    if 3<=mode<=9:needed.add(names[mapping])
    if mode==6:needed.add(names[n])
    if mode==7 or mode==9:needed.update((names[(mapping+1)%8],names[(mapping+2)%8]))
    before={m.group(1):m.group() for m in function.finditer(x)}
    matches=list(function.finditer(y));after={m.group(1):m.group() for m in matches}
    assert len(before)==8
    assert len(matches)==len(after) and set(after)==needed,(old.name,set(after),needed)
    assert all(after[name]==before[name] for name in needed),(old.name,"function changed")
    assert function.sub("",x)==function.sub("",y),(old.name,"shader body changed")
    source_saved+=len(x)-len(y)
    if mode==0:ordinary_source=len(x)-len(y)
    if api:
        first,second=opcodes(old.with_suffix(".spv")),opcodes(new.with_suffix(".spv"))
        assert {i:first[i] for i in range(86,108)}=={i:second[i] for i in range(86,108)}
        size=old.with_suffix(".spv").stat().st_size-new.with_suffix(".spv").stat().st_size
        assert size>=0;spv_saved+=size
        if mode==0:ordinary_spv=size
    count+=1
assert count==448
result={"status":"PASS","fragment_variants":count,"shader_bodies_unchanged":True,
        "used_helper_definitions_unchanged":True,"helpers_emitted_once":True,
        "GLSL_bytes_removed_total":source_saved,"SPIRV_bytes_removed_total":spv_saved,
        "ordinary_GLSL_bytes_removed":ordinary_source,"ordinary_SPIRV_bytes_removed":ordinary_spv,
        "Vulkan_image_operation_counts_unchanged":True}
(out/"validation.json").write_text(json.dumps(result,indent=2)+"\n")
print(json.dumps(result))
