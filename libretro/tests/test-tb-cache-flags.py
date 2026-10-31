# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute production flag-check IR against an independent identity oracle."""
from pathlib import Path
import subprocess,sys,tempfile,re
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'accel/tcg/translator.c').read_text();a=s.index('static void gen_tb_flags_check(');i=s.index('{',a)+1;depth=1
while depth:
 depth+=(s[i]=='{')-(s[i]=='}');i+=1
body=s[a:i]
mask=re.search(r'^#define XEMU_TB_CF_ANY .*$',(r/'include/qemu/cpu-boost.h').read_text(),re.M)[0]
source=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
typedef int TCGv_ptr;
typedef int TCGv_i32;
typedef int TCGLabel;
typedef struct { uint32_t flags; } TranslationBlock;
enum { TCG_COND_EQ,TCG_COND_NE };
static uint32_t v[8],stored;
static unsigned next,ops;
static bool rejected;
static void tcg_gen_ld_i32(int d,int p,size_t off) { (void)p;assert(off==offsetof(TranslationBlock,flags));v[d]=stored;ops++; }
static void tcg_gen_xori_i32(int d,int a,uint32_t b) { v[d]=v[a]^b;ops++; }
static int tcg_constant_i32(uint32_t x) { assert(next<8);v[next]=x;return next++; }
static void tcg_gen_movcond_i32(int c,int d,int a,int b,int t,int f) { assert(c==TCG_COND_EQ);v[d]=v[a]==v[b]?v[t]:v[f];ops++; }
static void tcg_gen_brcondi_i32(int c,int a,uint32_t b,int *label) { (void)label;assert(c==TCG_COND_NE);rejected|=v[a]!=b;ops++; }
/* PRODUCTION */
static uint32_t cls(unsigned n) { return ((n&3u)<<30)|((n>>2)<<2); }
int main(void) {
 uint32_t seed=0x12345678;unsigned cases=0;
 for(unsigned i=0;i<1024;i++) {
  seed=seed*1664525u+1013904223u;
  uint32_t base=seed&~XEMU_TB_CF_ANY;
  for(unsigned want=0;want<8;want++) for(unsigned have=0;have<8;have++)
  for(int changed=-1;changed<32;changed++) {
   if(changed>=0 && (XEMU_TB_CF_ANY&(1u<<changed))) continue;
   uint32_t requested=base|cls(want);
   stored=(base|cls(have))^(changed<0?0:1u<<changed);
   bool expected=stored==requested || stored==((requested&~XEMU_TB_CF_ANY)|XEMU_TB_CF_ANY);
   next=1;ops=0;rejected=false;TCGLabel slow=0;
   gen_tb_flags_check(0,0,requested,&slow);
   assert(rejected==!expected);
   assert(ops==(want==7?2u:4u));
   cases++;
  }
 }
 printf("TB cache flags: %u exact/ANY/class/mode mismatch cases passed\n",cases);
}
'''.replace('/* PRODUCTION */',mask+'\n'+body)
with tempfile.TemporaryDirectory(prefix='tb-cache-flags-') as td:
 p=Path(td);(p/'test.c').write_text(source);exe=p/'test.exe'
 subprocess.run([cc,'-O2','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
