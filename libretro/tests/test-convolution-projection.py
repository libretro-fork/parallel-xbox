#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check production convolution texel spacing after projective division."""
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
with tempfile.TemporaryDirectory(prefix="convolution-projection-") as temporary:
    out = a.output.resolve() if a.output else Path(temporary)
    out.mkdir(parents=True, exist_ok=True)
    for version in ("parent", "candidate"):
        source = (subprocess.check_output(["git", "show", a.parent + ":hw/xbox/nv2a/pgraph/glsl/psh.c"], cwd=r, text=True)
                  if version == "parent" else (r / "hw/xbox/nv2a/pgraph/glsl/psh.c").read_text())
        fn = re.search(r"^static void apply_convolution_filter\(.*?^\}", source, re.M | re.S).group()
        fixture = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
typedef GString MString;
#define mstring_append_fmt g_string_append_printf
typedef struct {int dim_tex[4];bool rect_tex[4];} PshState;
struct PixelShader {const PshState *state;};
/* PRODUCTION */
int main(int argc,char **argv) {
 PshState state={.dim_tex={2},.rect_tex={argc>1}};struct PixelShader ps={.state=&state};
 MString *s=g_string_new("");apply_convolution_filter(&ps,s,0);fputs(s->str,stdout);g_string_free(s,true);
}
""".replace("/* PRODUCTION */", fn)
        c = out / (version + "-emit.c")
        exe = out / (version + ("-emit.exe" if os.name == "nt" else "-emit"))
        c.write_text(fixture)
        subprocess.run([a.cc,"-std=gnu11","-O2",*compat_glib_flags(r,a.cc),str(c),"-o",str(exe)],check=True)
        tables = re.search(r"^static void psh_append_convolution_tables\(.*?^\}", source, re.M | re.S)
        table = (tables.group() if tables else
                 source[source.index('"const float[9] gaussian3x3'):source.index('"vec2 remapCubeTo2D')])
        table = "".join(ast.literal_eval(x) for x in re.findall(r'"(?:[^"\\]|\\.)*"', table))
        for rect in range(2):
            body = subprocess.check_output([str(exe),*(["rect"] if rect else [])],text=True)
            normalizer = "vec3 norm0(vec3 p){return vec3(p.xy/vec2(textureSize(texSamp0,0)),p.z);}\n" if rect else ""
            shader = "#version 450\nlayout(local_size_x=64) in;layout(binding=0) uniform sampler2D texSamp0;\nlayout(std430,binding=1) buffer Coords {vec4 coords[];};\nlayout(std430,binding=2) buffer Out {vec4 values[];};\n" + table + normalizer + "void main(){uint i=gl_GlobalInvocationID.x;vec4 pT0=coords[i];\n" + body + "values[i]=t0;}\n"
            (out / f"{version}-{rect}.comp").write_text(shader)
    exe = out / ("boundary.exe" if os.name == "nt" else "boundary")
    cmd = [a.cc,"-O2","-Wall","-Wextra","-Werror",str(Path(__file__).with_suffix(".c")),"-o",str(exe),"-lm"]
    if os.name == "nt":
        cmd += ["-I",str((a.epoxy_root or r / "deps") / "libepoxy/include"),str((a.epoxy_root or r / "deps") / "libepoxy.a"),"-lopengl32","-lgdi32","-static"]
    else:
        cmd += ["-lEGL","-lGL"]
    subprocess.run(cmd,check=True)
    subprocess.run([str(exe),str(out)],check=True,env=dict(os.environ,EGL_PLATFORM="surfaceless"))
