/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#define MiB (1024u*1024u)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MEMTXATTRS_UNSPECIFIED 0
typedef uint64_t hwaddr;
typedef struct {bool migratable;} RAMBlock;
typedef struct {size_t size;RAMBlock*ram_block;} MemoryRegion;
typedef struct {MemoryRegion*vram;MemoryRegion ramin;uint8_t*vram_ptr,*ramin_ptr,*pramin_top;bool pramin_unified,migration_parked;} NV2AState;
static unsigned unpark;
static void nv2a_unlock_fifo(NV2AState*p){assert(p->migration_parked==false);unpark++;}
static NV2AState d;static int address_space_memory;static unsigned reads,writes,dirties;static size_t dirty_bytes;
static void qemu_ram_unset_migratable(RAMBlock*b){b->migratable=false;}
static void qemu_ram_set_migratable(RAMBlock*b){b->migratable=true;}
static size_t memory_region_size(MemoryRegion*m){return m->size;}
static uint64_t ldq_le_p(const void*p){const uint8_t*b=p;uint64_t x=0;for(unsigned i=0;i<8;i++)x|=(uint64_t)b[i]<<(8*i);return x;}
static void stl_le_p(void*p,uint32_t x){uint8_t*b=p;for(unsigned i=0;i<4;i++)b[i]=x>>(8*i);}
static void memory_region_set_dirty(MemoryRegion*m,size_t a,size_t n){assert(m==&d.ramin && a+n<=m->size);dirties++;dirty_bytes+=n;}
static void address_space_read(int*as,hwaddr a,int attrs,void*p,size_t n){assert(as==&address_space_memory && !attrs && a+n<=d.vram->size);reads++;memcpy(p,d.vram_ptr+a,n);}
static void address_space_write(int*as,hwaddr a,int attrs,const void*p,size_t n){assert(as==&address_space_memory && !attrs && a+n<=d.vram->size);writes++;dirty_bytes+=n;memcpy(d.vram_ptr+a,p,n);}
/* PRODUCTION */
static uint32_t rng=137;
static uint32_t random_word(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static size_t oracle(size_t a){return d.vram->size-0x100000 + (0xffffu-(a/16))*16 + a%16;}
static void exercise(bool unified){
 nv2a_pramin_set_mode(&d,unified);unsigned cases=0;
 for(unsigned pass=0;pass<3;pass++){
  unsigned n=1u<<pass;
  for(size_t a=0;a+n<=0x100000;a++){
   uint32_t value=random_word();uint64_t expected=0;
   uint8_t old[4];for(unsigned j=0;j<n;j++)old[j]=d.vram_ptr[oracle(a+j)];
   reads=writes=dirties=0;dirty_bytes=0;
   nv2a_pramin_write(&d,a,value,n);
   for(unsigned j=0;j<n;j++){
    uint8_t actual=unified?d.vram_ptr[oracle(a+j)]:d.ramin_ptr[a+j];assert(actual==(uint8_t)(value>>(j*8)));
    if(!unified)assert(d.vram_ptr[oracle(a+j)]==old[j]);
    expected|=(uint64_t)actual<<(j*8);
   }
   assert(dirty_bytes==n);unsigned spans=(a/16==(a+n-1)/16)?1:2;
   assert(writes==(unified?spans:0) && dirties==(unified?0:1));
   assert(nv2a_pramin_read(&d,a,n)==expected);
   assert(reads==(unified?spans:0));cases++;
  }
 }
 /* Native physical writes are immediately visible via all four dwords. */
 for(size_t a=0;a<0x100000;a+=16){
  uint8_t*p=nv2a_ramin_span(&d,a,16);assert(p==(unified?d.vram_ptr+oracle(a):d.ramin_ptr+a));
  for(unsigned k=0;k<4;k++){uint32_t x=random_word();stl_le_p(p+4*k,x);assert(nv2a_pramin_read(&d,a+4*k,4)==x);}
 }
 printf("PRAMIN %s: %u width/address cases plus 262144 descriptor dwords PASS\n",unified?"unified":"legacy",cases);
}
int main(void){
 RAMBlock shadow={true};MemoryRegion region={.size=128*MiB};d=(NV2AState){.vram=&region,.ramin={.size=0x100000,.ram_block=&shadow},.vram_ptr=calloc(1,128*MiB),.ramin_ptr=calloc(1,0x100000)};assert(d.vram_ptr && d.ramin_ptr);
 region.size=64*MiB;
 nv2a_pramin_set_mode(&d,true);nv2a_pramin_save_begin(&d);assert(!shadow.migratable);nv2a_pramin_save_end(&d);assert(shadow.migratable);
 nv2a_pramin_set_mode(&d,false);nv2a_pramin_save_begin(&d);assert(shadow.migratable);
 d.migration_parked=true;shadow.migratable=false;nv2a_migration_release(&d);assert(unpark==1 && shadow.migratable && !d.migration_parked);nv2a_migration_release(&d);assert(unpark==1);
 exercise(true);exercise(false);
 /* Old versions force the original model without copying either RAM store. */
 uint8_t before[32];memcpy(before,d.vram_ptr+64*MiB-32,32);
 for(int v=1;v<=5;v++){nv2a_pramin_set_mode(&d,true);assert(nv2a_pramin_restore_mode(&d,v) && !d.pramin_unified && !d.pramin_top);assert(!memcmp(before,d.vram_ptr+64*MiB-32,32));}
 nv2a_pramin_set_mode(&d,true);assert(nv2a_pramin_restore_mode(&d,6) && d.pramin_top);
 nv2a_pramin_set_mode(&d,false);assert(nv2a_pramin_restore_mode(&d,6) && !d.pramin_top);
 region.size=128*MiB;exercise(false);d.pramin_unified=true;assert(!nv2a_pramin_restore_mode(&d,6) && !d.pramin_unified && !d.pramin_top);
 free(d.ramin_ptr);free(d.vram_ptr);puts("PRAMIN mode/state compatibility and 128 MiB legacy policy PASS");
}
