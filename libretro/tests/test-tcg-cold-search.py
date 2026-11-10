# SPDX-License-Identifier: GPL-2.0-or-later
"""Round-trip production search metadata independently of the code body."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'accel/tcg/translate-all.c').read_text();parts=[]
for name in ['encode_sleb128','decode_sleb128','encode_search','cpu_unwind_data_from_tb']:
 m=re.search(r'^static (?:uint8_t \*|int64_t|int)\s*'+name+r'\(',s,re.M);assert m,name
 a=s.index('{',m.start());b=a+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 parts.append(s[m.start():b])
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define INSN_START_WORDS 3
#define GETPC_ADJ 2
#define CF_PCREL 1
#define unlikely(v) (v)
#if TEST_COLD
#define TCG_TARGET_COLD_CODE 1
#endif
typedef struct {struct {const uint8_t *ptr;unsigned size;} tc,xemu_cold;uint64_t pc;unsigned icount,flags;} TranslationBlock;
typedef struct {uint8_t *code_gen_highwater,*code_gen_cold_highwater;uint64_t gen_insn_data[384];uint16_t gen_insn_end_off[128];} TCGContext;
static TCGContext context,*tcg_ctx=&context;
static unsigned tb_cflags(TranslationBlock*t){return t->flags;}
/* PRODUCTION */
static uint64_t seed=0x12345678abcdef;
static uint64_t next(void){seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;return seed;}
int main(void){
 uint8_t hot[16384],cold[8192];unsigned checks=0;
 for(unsigned round=0;round<256;round++)for(unsigned relative=0;relative<2;relative++){
  memset(hot,0,sizeof(hot));memset(cold,0,sizeof(cold));
  TranslationBlock tb={.tc={hot+64,0},.xemu_cold={cold+64,64},.pc=next(),.icount=1+round%128,.flags=relative};
  uint8_t *table=cold+128;
  context.code_gen_highwater=TEST_COLD?hot+16:hot+sizeof(hot)-1024;
  context.code_gen_cold_highwater=cold+sizeof(cold)-1024;
  unsigned end=0;
  for(unsigned i=0;i<tb.icount;i++){
   for(unsigned j=0;j<3;j++)context.gen_insn_data[i*3+j]=next();
   end+=1+next()%31;context.gen_insn_end_off[i]=end;
  }
  tb.tc.size=end;
  if(!TEST_COLD){tb.xemu_cold.ptr=tb.tc.ptr+end;tb.tc.size+=64;table=(uint8_t*)tb.xemu_cold.ptr+64;}
  uint8_t *limit=TEST_COLD?context.code_gen_cold_highwater:context.code_gen_highwater;
  int size=encode_search(&tb,table);assert(size>0&&table+size<=limit);
  for(unsigned i=0;i<tb.icount;i++){
   unsigned begin=i?context.gen_insn_end_off[i-1]:0,last=context.gen_insn_end_off[i]-1;
   for(unsigned point=0;point<2;point++){
    uint64_t data[3];uintptr_t pc=(uintptr_t)tb.tc.ptr+(point?last:begin)+GETPC_ADJ;
    assert(cpu_unwind_data_from_tb(&tb,pc,data)==(int)(tb.icount-i));
    assert(!memcmp(data,context.gen_insn_data+i*3,sizeof(data)));checks++;
   }
  }
  uint64_t data[3];
  assert(cpu_unwind_data_from_tb(&tb,(uintptr_t)tb.tc.ptr-1+GETPC_ADJ,data)==-1);
  assert(cpu_unwind_data_from_tb(&tb,(uintptr_t)tb.tc.ptr+end+GETPC_ADJ,data)==-1);
  if(TEST_COLD)context.code_gen_cold_highwater=table;else context.code_gen_highwater=table;
  assert(encode_search(&tb,table)==-1);checks+=3;
 }
 printf("PASS: %u search/unwind checks, split=%d; raw 64-bit state, negative deltas, instruction boundaries and highwater\n",checks,TEST_COLD);
}
'''.replace('/* PRODUCTION */','\n'.join(parts))
if os.name!='nt':
 import resource
 resource.setrlimit(resource.RLIMIT_CORE,(0,0))
with tempfile.TemporaryDirectory(prefix='tcg-cold-search-') as d:
 p=Path(d);src=p/'test.c';exe=os.environ.get('COLD_SEARCH_BINARY',str(p/'test'))
 flags=shlex.split(os.environ.get('COLD_SEARCH_CFLAGS','-fsanitize=address,undefined'))
 args=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fwrapv']
 def build(text,split):
  src.write_text(text);subprocess.run(args+flags+['-DTEST_COLD='+str(split),str(src),'-o',exe],check=True)
 for split in map(int,os.environ.get('COLD_SEARCH_MODES','0,1').split(',')):
  build(code,split)
  if not os.environ.get('COLD_SEARCH_SKIP_RUN'):subprocess.run(shlex.split(os.environ.get('COLD_SEARCH_RUNNER',''))+[exe],check=True,timeout=60)
 if not os.environ.get('COLD_SEARCH_SKIP_RUN'):
  runner=shlex.split(os.environ.get('COLD_SEARCH_RUNNER',''))
  old='tb->xemu_cold.ptr + tb->xemu_cold.size';assert old in code
  build(code.replace(old,'tb->tc.ptr + tb->tc.size'),1)
  result=subprocess.run(runner+[exe],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
  assert result.returncode and b'Assertion' in result.stdout,result.stdout
  print('Required negative control rejected: search metadata follows body',flush=True)
