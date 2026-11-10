# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare allocator capacity; requires parent commit 8684dae402 locally."""
from pathlib import Path
import subprocess,re,tempfile,sys
r=Path(sys.argv[1]).resolve()
namespace={'__file__':str(r/'libretro/tests/test-tcg-cold-layout.py')}
script=(r/'libretro/tests/test-tcg-cold-layout.py').read_text()
import sys
sys.argv=['capacity',str(r)]
exec(script[:script.index("if os.name!='nt':")],namespace)
code=namespace['code'];code=code[:code.index('int main(void)')]
parent=subprocess.check_output(['git','show','8684dae402:tcg/tcg.c'],cwd=r,text=True)
a=parent.index('TranslationBlock *tcg_tb_alloc(');b=parent.index('\nvoid tcg_prologue_init',a)
parent=parent[a:b].replace('tcg_tb_alloc','parent_tb_alloc')
main=r'''
static unsigned capacity(uint8_t*base,unsigned hot,unsigned cold,bool split){
 TCGContext s={.code_gen_buffer=base,.code_gen_buffer_size=8*TCG_COLD_CHUNK_SIZE};
 if(split)assert(tcg_region_chunk_init(&s,base));
 else{s.code_gen_ptr=base;s.code_gen_highwater=base+s.code_gen_buffer_size-TCG_HIGHWATER;}
 unsigned count=0;region_requests=0;
 while(true){
  TranslationBlock*t=split?tcg_tb_alloc(&s):parent_tb_alloc(&s);if(!t)break;
  if(split){
   if((uint8_t*)s.code_gen_ptr+hot>(uint8_t*)s.code_gen_highwater ||
      (uint8_t*)s.code_gen_cold_ptr+cold>(uint8_t*)s.code_gen_cold_highwater){
    s.code_buf=s.code_gen_ptr;tcg_code_overflow(&s);tcg_code_gen_chunk_full(&s);continue;
   }
   s.code_gen_ptr=(void*)ROUND_UP((uintptr_t)s.code_gen_ptr+hot,16);
   s.code_gen_cold_ptr=(void*)ROUND_UP((uintptr_t)s.code_gen_cold_ptr+cold,16);
  }else{
   if((uint8_t*)s.code_gen_ptr+hot+cold>(uint8_t*)s.code_gen_highwater)break;
   s.code_gen_ptr=(void*)ROUND_UP((uintptr_t)s.code_gen_ptr+hot+cold,16);
  }
  count++;
 }
 return count;
}
int main(void){
 uint8_t*raw=malloc(8*TCG_COLD_CHUNK_SIZE+128);assert(raw);uint8_t*base=(void*)ROUND_UP((uintptr_t)raw,128);
 unsigned bodies[]={16,64,128,256,512,1024,4096},tails[]={16,32,64,128,256,512,4096};
 puts("body_bytes,cold_bytes,parent_blocks,split_blocks,capacity_delta_percent");
 for(unsigned i=0;i<7;i++)for(unsigned j=0;j<7;j++){
  unsigned p=capacity(base,bodies[i],tails[j],false),c=capacity(base,bodies[i],tails[j],true);
  printf("%u,%u,%u,%u,%.2f\n",bodies[i],tails[j],p,c,100.0*((double)c/p-1));
 }
 free(raw);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code+parent+main)
 subprocess.run(['cc','-O2','-std=gnu11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 result=subprocess.check_output([str(p/'test')],text=True)
 # CSV goes to stdout; repository and input code remain unchanged.
 print(result)
