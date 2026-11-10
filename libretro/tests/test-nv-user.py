# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production NV_USER handlers over channels, offsets and FIFO state."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/user.c').read_text()
parts=[]
for name in ['user_read','user_write']:
 m=re.search(r'^(?:uint64_t|void) '+name+r'\(',s,re.M);assert m
 a=s.index('{',m.start());b=a+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 parts.append(s[m.start():b])
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hw/xbox/nv2a/nv2a_regs.h"
typedef uint64_t hwaddr;
typedef struct {struct {int regs[0x2000];} pfifo;} NV2AState;
typedef struct {bool lr_poll_any;} CPUState;
static CPUState cpu,*current_cpu;
static unsigned kicks,stores,release_stores,read_logs,write_logs;
#define ctz32(v) __builtin_ctz(v)
static uint32_t pfifo_reg_r(NV2AState*d,unsigned reg){assert(reg<0x2000);return (uint32_t)d->pfifo.regs[reg];}
static void pfifo_reg_w(NV2AState*d,unsigned reg,uint32_t val){
 assert(reg==NV_PFIFO_CACHE1_DMA_GET||reg==NV_PFIFO_CACHE1_REF);
 d->pfifo.regs[reg]=(int)val;stores++;
}
static void retro_atomic_store_release_int(int *p,int v){*p=v;stores++;release_stores++;}
static void pfifo_kick_guest(NV2AState*d){(void)d;assert(stores==1);kicks++;}
static void nv2a_reg_log_read(unsigned unit,hwaddr addr,unsigned size,uint64_t val){(void)addr;(void)val;assert(unit==NV_USER&&size==4);read_logs++;}
static void nv2a_reg_log_write(unsigned unit,hwaddr addr,unsigned size,uint64_t val){(void)addr;(void)val;assert(unit==NV_USER&&size==4);write_logs++;}
/* PRODUCTION */
static void one(unsigned channel,unsigned offset,bool dma,unsigned active,bool have_cpu,bool busy){
 static NV2AState d;
 uint32_t put=0x12345678,get=busy?0x87654321:put,ref=0xabcdef01;
 d.pfifo.regs[NV_PFIFO_MODE]=dma&&channel<32?(int)(1u<<channel):0;
 d.pfifo.regs[NV_PFIFO_CACHE1_PUSH1]=(int)active;
 d.pfifo.regs[NV_PFIFO_CACHE1_DMA_PUT]=(int)put;
 d.pfifo.regs[NV_PFIFO_CACHE1_DMA_GET]=(int)get;
 d.pfifo.regs[NV_PFIFO_CACHE1_REF]=(int)ref;
 int before[0x2000];
 bool audit=offset%1024==0||offset==NV_USER_DMA_PUT||offset==NV_USER_DMA_GET||offset==NV_USER_REF;
 if(audit)memcpy(before,d.pfifo.regs,sizeof(before));
 current_cpu=have_cpu?&cpu:NULL;cpu.lr_poll_any=false;
 kicks=stores=release_stores=read_logs=write_logs=0;
 bool selected=channel<32&&dma&&channel==active;
 bool known=offset==NV_USER_DMA_PUT||offset==NV_USER_DMA_GET||offset==NV_USER_REF;
 uint64_t expected=!selected?0:offset==NV_USER_DMA_PUT?put:offset==NV_USER_DMA_GET?get:offset==NV_USER_REF?ref:0;
 hwaddr addr=((hwaddr)channel<<16)|offset;
 assert(user_read(&d,addr,4)==expected);
 assert(cpu.lr_poll_any==(have_cpu&&selected&&offset==NV_USER_DMA_GET&&busy));
 assert(read_logs==1&&!stores&&!kicks);
 if(audit)assert(!memcmp(before,d.pfifo.regs,sizeof(before)));
 uint64_t val=UINT64_C(0xdeadbeefcafebabe);
 user_write(&d,addr,val,4);
 if(selected&&known){
  unsigned reg=offset==NV_USER_DMA_PUT?NV_PFIFO_CACHE1_DMA_PUT:offset==NV_USER_DMA_GET?NV_PFIFO_CACHE1_DMA_GET:NV_PFIFO_CACHE1_REF;
  if(audit)before[reg]=(int)(uint32_t)val;
  if(offset==NV_USER_DMA_PUT)put=(uint32_t)val;
  if(offset==NV_USER_DMA_GET)get=(uint32_t)val;
  if(offset==NV_USER_REF)ref=(uint32_t)val;
 }
 assert(stores==(selected&&known)&&kicks==stores&&write_logs==1);
 assert(release_stores==(selected&&offset==NV_USER_DMA_PUT));
 assert(pfifo_reg_r(&d,NV_PFIFO_CACHE1_DMA_PUT)==put);
 assert(pfifo_reg_r(&d,NV_PFIFO_CACHE1_DMA_GET)==get);
 assert(pfifo_reg_r(&d,NV_PFIFO_CACHE1_REF)==ref);
 if(audit)assert(!memcmp(before,d.pfifo.regs,sizeof(before)));
}
int main(void){
 unsigned cases=0;
 /* Exhaust all mapped channel IDs and every register offset with both
  * enabled and disabled DMA. Every write must touch exactly its owner. */
 for(unsigned channel=0;channel<128;channel++)for(unsigned dma=0;dma<2;dma++)
 for(unsigned offset=0;offset<65536;offset++){
  one(channel,offset,dma,channel%32,true,true);cases++;
 }
 const unsigned offsets[]={0,0x3c,0x40,0x44,0x48,0x50,0xffff};
 for(unsigned channel=0;channel<32;channel++)for(unsigned active=0;active<32;active++)
 for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++)
 for(unsigned have_cpu=0;have_cpu<2;have_cpu++)for(unsigned busy=0;busy<2;busy++){
  one(channel,offsets[i],true,active,have_cpu,busy);cases++;
 }
 printf("PASS: %u NV_USER read/write cases; channel/offset bounds, inactive/PIO access, register ownership, release PUT, kicks and GET polling\n",cases);
}
'''.replace('/* PRODUCTION */','\n'.join(parts))
# Full register-owner comparisons cover every supported offset and sampled
# gaps; all offsets also check write/kick counts and each DMA register.
if os.name!='nt':
 import resource
 resource.setrlimit(resource.RLIMIT_CORE,(0,0))
with tempfile.TemporaryDirectory(prefix='nv-user-') as d:
 p=Path(d);src=p/'test.c';exe=os.environ.get('NV_USER_BINARY',str(p/'test'))
 flags=shlex.split(os.environ.get('NV_USER_CFLAGS','-fsanitize=undefined -fsanitize-undefined-trap-on-error'))
 args=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(r)]
 def build(text):
  src.write_text(text);subprocess.run(args+flags+[str(src),'-o',exe],check=True)
 build(code)
 if not os.environ.get('NV_USER_SKIP_RUN'):
  runner=shlex.split(os.environ.get('NV_USER_RUNNER',''))
  subprocess.run(runner+[exe],check=True,timeout=120)
  for label,old,new in [
   ('out-of-range channel abort','if (channel_id >= NV2A_NUM_CHANNELS)', 'assert(channel_id < NV2A_NUM_CHANNELS); if (channel_id >= NV2A_NUM_CHANNELS)'),
   ('unsupported writes kick FIFO','/* Unsupported register: do not publish work to PFIFO. */\n                return;', 'break;'),
   ('GET polling detection lost','current_cpu->lr_poll_any = true;', 'current_cpu->lr_poll_any = false;')]:
   assert old in code,label
   build(code.replace(old,new))
   result=subprocess.run(runner+[exe],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120)
   assert result.returncode and b'Assertion' in result.stdout,(label,result.stdout)
   print('Required negative control rejected:',label,flush=True)
