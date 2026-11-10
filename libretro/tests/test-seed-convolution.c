/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hw/xbox/nv2a/pgraph/glsl/psh.h"
int main(void){
 static const unsigned values[]={0,1,2,3,4,5,6,7,0xffffffffu};unsigned cases=0;
 for(unsigned generation=0;generation<16;generation++)for(unsigned pattern=0;pattern<6561;pattern++){
  struct {uint64_t before;PshState state;uint64_t after;} owner;
  memset(&owner,0x91+generation,sizeof(owner));owner.before=0x12345678abcdef01ull;owner.after=0xfedcba8765432100ull;
  unsigned code=pattern;
  for(unsigned i=0;i<4;i++){owner.state.conv_tex[i]=(enum ConvolutionFilter)values[code%9];code/=9;}
  PshState expected=owner.state;
  for(unsigned i=0;i<4;i++){
   unsigned input=expected.conv_tex[i];
   expected.conv_tex[i]=(enum ConvolutionFilter)(input==1?5:input==2?6:input);
  }
  pgraph_glsl_psh_migrate_seed(&owner.state);
  assert(!memcmp(&owner.state,&expected,sizeof(expected)));
  pgraph_glsl_psh_migrate_seed(&owner.state);
  assert(!memcmp(&owner.state,&expected,sizeof(expected)));
  assert(owner.before==0x12345678abcdef01ull&&owner.after==0xfedcba8765432100ull);cases++;
 }
 printf("Seed convolution: %u tag/padding/canary/idempotence cases; only legacy1/2 become current5/6, all other bytes unchanged PASS\n",cases);
}
