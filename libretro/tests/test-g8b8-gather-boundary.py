#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the production base-level G8B8 gather around texel boundaries."""
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
p.add_argument("--parent", default="HEAD")
p.add_argument("--cc", default="gcc")
p.add_argument("--output", type=Path)
p.add_argument("--epoxy-root", type=Path, help="native Windows libepoxy build directory")
a = p.parse_args()
r = a.checkout.resolve()
with tempfile.TemporaryDirectory(prefix="g8b8-boundary-") as temporary:
    out = a.output.resolve() if a.output else Path(temporary)
    out.mkdir(parents=True, exist_ok=True)
    for version in ("parent", "candidate"):
        source = (subprocess.check_output(["git", "show", a.parent + ":hw/xbox/nv2a/pgraph/glsl/psh.c"], cwd=r, text=True)
                  if version == "parent" else (r / "hw/xbox/nv2a/pgraph/glsl/psh.c").read_text())
        fn = re.search(r"^static void psh_append_signed_g8b8\(.*?^\}", source, re.M | re.S).group()
        decoder = re.search(r'"float sign3\(float x\).*?^        "}\\n"', source, re.M | re.S).group()
        decoder = "".join(ast.literal_eval(x) for x in re.findall(r'"(?:[^"\\]|\\.)*"', decoder))
        fixture = r"""
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
typedef GString MString;
#define mstring_append_fmt g_string_append_printf
typedef struct {unsigned char g8b8_signed[4],g8b8_mip_flags;bool rect_tex[4];} PshState;
struct PixelShader {const PshState *state;struct {bool vulkan;} opts;};
static void psh_append_signed_g8b8_mips(const struct PixelShader *p,MString *s,int i,unsigned f){abort();}
/* PRODUCTION */
int main(void) {
 PshState state={.g8b8_signed={31}};struct PixelShader ps={.state=&state};
 MString *s=g_string_new("");psh_append_signed_g8b8(&ps,s,0);fputs(s->str,stdout);g_string_free(s,true);
}
""".replace("/* PRODUCTION */", fn)
        c = out / (version + "-emit.c")
        exe = out / (version + ("-emit.exe" if os.name == "nt" else "-emit"))
        c.write_text(fixture)
        subprocess.run([a.cc,"-std=gnu11","-O2",*compat_glib_flags(r,a.cc),str(c),"-o",str(exe)],check=True)
        body = subprocess.check_output([str(exe)],text=True)
        shader = "#version 450\nlayout(local_size_x=64) in;layout(binding=0) uniform sampler2D texSamp0;\nlayout(std430,binding=0) buffer Coords {vec4 coords[];};\nlayout(std430,binding=1) buffer Out {vec2 values[];};\n" + decoder + "void main(){uint i=gl_GlobalInvocationID.x;vec4 pT0=coords[i];vec4 t0=textureLod(texSamp0,pT0.xy,0.0);\n" + body + "values[i]=signedGB0;}\n"
        (out / (version + ".comp")).write_text(shader)
    exe = out / ("boundary.exe" if os.name == "nt" else "boundary")
    cmd = [a.cc,"-O2","-Wall","-Wextra","-Werror",str(Path(__file__).with_suffix(".c")),"-o",str(exe),"-lm"]
    if os.name == "nt":
        cmd += ["-I",str((a.epoxy_root or r / "deps") / "libepoxy/include"),str((a.epoxy_root or r / "deps") / "libepoxy.a"),"-lopengl32","-lgdi32","-static"]
    else:
        cmd += ["-lEGL","-lGL"]
    subprocess.run(cmd,check=True)
    subprocess.run([str(exe),str(out / "parent.comp"),str(out / "candidate.comp")],check=True,env=dict(os.environ,EGL_PLATFORM="surfaceless"))
