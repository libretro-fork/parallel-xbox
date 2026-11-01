#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute production signed G8B8 mip shaders near texel boundaries."""
import argparse
import ast
import os
from pathlib import Path
import re
import subprocess
import tempfile
from glib_fixture import compat_glib_flags

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("checkout", type=Path)
p.add_argument("--cc", default="gcc")
p.add_argument("--output", type=Path)
p.add_argument("--epoxy-root", type=Path)
a = p.parse_args()
r = a.checkout.resolve()
source = (r / "hw/xbox/nv2a/pgraph/glsl/psh.c").read_text()
state = re.search(r"typedef struct PshState \{.*?\} PshState;", (r / "hw/xbox/nv2a/pgraph/glsl/psh.h").read_text(), re.S).group()
helper = re.search(r"^static void psh_append_signed_g8b8_mips\(.*?^\}", source, re.M | re.S).group()
decoder = re.search(r'"float sign3\(float x\).*?^        "}\\n"', source, re.M | re.S).group()
decoder = "".join(ast.literal_eval(x) for x in re.findall(r'"(?:[^"\\]|\\.)*"', decoder))
fixture = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
enum ConvolutionFilter { CONV_DISABLED };
enum PshDepthFormat { DEPTH_DISABLED };
enum PshShadowDepthFunc { SHADOW_DISABLED };
/* STATE */
#include "hw/xbox/nv2a/pgraph/glsl/g8b8-lod.h"
typedef GString MString;
#define mstring_append_fmt g_string_append_printf
#define mstring_append g_string_append
struct PixelShader {const PshState *state;struct {bool vulkan;} opts;};
/* HELPER */
int main(int argc,char **argv) {
 assert(argc==7);
 unsigned api=atoi(argv[1]),minimum=atoi(argv[2]),filter=atoi(argv[3]);
 unsigned mixed=atoi(argv[4]),u=atoi(argv[5]),v=atoi(argv[6]);
 PshState state={0};state.g8b8_signed[0]=15|128|((filter==4||filter==6)?16:0)|(filter>=5?64:0);
 if(mixed)state.g8b8_mip_flags|=1;
 if(u==3&&v==3)state.g8b8_mip_flags|=16;
 else {if(u==3)state.g8b8_mip_clamp_u|=1;if(v==3)state.g8b8_mip_clamp_v|=1;}
 if(u==2)state.g8b8_mip_mirror|=1;if(v==2)state.g8b8_mip_mirror|=16;
 if(minimum==1){state.g8b8_mip_clamp_u|=16;state.g8b8_signed[0]|=32;}
 if(minimum>=4)psh_g8b8_set_extended_lod(&state,0,minimum,u,v);
 struct PixelShader ps={.state=&state,.opts={.vulkan=api}};
 MString *text=g_string_new("");psh_append_signed_g8b8_mips(&ps,text,0,state.g8b8_signed[0]);
 fputs(text->str,stdout);g_string_free(text,true);
}
""".replace("/* STATE */", state).replace("/* HELPER */", helper)
with tempfile.TemporaryDirectory(prefix="g8b8-mip-boundary-") as temporary:
    out = a.output.resolve() if a.output else Path(temporary)
    out.mkdir(parents=True, exist_ok=True)
    c = out / "emit.c"
    exe = out / ("emit.exe" if os.name == "nt" else "emit")
    c.write_text(fixture)
    subprocess.run([a.cc,"-std=gnu11","-O2","-I",str(r),*compat_glib_flags(r,a.cc),str(c),"-o",str(exe)],check=True)
    manifest = []
    for api in range(2):
        for minimum in (0,1,4):
            choices = ((3,False),(4,False),(6,False),(4,True),(5,True)) if minimum==0 else ((4,False),(6,False))
            for filter,mixed in choices:
                for u in range(1,4):
                    for v in range(1,4):
                        values = [api,minimum,filter,int(mixed),u,v]
                        stem = "-".join(map(str,values))
                        body = subprocess.check_output([str(exe),*map(str,values)],text=True)
                        # Supply a controlled host LOD query. Production code still performs
                        # level selection, lower clamping, addressing, decode and interpolation.
                        shader = "#version 450\nlayout(local_size_x=64) in;layout(binding=0) uniform sampler2D texSamp0;\nuniform vec2 probeQuery;\n#define textureQueryLod(s,c) probeQuery\nlayout(std430,binding=0) buffer Coords {vec4 coords[];};\nlayout(std430,binding=1) buffer Out {vec2 values[];};\n" + decoder + "void main(){uint i=gl_GlobalInvocationID.x;vec4 pT0=coords[i];\n" + body + "values[i]=signedGB0;}\n"
                        (out / (stem + ".comp")).write_text(shader)
                        manifest.append(stem + " " + " ".join(map(str,values)))
    (out / "manifest.txt").write_text("\n".join(manifest)+"\n")
    exe = out / ("mip-boundary.exe" if os.name == "nt" else "mip-boundary")
    cmd = [a.cc,"-O2","-Wall","-Wextra","-Werror",str(Path(__file__).with_suffix(".c")),"-o",str(exe),"-lm"]
    if os.name == "nt":
        deps = a.epoxy_root or r / "deps"
        cmd += ["-I",str(deps / "libepoxy/include"),str(deps / "libepoxy.a"),"-lopengl32","-lgdi32","-static"]
    else:
        cmd += ["-lEGL","-lGL"]
    subprocess.run(cmd,check=True)
    subprocess.run([str(exe),str(out)],check=True,env=dict(os.environ,EGL_PLATFORM="surfaceless"))
