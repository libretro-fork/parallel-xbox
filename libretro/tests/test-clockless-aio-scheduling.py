# SPDX-License-Identifier: GPL-2.0-or-later
"""Queued bottom halves are ready work, never host-time delays."""
from pathlib import Path
import subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'util/async.c').read_text();a=s.index('static int64_t aio_compute_bh_timeout');b=s.index('static gboolean',a)
flags=s[s.index('enum {'):s.index('struct QEMUBH {')]
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
/* FLAGS */
typedef struct QEMUBH {int flags;struct QEMUBH*next;} QEMUBH;
typedef struct {QEMUBH*first;} BHList;
typedef struct BHListSlice {BHList bh_list;struct BHListSlice*next;} BHListSlice;
typedef struct {BHList bh_list;struct {BHListSlice*first;} bh_slice_list;} AioContext;
#define QSLIST_FOREACH_RCU(p,h,field) for(p=(h)->first;p;p=p->field)
#define QSIMPLEQ_FOREACH(p,h,field) for(p=(h)->first;p;p=p->field)
static int retro_atomic_load_acquire_int(int*p){return *p;}
/* PRODUCTION */
int main(void){
 AioContext ctx={0};assert(aio_compute_timeout(&ctx)==-1);unsigned cases=0;
 for(int first=0;first<32;first++)for(int second=0;second<32;second++)for(int placement=0;placement<3;placement++){
  QEMUBH a={first,0},b={second,0};BHListSlice x={{0},0},y={{0},0};ctx=(AioContext){0};
  if(placement==0){ctx.bh_list.first=&a;a.next=&b;}
  if(placement==1){ctx.bh_list.first=&a;ctx.bh_slice_list.first=&x;x.bh_list.first=&b;}
  if(placement==2){ctx.bh_slice_list.first=&x;x.next=&y;x.bh_list.first=&a;y.bh_list.first=&b;}
  int runnable=((first&BH_SCHEDULED)&&!(first&BH_DELETED))||((second&BH_SCHEDULED)&&!(second&BH_DELETED));
  assert(aio_compute_timeout(&ctx)==(runnable?0:-1));cases++;
 }
 printf("AIO scheduling: %u flag/list/slice cases; idle work never creates a host timeout\n",cases);
}
'''
code=code.replace('/* FLAGS */',flags).replace('/* PRODUCTION */',s[a:b])
with tempfile.TemporaryDirectory(prefix='aio-clockless-') as t:
 p=Path(t);(p/'test.c').write_text(code);exe=p/'test'
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,timeout=30)
