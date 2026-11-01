#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production WGL version negotiation without a GPU."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/thirdparty/gloffscreen/gloffscreen_libretro.c').read_text()
fn=re.search(r'^static HGLRC create_shared_core_context\(.*?^\}',s,re.M|re.S).group()
code=r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); _Exit(1); } } while(0)
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 1
typedef void *HDC;
typedef void *HGLRC;
static HDC expected_dc;
static HGLRC expected_share;
static unsigned calls;
static int accept_minor;
static HGLRC create(HDC dc,HGLRC share,const int *a) {
 static const int sequence[]={5,3,1,0};
 assert(dc==expected_dc && share==expected_share && calls<4);
 assert(a[0]==WGL_CONTEXT_MAJOR_VERSION_ARB && a[1]==4 && a[2]==WGL_CONTEXT_MINOR_VERSION_ARB);
 assert(a[3]==sequence[calls] && a[4]==WGL_CONTEXT_PROFILE_MASK_ARB && a[5]==WGL_CONTEXT_CORE_PROFILE_BIT_ARB && !a[6]);
 calls++;
 return a[3]==accept_minor?(HGLRC)(uintptr_t)(a[3]+1):NULL;
}
static HGLRC (*p_wglCreateContextAttribsARB)(HDC,HGLRC,const int *)=create;
/* PRODUCTION */
int main(void) {
 const int minors[]={5,3,1,0,-1};
 for(unsigned shared=0;shared<2;shared++)for(unsigned i=0;i<5;i++) {
  expected_dc=(HDC)(uintptr_t)0x1234;expected_share=shared?(HGLRC)(uintptr_t)0x5678:NULL;
  accept_minor=minors[i];calls=0;
  HGLRC context=create_shared_core_context(expected_dc,expected_share);
  assert(calls==(i<4?i+1:4));
  assert(context==(i<4?(HGLRC)(uintptr_t)(minors[i]+1):NULL));
 }
 puts("WGL context negotiation: 4.5/4.3/4.1/4.0 fallbacks, unsupported drivers and preserved share/DC arguments PASS");
}
'''.replace('/* PRODUCTION */',fn)
with tempfile.TemporaryDirectory(prefix='wgl-core-versions-') as tmp:
 d=Path(tmp);c=d/'test.c';c.write_text(code);exe=d/('test.exe' if os.name=='nt' else 'test')
 env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',*shlex.split(os.environ.get('WGL_CORE_CFLAGS','')),str(c),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env,timeout=30)
