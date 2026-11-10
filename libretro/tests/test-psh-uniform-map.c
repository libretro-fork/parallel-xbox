/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"uniform route line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static unsigned profile_queries,allocations;
static void *test_allocate(size_t n){allocations++;return malloc(n);}
#define malloc test_allocate
/* PRODUCTION */
#undef malloc
int main(void){
 static const unsigned modes[]={PS_TEXTUREMODES_NONE,PS_TEXTUREMODES_PROJECT2D,PS_TEXTUREMODES_PROJECT3D,PS_TEXTUREMODES_BUMPENVMAP,PS_TEXTUREMODES_BUMPENVMAP_LUM,PS_TEXTUREMODES_DPNDNT_AR,PS_TEXTUREMODES_DOT_STR_3D,PS_TEXTUREMODES_CUBEMAP};
 unsigned cases=0;
 for(unsigned generation=0;generation<16384;generation++){
  PshState state={.alpha_dynamic=!!(generation&1),.combiner_dynamic=!!(generation&2)};
  for(unsigned j=0;j<4;j++){
   unsigned f=(generation*197u+j*31u)>>(j*2);
   state.shader_stage_program|=modes[f%8]<<(j*5);state.dim_tex[j]=2;
   state.rect_tex[j]=!!(f&8);state.border_adjust[j]=!!(f&16);state.colorkey_mode[j]=(f>>5)%4;
   state.shadow_map[j]=!!(f&128);state.shadow_depth_func=SHADOW_DEPTH_FUNC_LESS;
  }
  unsigned expected[UNIFORMS+1],member=0;
  for(unsigned j=0;j<UNIFORMS;j++){expected[j]=member;member+=uniform_enabled(&state,j);}
  expected[UNIFORMS]=member;
  struct {uint64_t before;UniformMap map;uint64_t after;} owner={.before=0x123456789abcdef0ull,.after=0xfedcba9876543210ull};
  memset(&owner.map,0xa5,sizeof(owner.map));profile_queries=0;
  init_uniform_map(&state,&owner.map);CHECK(profile_queries==UNIFORMS);
  for(unsigned j=0;j<=UNIFORMS;j++)CHECK(owner.map.members[j]==expected[j]);
  CHECK(owner.before==0x123456789abcdef0ull&&owner.after==0xfedcba9876543210ull);
  uint32_t words[16];Writer w={.words=words,.capacity=16,.next=1000};
  Context c={.w=&w,.uniform_map=&owner.map}; /* Routing borrows no source-key bytes. */
  for(unsigned j=0;j<32;j++)c.si[j]=100+j;
  profile_queries=0;
  for(unsigned j=0;j<UNIFORMS;j++){
   w.count=0;unif_at(&c,j,c.si[0]);CHECK(words[4]==c.si[expected[j]]);
   CHECK(w.count==(uniforms[j].count>1?10u:9u));cases++;
  }
  CHECK(!profile_queries&&!allocations);
 }
 printf("Uniform routing: %u access/member/generation cases; one %zu-byte stack map, %u profile queries per compile, zero lookup scans/heap calls PASS\n",cases,sizeof(UniformMap),UNIFORMS);
}
