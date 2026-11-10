/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"layout name lend line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static size_t bytes,calls;
static void *test_allocate(size_t n){bytes+=n;calls++;return malloc(n);}
#define malloc test_allocate
/* PRODUCTION */
#undef malloc
static uint64_t name_hash(const char*s){uint64_t h=0;for(;*s;s++)h=h*131+(unsigned char)*s;return h;}
int main(void){
 unsigned cases=0;
 for(unsigned lighting=0;lighting<(1u<<L_COUNT);lighting++)for(unsigned flags=0;flags<16;flags++){
  bytes=calls=0;
  GlslcLayout a=layout(false,flags&1,0,flags&2,flags&4,flags&8,lighting);
  GlslcLayout b=layout(false,flags&1,0,flags&2,flags&4,flags&8,lighting);
  CHECK(calls==2 && bytes==(a.count+b.count)*sizeof(GlslcUniform));
  CHECK(a.count==b.count && a.size==b.size);
  uint64_t hashes[32];CHECK(a.count<32);
  for(size_t i=0;i<a.count;i++){
   CHECK(a.members[i].name==b.members[i].name);
   CHECK((uintptr_t)a.members[i].name<(uintptr_t)a.members || (uintptr_t)a.members[i].name>=(uintptr_t)(a.members+a.count));
   hashes[i]=name_hash(a.members[i].name);CHECK(hashes[i]);
  }
  free(a.members);
  for(size_t i=0;i<b.count;i++)CHECK(name_hash(b.members[i].name)==hashes[i]);
  free(b.members);cases++;
 }
 for(unsigned count=1;count<=16;count++){
  bytes=calls=0;GlslcLayout a=layout(true,false,count,false,false,false,0);
  CHECK(calls==1 && bytes==sizeof(GlslcUniform));CHECK(!strcmp(a.members[0].name,"inlineValue"));
  CHECK(a.members[0].dim_a==count && a.size==count*16);free(a.members);cases++;
 }
 printf("Layout name lends: %u layouts; record-only allocation bytes, shared static names and independent retirement PASS\n",cases);
}
