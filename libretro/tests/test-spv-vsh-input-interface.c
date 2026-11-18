/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spirv/unified1/spirv.h>
#include "hw/xbox/nv2a/pgraph/spv/vsh.h"
bool reference_vsh(const VshState *, SpvVshOptions, GlslcResult *);
static void release(GlslcResult *r){glslc_result_free_words(r);free(r->uniforms.members);free(r->push_constants.members);}
static bool representative(unsigned m){return m==0||m==65535||m==0x5555||m==0xaaaa||__builtin_popcount(m)==1||__builtin_popcount(m)==15;}
static void layout_equal(const GlslcLayout *a,const GlslcLayout *b){assert(a->count==b->count&&a->size==b->size);for(size_t i=0;i<a->count;i++){const GlslcUniform *x=&a->members[i],*y=&b->members[i];assert(!strcmp(x->name,y->name)&&x->dim_v==y->dim_v&&x->dim_a==y->dim_a&&x->stride==y->stride&&x->offset==y->offset);}}
static void inspect(const GlslcResult *a,const GlslcResult *b,unsigned mask){
 uint32_t ids[16]={0},entry[40]={0},input_ids[16],decor_ids[32],decor_loc[32];unsigned interfaces=0,entries=0,ni=0,nd=0;
 assert(!memcmp(a->words,b->words,5*sizeof(uint32_t)));
 for(size_t i=5;i<a->num_words;){uint32_t n=a->words[i]>>16,op=a->words[i]&65535;assert(n&&i+n<=a->num_words);
  if(op==SpvOpDecorate&&n==4&&a->words[i+2]==SpvDecorationLocation&&a->words[i+3]<16){assert(nd<32);decor_ids[nd]=a->words[i+1];decor_loc[nd++]=a->words[i+3];}
  if(op==SpvOpVariable&&n>=4&&a->words[i+3]==SpvStorageClassInput){assert(ni<16);input_ids[ni++]=a->words[i+2];}
  if(op==SpvOpEntryPoint){assert(n>=5&&n-5<=40);interfaces=n-5;memcpy(entry,a->words+i+5,interfaces*4);entries++;}i+=n;
 }
 assert(entries==1&&ni==16);
 for(unsigned k=0;k<ni;k++)for(unsigned j=0;j<nd;j++)if(input_ids[k]==decor_ids[j])ids[decor_loc[j]]=input_ids[k];
 for(unsigned loc=0;loc<16;loc++){assert(ids[loc]);unsigned found=0;for(unsigned j=0;j<interfaces;j++)found+=entry[j]==ids[loc];assert(found==!(mask&(1u<<loc)));}
 /* The ONLY allowed word change is the entry-point interface. This proves
  * identical input(), arithmetic, exotic-bit handling, IDs and final owners. */
 size_t x=5,y=5;while(x<a->num_words&&y<b->num_words){unsigned na=a->words[x]>>16,nb=b->words[y]>>16;assert(na&&nb);if((a->words[x]&65535)==SpvOpEntryPoint){assert((b->words[y]&65535)==SpvOpEntryPoint);assert(nb-na==(unsigned)__builtin_popcount(mask));assert(!memcmp(a->words+x+1,b->words+y+1,4*sizeof(uint32_t)));}else{assert(na==nb&&!memcmp(a->words+x,b->words+y,na*4));}x+=na;y+=nb;}assert(x==a->num_words&&y==b->num_words);
 layout_equal(&a->uniforms,&b->uniforms);layout_equal(&a->push_constants,&b->push_constants);
}
int main(int argc,char **argv){assert(argc==2);unsigned count=0,exports=0;
 for(unsigned ff=0;ff<2;ff++)for(unsigned push=0;push<2;push++)for(unsigned mask=0;mask<65536;mask++){
  VshState s={0};s.uniform_attrs=mask;s.is_fixed_function=ff;s.smooth_shading=mask&1;s.compressed_attrs=(~mask)&0x4444;s.swizzle_attrs=(~mask)&0x8888;
  if(!ff){s.programmable.program_length=1;uint32_t *t=s.programmable.program_data[0];t[1]=(MAC_MOV<<21)|(32u<<13)|((mask&15)<<9)|0x1b;t[2]=(PARAM_V<<26)|(PARAM_C<<11)|(0x1bu<<17)|(0x1bu<<2);t[3]=(PARAM_V<<28)|(15u<<24)|(2u<<20)|(15u<<16)|(15u<<12)|(OUTPUT_O<<11)|1u;}
  SpvVshOptions o={.push_constants=push,.preserve_float32=true,.spirv_1_6=!!(mask&2)};
  GlslcResult a,b;assert(pgraph_spv_vsh(&s,o,&a)&&reference_vsh(&s,o,&b));inspect(&a,&b,mask);
  if(representative(mask)){char file[1024];int n=snprintf(file,sizeof(file),"%s/interface-%u-%u-%04x.spv",argv[1],ff,push,mask);assert(n>0&&(size_t)n<sizeof(file));FILE *f=fopen(file,"wb");assert(f&&fwrite(a.words,4,a.num_words,f)==a.num_words&&!fclose(f));exports++;}
  release(&a);release(&b);count++;
 }
 printf("Vertex input interfaces: %u modules (all65536 masks x FF/prog x push0/1); pinned baseline instructions/layouts identical, %u representative SPIR-V exports PASS\n",count,exports);return 0;
}
