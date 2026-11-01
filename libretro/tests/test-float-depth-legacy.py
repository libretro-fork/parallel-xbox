#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Keep non-F16 shadow GLSL byte-identical to the supplied parent revision."""
from pathlib import Path
import re,subprocess,sys,tempfile
from glib_fixture import compat_glib_flags
r=Path(sys.argv[1]).resolve();parent=sys.argv[2];cc=sys.argv[3] if len(sys.argv)>3 else "cc"
source=(r/"hw/xbox/nv2a/pgraph/glsl/psh.c").read_text()
old=subprocess.check_output(["git","show",parent+":hw/xbox/nv2a/pgraph/glsl/psh.c"],cwd=r,text=True)
fn=lambda s,name:re.search(r"^static void "+name+r"\(.*?^\}",s,re.M|re.S).group()
state=re.search(r"typedef struct PshState \{.*?\} PshState;",(r/"hw/xbox/nv2a/pgraph/glsl/psh.h").read_text(),re.S).group()
table=re.search(r"static const char \*shadow_comparison_map\[\].*?\};",source,re.S).group()
code=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include "hw/xbox/nv2a/pgraph/psh_regs.h"
enum PshDepthFormat { DEPTH_DISABLED };
/* STATE */
typedef GString MString;
#define mstring_append g_string_append
#define mstring_append_fmt g_string_append_printf
struct PixelShader {const PshState *state;struct {bool vulkan;} opts;};
/* PRODUCTION */
int main(void) {
 unsigned cases=0;
 for(unsigned stage=0;stage<4;stage++)
 for(unsigned func=0;func<8;func++)
 for(unsigned rect=0;rect<2;rect++)
 for(unsigned x24=0;x24<2;x24++)
 for(unsigned vk=0;vk<2;vk++)
 for(unsigned compare=0;compare<2;compare++) {
  PshState state={0};state.shadow_map[stage]=1;state.shadow_depth_func=func;
  state.rect_tex[stage]=rect;state.tex_x8y24[stage]=x24;
  struct PixelShader ps={.state=&state,.opts={vk}};
  GString *a=g_string_new(NULL),*b=g_string_new(NULL);
  old_shadow(&ps,stage,compare,a);new_shadow(&ps,stage,compare,b);
  assert(!strcmp(a->str,b->str));g_string_free(a,true);g_string_free(b,true);cases++;
 }
 printf("PASS: %u non-F16 shadow GLSL snippets remain byte-identical to parent\n",cases);
}
""".replace("/* STATE */",state).replace("/* PRODUCTION */",table+"\n"+fn(source,"psh_append_f16_shadow")+"\n"+fn(old,"psh_append_shadowmap").replace("psh_append_shadowmap","old_shadow")+"\n"+fn(source,"psh_append_shadowmap").replace("psh_append_shadowmap","new_shadow"))
with tempfile.TemporaryDirectory(prefix="f16-legacy-") as folder:
 c=Path(folder)/"legacy.c";exe=Path(folder)/"legacy";c.write_text(code)
 subprocess.run([cc,"-std=gnu11","-O2","-Wall","-Wextra","-Werror","-I",str(r),str(c),*compat_glib_flags(r,cc),"-o",str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
