#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Benchmark paired generated Vulkan fragments with the production compiler.

Preload and deduplicate paired inputs, alternate timing order, and profile
allocations in a separately instrumented binary. No latency pass/fail threshold.
"""
import argparse
import hashlib
import json
import os
import platform
from pathlib import Path
import re
import statistics
import subprocess
import sys
from shader_helpers_fixture import extract_common_helpers

p=argparse.ArgumentParser(description=__doc__)
for name in ("checkout","parent_sources","candidate_sources","output"):
    p.add_argument(name,type=Path)
p.add_argument("--cc",default="gcc")
p.add_argument("--cpu",type=int,help="pin only the benchmark child thread to this logical CPU")
a=p.parse_args();
if a.cpu is not None and a.cpu<0:p.error("--cpu must be nonnegative")
r=a.checkout.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
corpus=out/"corpus"
for name in ("parent","candidate"):(corpus/name).mkdir(parents=True,exist_ok=True)
dotmaps=re.compile(r"vec3 (dotmap_\w+)\(vec4 col\) \{\n[\s\S]*?\n\}\n")
tables=re.compile(r"const float\[9\] gaussian3x3 = float\[9\]\([\s\S]*?const vec2\[9\] convolution3x3 = vec2\[9\]\([\s\S]*?\);\n")
seen=set();cases=[];input_count=0
for old in sorted(a.parent_sources.resolve().glob("1-*.frag")):
    new=a.candidate_sources.resolve()/old.name
    x,y=old.read_text(),new.read_text();assert "\0" not in x+y
    before,bx=extract_common_helpers(x);after,by=extract_common_helpers(y)
    assert all(before.get(name)==text for name,text in after.items()),old.name
    dx={m.group(1):m.group() for m in dotmaps.finditer(bx)}
    dy={m.group(1):m.group() for m in dotmaps.finditer(by)}
    assert all(dx.get(name)==text for name,text in dy.items()),old.name
    tx,ty=tables.findall(bx),tables.findall(by)
    assert not ty or tx==ty,old.name
    assert dotmaps.sub("",tables.sub("",bx))==dotmaps.sub("",tables.sub("",by)),old.name
    input_count+=1;key=hashlib.sha256((x+"\0"+y).encode()).hexdigest()
    if key in seen:continue
    seen.add(key);i=len(cases)
    (corpus/"parent"/f"{i}.frag").write_bytes(x.encode())
    (corpus/"candidate"/f"{i}.frag").write_bytes(y.encode())
    cases.append({"name":old.name,"parent_SHA256":hashlib.sha256(x.encode()).hexdigest(),
                  "candidate_SHA256":hashlib.sha256(y.encode()).hexdigest(),
                  "parent_bytes":len(x.encode()),"candidate_bytes":len(y.encode())})
assert cases,"no paired Vulkan fragments"
(out/"corpus.json").write_text(json.dumps({"paired_inputs":input_count,"unique_program_pairs":len(cases),"cases":cases},indent=2)+"\n")
source=r/"hw/xbox/nv2a/pgraph/spv"
env=dict(os.environ);env['PATH']=str(Path(a.cc).resolve().parent)+os.pathsep+env.get('PATH','') if Path(a.cc).is_absolute() else env.get('PATH','')
compiler=subprocess.check_output([a.cc,"--version"],text=True,env=env).splitlines()[0]
results={}
for profile in (False,True):
    exe=out/(("allocations" if profile else "timing")+(".exe" if os.name=="nt" else ""))
    cmd=[a.cc,"-O2","-std=gnu11","-fno-strict-aliasing","-fwrapv","-Wall","-Wextra","-Werror",
         "-Wno-misleading-indentation","-Wno-sign-compare","-Wno-missing-field-initializers",
         "-I"+str(r),"-I"+str(r/"deps/spirv-headers/include"),
         str(Path(__file__).with_suffix('.c')),str(source/"glslc.c"),str(source/"glslc_lex.c"),"-lm","-o",str(exe)]
    if profile:cmd += ["-DPROFILE_ALLOCATIONS","-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc"]
    subprocess.run(cmd,check=True,env=env)
    for version in (15,16):
        label=("allocations" if profile else "timing")+f"-{version}"
        print(f"Running {label}: {len(cases)} unique paired fragments",flush=True)
        completed=subprocess.run([str(exe),str(corpus),str(len(cases)),str(version),*([str(a.cpu)] if a.cpu is not None else [])],capture_output=True,text=True,env=env,timeout=90)
        if completed.returncode:
            sys.stderr.write(completed.stderr)
            completed.check_returncode()
        (out/(label+'.jsonl')).write_text(completed.stdout)
        rows=[json.loads(line) for line in completed.stdout.splitlines()]
        assert rows[0]['metadata_equal'];results[label]=rows[1:]
summary={}
for version in (15,16):
    timed=results[f'timing-{version}'];assert len(timed)==16
    grouped={v:sorted((row for row in timed if row['version']==v),key=lambda row:row['round']) for v in (0,1)}
    assert all(len(rows)==8 for rows in grouped.values())
    detail={}
    for clock in ('cpu_ns','wall_ns'):
        ratios=[]
        for old,new in zip(grouped[0],grouped[1]):
            assert old['round']==new['round'] and old['compiles']==new['compiles']
            ratios.append(new[clock]/old[clock])
        detail[clock]={"parent_median_us_per_compile":statistics.median(row[clock]/row['compiles']/1000 for row in grouped[0]),
                       "candidate_median_us_per_compile":statistics.median(row[clock]/row['compiles']/1000 for row in grouped[1]),
                       "median_paired_reduction_percent":100*(1-statistics.median(ratios)),
                       "paired_ratio_min":min(ratios),"paired_ratio_max":max(ratios)}
    allocations=results[f'allocations-{version}'];assert len(allocations)==2
    detail['allocations']=allocations
    for metric in ('allocation_calls','requested_bytes','module_bytes'):
        detail[metric+'_reduction_percent']=100*(1-allocations[1][metric]/allocations[0][metric])
    summary[str(version)]=detail
report={"status":"COMPLETE","compiler":compiler,"paired_inputs":input_count,"unique_program_pairs":len(cases),
        "rounds":8,"CPU_affinity":a.cpu,"emit_layout":True,"platform":platform.platform(),
        "compiler_source_SHA256":{path.name:hashlib.sha256(path.read_bytes()).hexdigest() for path in source.glob("glslc*") if path.suffix in (".c",".h")},"summary":summary,
        "scope":"CPU compiler microbenchmark with preloaded deduplicated synthetic fragment pairs, compilation/result disposal and uniform-layout checks. No renderer, shader generator, disk I/O or GPU pipeline creation inside timed batches. Timing binary is uninstrumented; allocation profiling is a separate binary. Source differences are restricted to omitted helper definitions/tables.",
        "limitations":"Synthetic corpus is not game-weighted. No game FPS, driver compilation or stutter measurement. Recorded allocation calls are direct linked malloc/calloc/realloc requests, excluding allocator internals. Requested bytes sum capacities, including realloc, and are not peak/live/RSS memory."}
(out/'benchmark.json').write_text(json.dumps(report,indent=2)+"\n")
print(json.dumps(report,indent=2))
