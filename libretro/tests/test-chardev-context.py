# SPDX-License-Identifier: GPL-2.0-or-later
"""Check production read-handler context publication across handoffs."""
from pathlib import Path
import os, re, shlex, subprocess, sys, tempfile

repo=Path(sys.argv[1]).resolve()
cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
source=(repo/'chardev/char.c').read_text()
match=re.search(r'^void qemu_chr_be_update_read_handlers\(',source,re.M)
assert match
start=source.index('{',match.start());end=start+1;depth=1
while depth:
 depth+=(source[end]=='{')-(source[end]=='}');end+=1
body=source[match.start():end]
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
typedef struct {unsigned watches;bool alive;} GMainContext;
typedef struct Chardev Chardev;
typedef struct {void (*chr_update_read_handler)(Chardev *);} ChardevClass;
struct Chardev {GMainContext *gcontext,*watch_context;ChardevClass *klass;bool feature;};
static GMainContext contexts[4],*qemu_main_context=&contexts[0],*expected;
static unsigned updates;
#define CHARDEV_GET_CLASS(s) ((s)->klass)
#define QEMU_CHAR_FEATURE_GCONTEXT 1
static bool qemu_chr_has_feature(Chardev *s,int feature){assert(feature==1);return s->feature;}
static void update(Chardev *s){
 assert(s->gcontext==expected&&expected->alive);
 if(s->watch_context){assert(s->watch_context->watches);s->watch_context->watches--;}
 s->watch_context=s->gcontext;s->watch_context->watches++;updates++;
}
/* PRODUCTION */
int main(void){
 unsigned cases=0;
 for(unsigned old=0;old<4;old++)for(unsigned requested=0;requested<5;requested++)
 for(unsigned callback=0;callback<2;callback++){
  for(unsigned i=0;i<4;i++)contexts[i]=(GMainContext){.alive=true};
  ChardevClass klass={.chr_update_read_handler=callback?update:NULL};
  Chardev s={.gcontext=&contexts[old],.watch_context=callback?&contexts[old]:NULL,.klass=&klass,.feature=true};
  if(callback)contexts[old].watches++;
  GMainContext *request=requested==4?NULL:&contexts[requested];
  expected=request?request:qemu_main_context;updates=0;
  qemu_chr_be_update_read_handlers(&s,request);
  assert(s.gcontext==expected&&updates==callback);
  for(unsigned i=0;i<4;i++)assert(contexts[i].watches==(callback && &contexts[i]==expected));
  if(callback){s.watch_context->watches--;s.watch_context=NULL;}
  /* Return from an explicit owner with no GCONTEXT support and NULL:
   * this is permitted by the API's feature assertion. */
  s.feature=false;expected=qemu_main_context;
  qemu_chr_be_update_read_handlers(&s,NULL);
  assert(s.gcontext==expected);
  for(unsigned i=1;i<4;i++){assert(!contexts[i].watches);contexts[i].alive=false;}
  if(callback){assert(s.watch_context==qemu_main_context);s.watch_context->watches--;}
  assert(!qemu_main_context->watches);cases++;
 }
 printf("PASS: %u context handoffs, callback publication, main-loop return and retired-owner teardown cases\n",cases);
}
'''.replace('/* PRODUCTION */',body)
if os.name!='nt':
 import resource
 resource.setrlimit(resource.RLIMIT_CORE,(0,0))
with tempfile.TemporaryDirectory(prefix='chardev-context-') as d:
 p=Path(d);src=p/'test.c';exe=os.environ.get('CHAR_CONTEXT_BINARY',str(p/'test'))
 args=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror']
 flags=shlex.split(os.environ.get('CHAR_CONTEXT_CFLAGS','-fsanitize=undefined -fsanitize-undefined-trap-on-error'))
 def build(text):
  src.write_text(text);subprocess.run(args+flags+[str(src),'-o',exe],check=True)
 build(code)
 if not os.environ.get('CHAR_CONTEXT_SKIP_RUN'):
  runner=shlex.split(os.environ.get('CHAR_CONTEXT_RUNNER',''))
  subprocess.run(runner+[exe],check=True,timeout=30)
  for label,replacement in [('NULL retains retiring owner','if (context) s->gcontext = context;'),('NULL selects unpolled default','s->gcontext = context;')]:
   old='s->gcontext = context ? context : qemu_main_context;'
   assert old in code
   build(code.replace(old,replacement))
   result=subprocess.run(runner+[exe],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
   assert result.returncode and b'Assertion' in result.stdout,(label,result.stdout)
   print('Required negative control rejected:',label,flush=True)
