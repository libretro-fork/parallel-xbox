/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <encodings/deflate.h>
uint64_t cpu_features_get(void){return 0;}
#include "qemu/file-map.h"
#define SEED_MAGIC 0x44455358u
#define SEED_VERSION 1u
#define SEED_MAX_ITEMS 65536u
#define SEED_DICT_MAGIC 0x44445358u
#define SEED_DICT_VERSION 1u
#define sizeof_field(t,f) sizeof(((t*)0)->f)
static unsigned maps,unmaps,inflations,duplicates,allocations,live;
static bool map_available,path_available=true,active,fail_inflate;
static const char *game="test";
static uint8_t local_image[1024],dictionary[2048];
static size_t local_length,shader_seeds_size;
static const uint8_t *shader_seeds_data=dictionary;
typedef union {max_align_t align;struct {size_t n;unsigned magic;} value;} Allocation;
static void *g_malloc(size_t n){Allocation *p=malloc(sizeof(*p)+n);assert(p);p->value.n=n;p->value.magic=0x1234;allocations++;live++;return p+1;}
static void g_free(void *p){if(p){Allocation *a=(Allocation*)p-1;assert(a->value.magic==0x1234&&live);memset(p,0xa5,a->value.n);a->value.magic=0;live--;free(a);}}
static void *g_memdup2(const void *p,size_t n){void *out=g_malloc(n);memcpy(out,p,n);duplicates++;return out;}
static char *seed_path(void){if(!path_available)return NULL;char *p=g_malloc(5);memcpy(p,"test",5);return p;}
static const char *pgraph_seed_game_tag(void){return game;}
bool xemu_file_map(XemuFileMap *m,const char *path){assert(path&&!active);if(!map_available)return false;m->owned=g_malloc(local_length?local_length:1);memcpy(m->owned,local_image,local_length);m->data=m->owned;m->len=local_length;maps++;active=true;return true;}
void xemu_file_unmap(XemuFileMap *m){assert(active);g_free(m->owned);memset(m,0,sizeof(*m));active=false;unmaps++;}
static void *counted_inflate_new(int bits){inflations++;return fail_inflate?NULL:rinflate_new(bits);}
#define rinflate_new counted_inflate_new
/* TYPES */
/* FIXTURES */
/* PRODUCTION */
static void counters(void){assert(!active);maps=unmaps=inflations=duplicates=allocations=0;}
static void put32(uint8_t *p,uint32_t v){memcpy(p,&v,4);}
static void dictionary_case(unsigned mode)
{
 SeedDictHeader h={SEED_DICT_MAGIC,SEED_DICT_VERSION,1};SeedDictEntry e={.game="test",.raw_len=sizeof(embedded_raw),.z_len=sizeof(embedded_z)};
 memcpy(dictionary,&h,sizeof(h));memcpy(dictionary+sizeof(h),&e,sizeof(e));memcpy(dictionary+sizeof(h)+sizeof(e),embedded_z,sizeof(embedded_z));shader_seeds_size=sizeof(h)+sizeof(e)+sizeof(embedded_z);game="test";fail_inflate=false;
 switch(mode){case 1:put32(dictionary,0);break;case 2:put32(dictionary+4,99);break;case 3:shader_seeds_size=3;break;case 4:put32(dictionary+8,UINT32_MAX);break;case 5:put32(dictionary+sizeof(h)+offsetof(SeedDictEntry,z_len),2048);break;case 6:put32(dictionary+sizeof(h)+offsetof(SeedDictEntry,raw_len),0);break;case 7:put32(dictionary+sizeof(h)+offsetof(SeedDictEntry,raw_len),(64u<<20)+1);break;case 8:put32(dictionary+sizeof(h)+offsetof(SeedDictEntry,raw_len),sizeof(embedded_raw)+1);break;case 9:dictionary[shader_seeds_size-1]^=1;break;case 10:game="other";break;case 11:game="abcdefghijklmnopqrstuvwxyz0123456789";break;case 12:game=NULL;break;case 13:fail_inflate=true;break;case 14:shader_seeds_size--;break;}
}
static void local_case(unsigned mode)
{
 memcpy(local_image,local_raw,sizeof(local_raw));local_length=sizeof(local_raw);
 switch(mode){case 1:put32(local_image,0);break;case 2:local_length=4;break;case 3:local_length=sizeof(SeedHeader)+sizeof(SeedSection)-1;break;case 4:local_length--;break;case 5:put32(local_image+sizeof(SeedHeader)+offsetof(SeedSection,itemsize),99);break;case 6:put32(local_image+sizeof(SeedHeader)+offsetof(SeedSection,count),SEED_MAX_ITEMS+1);break;case 7:put32(local_image+sizeof(SeedHeader)+offsetof(SeedSection,count),0);break;}
}
static void real_dictionary(const char *path)
{
 FILE *file=fopen(path,"rb");assert(file);assert(!fseek(file,0,SEEK_END));long size=ftell(file);assert(size>0);rewind(file);uint8_t *data=malloc(size);assert(data&&fread(data,1,size,file)==(size_t)size);fclose(file);
 shader_seeds_data=data;shader_seeds_size=size;SeedDictHeader header;memcpy(&header,data,sizeof(header));assert(header.magic==SEED_DICT_MAGIC&&header.ngames<10000);unsigned sections=0;
 for(unsigned g=0;g<header.ngames;g++) {
  SeedDictEntry entry;memcpy(&entry,data+sizeof(header)+g*sizeof(entry),sizeof(entry));char tag[33];memcpy(tag,entry.game,32);tag[32]=0;game=tag;map_available=path_available=false;fail_inflate=false;
  size_t length;uint8_t *raw=seed_dict_open(&length);assert(raw&&length>=sizeof(SeedHeader));SeedHeader h;memcpy(&h,raw,sizeof(h));assert(h.nsections<100);size_t off=sizeof(h);
  for(unsigned i=0;i<h.nsections;i++) {
   assert(off+sizeof(SeedSection)<=length);SeedSection sec;memcpy(&sec,raw+off,sizeof(sec));off+=sizeof(sec);size_t bytes=(size_t)sec.itemsize*sec.count;assert(bytes<=length-off);
   PGRAPHSeedRead read={.section=sec.id,.itemsize=sec.itemsize};pgraph_seed_read_combined(&read,1);assert(read.count==sec.count&&read.dict_count==sec.count);if(bytes)assert(!memcmp(read.items,raw+off,bytes));g_free(read.items);off+=bytes;sections++;
  }
  g_free(raw);assert(!live);
 }
 free(data);shader_seeds_data=dictionary;printf("Bundled dictionary: %u games / %u sections, exact real decompressed payloads PASS\n",header.ngames,sections);
}
int main(int argc,char **argv)
{
 unsigned cases=0;
 for(unsigned lm=0;lm<8;lm++)for(unsigned dm=0;dm<15;dm++)for(unsigned availability=0;availability<4;availability++){
  local_case(lm);dictionary_case(dm);map_available=availability&1;path_available=availability&2;
  PGRAPHSeedRead reads[]={{.section=1,.itemsize=4},{.section=2,.itemsize=8},{.section=99,.itemsize=4},{.section=1,.itemsize=8}};
  void *expected[4];unsigned totals[4],dict_counts[4];
  for(unsigned i=0;i<4;i++){
   unsigned nl=0,nd=0;void *l=pgraph_seed_read(reads[i].section,reads[i].itemsize,&nl);void *d=pgraph_seed_dict_read(reads[i].section,reads[i].itemsize,&nd);
   totals[i]=nl+nd;dict_counts[i]=nd;expected[i]=totals[i]?g_malloc(totals[i]*reads[i].itemsize):NULL;
   if(nl)memcpy(expected[i],l,nl*reads[i].itemsize);if(nd)memcpy((uint8_t*)expected[i]+nl*reads[i].itemsize,d,nd*reads[i].itemsize);g_free(l);g_free(d);
  }
  unsigned retained=live;counters();pgraph_seed_read_combined(reads,4);assert(maps<=1&&maps==unmaps&&inflations<=1&&!duplicates&&!active);
  for(unsigned i=0;i<4;i++){assert(reads[i].count==totals[i]&&reads[i].dict_count==dict_counts[i]);assert(!!reads[i].items==!!totals[i]);if(totals[i])assert(!memcmp(reads[i].items,expected[i],totals[i]*reads[i].itemsize));g_free(reads[i].items);}
  assert(live==retained);
  /* Prefer states; absent/stale/malformed states fall back to module keys.
   * Reused outputs are deliberately poisoned before each call. */
  PGRAPHSeedRead first[]={{.section=2,.itemsize=8},{.section=1,.itemsize=4},{.section=99,.itemsize=4}};
  for(unsigned i=0;i<3;i++){first[i].items=(void *)(uintptr_t)1;first[i].count=first[i].dict_count=UINT_MAX;}
  counters();pgraph_seed_read_first(first,3);
  unsigned chosen=totals[1]?0:totals[0]?1:3;
  assert(maps<=1&&maps==unmaps&&inflations<=1&&!duplicates&&!active);
  for(unsigned i=0;i<3;i++) {
   unsigned ref=i==0?1:0;
   if(i==chosen){assert(first[i].count==totals[ref]&&first[i].dict_count==dict_counts[ref]);assert(!memcmp(first[i].items,expected[ref],totals[ref]*first[i].itemsize));g_free(first[i].items);}
   else assert(!first[i].items&&!first[i].count&&!first[i].dict_count);
  }
  assert(live==retained);for(unsigned i=0;i<4;i++)g_free(expected[i]);assert(!live);cases++;
 }
 /* Independently known local-first ordering, and one map/inflation for both. */
 local_case(0);dictionary_case(0);map_available=path_available=true;PGRAPHSeedRead reads[]={{.section=1,.itemsize=4},{.section=2,.itemsize=8}};counters();pgraph_seed_read_combined(reads,2);assert(maps==1&&unmaps==1&&inflations==1&&!duplicates);uint32_t words[]={11,12,21,22,23};uint64_t wide[]={111,222,333};assert(reads[0].count==5&&reads[0].dict_count==3&&!memcmp(reads[0].items,words,sizeof(words)));assert(reads[1].count==3&&reads[1].dict_count==2&&!memcmp(reads[1].items,wide,sizeof(wide)));g_free(reads[0].items);g_free(reads[1].items);assert(!live);
 /* With both sources valid, first-only materializes exactly one fewer array. */
 local_case(0);dictionary_case(0);map_available=path_available=true;
 PGRAPHSeedRead preferred[]={{.section=2,.itemsize=8},{.section=1,.itemsize=4}};
 counters();pgraph_seed_read_combined(preferred,2);unsigned all_allocations=allocations;
 g_free(preferred[0].items);g_free(preferred[1].items);counters();
 pgraph_seed_read_first(preferred,2);assert(allocations+1==all_allocations);
 assert(preferred[0].count==3&&!preferred[1].items&&!preferred[1].count);
 g_free(preferred[0].items);assert(!live);
 counters();pgraph_seed_read_first(NULL,0);assert(!maps&&!inflations&&!allocations);
 counters();pgraph_seed_read_combined(NULL,0);assert(!maps&&!inflations&&!allocations);if(argc>1)real_dictionary(argv[1]);printf("Seed batch: %u combined + first-only equivalence cases plus independent order/lifetime/allocation oracle; one mapping/inflation, no duplicate arrays, malformed/missing/stale/failure handling PASS\n",cases);return 0;
}
