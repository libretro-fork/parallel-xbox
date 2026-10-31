/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include "qemu/queue.h"
#include "hw/xbox/nv2a/pgraph/vram-ranges.h"
#ifdef _WIN32
#include <windows.h>
#include <signal.h>
#endif
/* QUIET ERRORS */
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define NV2A_PROF_QUEUE_SUBMIT_1 0
#define DIRTY_MEMORY_NV2A_TEX 0
#define DIRTY_MEMORY_VGA 1
typedef uint64_t hwaddr;
/* RANGE TYPE */
typedef struct SurfaceBinding {QTAILQ_ENTRY(SurfaceBinding) entry;hwaddr vram_addr,size;unsigned width,height,pitch,draw_time,id;bool draw_dirty,writeback_owed,download_pending,armed,direct;unsigned char value;} SurfaceBinding;
typedef struct {QTAILQ_HEAD(,SurfaceBinding) surfaces,invalid_surfaces;/* BATCH TYPE */} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;} PGRAPHState;
typedef struct {PGRAPHState pgraph;void *vram;} NV2AState;
static PGRAPHVkState state;
static SurfaceBinding surfaces[192];
static unsigned char ram[4096];
static SurfaceBinding *commands[192];
static bool pending[192];
static unsigned command_count,begins,waits,barriers,retirements,copies;
static unsigned copy_order[192];
static uint64_t learned[192];
static void readback_batch_flush(NV2AState *d);
static void readback_batch_begin(PGRAPHVkState *r);
static void readback_batch_end(NV2AState *d);
static VkCommandBuffer readback_batch_cmd(PGRAPHState *pg);
static void readback_batch_add(NV2AState *d,SurfaceBinding *s);
static void parked_owed_settled(NV2AState *d,SurfaceBinding *s);
static void parked_materialize(NV2AState *d,SurfaceBinding *s);
static void parked_materialize_range(NV2AState *d,hwaddr start,hwaddr size);
void pgraph_vk_surface_download_if_dirty(NV2AState *d,SurfaceBinding *s);
static void memory_region_set_client_dirty(void *vram,hwaddr addr,hwaddr size,unsigned client){(void)vram;(void)client;assert(addr+size<=sizeof(ram));}
static void pgraph_vk_invalidate_textures_from_gpu(NV2AState *d,hwaddr addr,hwaddr size){(void)d;assert(addr+size<=sizeof(ram));}
static void pgraph_writeback_clear_discard(PGRAPHState *pg,hwaddr addr,hwaddr size){(void)pg;assert(addr+size<=sizeof(ram));}
static void pgraph_writeback_learn_live(PGRAPHState *pg,hwaddr addr,hwaddr size){(void)pg;for(unsigned i=0;i<192;i++)if(surfaces[i].vram_addr==addr&&surfaces[i].size==size)learned[i]++;}
static void unregister_cpu_access_callback(NV2AState *d,SurfaceBinding *s){(void)d;assert(!pending[s->id]);if(s->armed)retirements++;s->armed=false;}
static void nv2a_profile_inc_counter(unsigned counter){assert(counter==NV2A_PROF_QUEUE_SUBMIT_1);}
static void readback_to_host_barrier(VkCommandBuffer cmd){assert(cmd&&command_count);barriers++;}
static VkCommandBuffer pgraph_vk_begin_single_time_commands(PGRAPHState *pg){(void)pg;assert(!command_count);begins++;return (VkCommandBuffer)(uintptr_t)begins;}
static void pgraph_vk_end_single_time_commands(PGRAPHState *pg,VkCommandBuffer cmd){(void)pg;assert(cmd&&command_count);waits++;for(unsigned i=0;i<command_count;i++){SurfaceBinding *s=commands[i];assert(!s->writeback_owed||s->armed);memset(ram+s->vram_addr,s->value,s->size);pending[s->id]=false;}command_count=0;}
static void queue_copy(SurfaceBinding *s){assert(command_count<ARRAY_SIZE(commands)&&copies<ARRAY_SIZE(copy_order));assert(!pending[s->id]);commands[command_count++]=s;pending[s->id]=true;copy_order[copies++]=s->id;}
static void download_settled(NV2AState *d,SurfaceBinding *s);
static void download_surface(NV2AState *d,SurfaceBinding *s,bool force){
 assert(force);if(!s->width||!s->height)return;
 if(s->direct&&state.readback_batch.open){readback_batch_cmd(&d->pgraph);queue_copy(s);readback_batch_add(d,s);}
 else{readback_batch_flush(d);VkCommandBuffer cmd=pgraph_vk_begin_single_time_commands(&d->pgraph);queue_copy(s);pgraph_vk_end_single_time_commands(&d->pgraph,cmd);download_settled(d,s);}
}
/* PRODUCTION */
/* Previous range behavior: live batch, then each parked image settled alone. */
static void reference_range(NV2AState *d,hwaddr start,hwaddr size){
 readback_batch_begin(&state);SurfaceBinding *s,*next;
 QTAILQ_FOREACH(s,&state.surfaces,entry)if(check_surface_overlaps_range(s,start,size))pgraph_vk_surface_download_if_dirty(d,s);
 readback_batch_end(d);
 QTAILQ_FOREACH_SAFE(s,&state.invalid_surfaces,entry,next)if(s->writeback_owed&&check_surface_overlaps_range(s,start,size)){parked_materialize(d,s);pgraph_writeback_learn_live(&d->pgraph,s->vram_addr,s->size);}
}
static uint32_t random_state=0x3f4a6912;
static uint32_t random_word(void){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static void setup(const SurfaceBinding *description,unsigned live,unsigned count){
 memset(&state,0,sizeof(state));QTAILQ_INIT(&state.surfaces);QTAILQ_INIT(&state.invalid_surfaces);memcpy(surfaces,description,count*sizeof(*description));memset(pending,0,sizeof(pending));memset(ram,0x37,sizeof(ram));memset(learned,0,sizeof(learned));command_count=begins=waits=barriers=retirements=copies=0;
 for(unsigned i=0;i<count;i++){if(i<live)QTAILQ_INSERT_TAIL(&state.surfaces,&surfaces[i],entry);else QTAILQ_INSERT_TAIL(&state.invalid_surfaces,&surfaces[i],entry);}
}
int main(void){
 quiet_test_errors();SurfaceBinding description[192],expected[192];unsigned char expected_ram[4096];unsigned expected_order[192];uint64_t expected_learned[192];unsigned total_before=0,total_after=0;
 for(unsigned trial=0;trial<2000;trial++){
  unsigned live=trial==0?1:random_word()%17,count=trial==0?25:live+random_word()%160;if(count>192)count=192;
  memset(description,0,sizeof(description));for(unsigned i=0;i<count;i++){
   SurfaceBinding *s=&description[i];s->id=i;s->size=trial==0?16:16+(random_word()%8)*16;s->vram_addr=trial==0?i*32:random_word()%(sizeof(ram)-s->size);s->width=trial==0||random_word()%23?4:0;s->height=1;s->pitch=s->size;s->draw_time=trial==0?i+1:random_word()%30;s->draw_dirty=trial==0||random_word()%9;s->writeback_owed=i>=live&&(trial==0||random_word()%7);s->armed=s->writeback_owed;if(s->writeback_owed){s->draw_dirty=true;s->width=4;}s->direct=trial==0||random_word()%7;s->value=(unsigned char)(i+1);
  }
  hwaddr start=trial==0?0:random_word()%4096,size=trial==0?4096:random_word()%4097;
  setup(description,live,count);NV2AState d={.pgraph={.vk_renderer_state=&state}};reference_range(&d,start,size);unsigned old_waits=waits,old_retirements=retirements,old_copies=copies;memcpy(expected_ram,ram,sizeof(ram));memcpy(expected,surfaces,count*sizeof(*surfaces));memcpy(expected_order,copy_order,copies*sizeof(*copy_order));total_before+=waits;memcpy(expected_learned,learned,sizeof(learned));
  setup(description,live,count);pgraph_vk_download_surfaces_in_range_if_dirty(&d.pgraph,start,size);total_after+=waits;
  assert(!state.readback_batch.open&&!state.readback_batch.recording&&!state.readback_batch.n&&!command_count);assert(waits<=old_waits&&retirements==old_retirements&&copies==old_copies);assert(!memcmp(expected_ram,ram,sizeof(ram))&&!memcmp(expected_order,copy_order,copies*sizeof(*copy_order))&&!memcmp(expected_learned,learned,sizeof(learned)));
  for(unsigned i=0;i<count;i++){assert(!pending[i]);assert(surfaces[i].draw_dirty==expected[i].draw_dirty&&surfaces[i].writeback_owed==expected[i].writeback_owed&&surfaces[i].armed==expected[i].armed&&surfaces[i].download_pending==expected[i].download_pending);}
  if(trial==0){assert(old_waits==25&&waits==1);puts("Texture producer batch: 25 producer waits -> 1 in the all-direct fixture");}
  const MemorySyncRequirement ranges[2]={{start,size},{random_word()%4096,random_word()%4097}};
  setup(description,live,count);reference_range(&d,ranges[0].addr,ranges[0].size);reference_range(&d,ranges[1].addr,ranges[1].size);old_waits=waits;old_retirements=retirements;old_copies=copies;memcpy(expected_ram,ram,sizeof(ram));memcpy(expected,surfaces,count*sizeof(*surfaces));memcpy(expected_order,copy_order,copies*sizeof(*copy_order));memcpy(expected_learned,learned,sizeof(learned));
  setup(description,live,count);pgraph_vk_download_surface_ranges_if_dirty(&d.pgraph,ranges,2);assert(waits<=old_waits&&retirements==old_retirements&&copies==old_copies&&!command_count&&!state.readback_batch.open&&!state.readback_batch.recording&&!state.readback_batch.n);assert(!memcmp(expected_ram,ram,sizeof(ram))&&!memcmp(expected_order,copy_order,copies*sizeof(*copy_order))&&!memcmp(expected_learned,learned,sizeof(learned)));
  for(unsigned i=0;i<count;i++)assert(!pending[i]&&surfaces[i].draw_dirty==expected[i].draw_dirty&&surfaces[i].writeback_owed==expected[i].writeback_owed&&surfaces[i].armed==expected[i].armed);
 }
 /* A capacity split must retire each callback after its own completed fence. */
 memset(description,0,sizeof(description));for(unsigned i=0;i<130;i++)description[i]=(SurfaceBinding){.vram_addr=i*24,.size=16,.width=4,.height=1,.pitch=16,.draw_time=i+1,.id=i,.draw_dirty=true,.writeback_owed=i!=0,.armed=i!=0,.direct=true,.value=(unsigned char)(i+1)};
 setup(description,1,130);NV2AState d={.pgraph={.vk_renderer_state=&state}};reference_range(&d,0,4096);assert(waits==130);memcpy(expected_ram,ram,sizeof(ram));setup(description,1,130);pgraph_vk_download_surfaces_in_range_if_dirty(&d.pgraph,0,4096);assert(waits==3&&retirements==129&&!memcmp(expected_ram,ram,sizeof(ram)));
 /* Two disjoint indexed inputs retain palette-before-index writes but share
  * the direct batch instead of introducing a host wait between the inputs. */
 description[0]=(SurfaceBinding){.vram_addr=16,.size=16,.width=4,.height=1,.pitch=16,.id=0,.draw_dirty=true,.direct=true,.value=11};description[1]=(SurfaceBinding){.vram_addr=128,.size=16,.width=4,.height=1,.pitch=16,.id=1,.draw_dirty=true,.writeback_owed=true,.armed=true,.direct=true,.value=22};
 setup(description,1,2);reference_range(&d,16,16);reference_range(&d,128,16);assert(waits==2);memcpy(expected_ram,ram,sizeof(ram));
 const MemorySyncRequirement indexed_ranges[2]={{16,16},{128,16}};setup(description,1,2);pgraph_vk_download_surface_ranges_if_dirty(&d.pgraph,indexed_ranges,2);assert(waits==1&&!memcmp(expected_ram,ram,sizeof(ram))&&copy_order[0]==0&&copy_order[1]==1);
 puts("Indexed palette/index producers: two modeled waits -> one, same copies and bytes");
 for(unsigned edge=0;edge<4;edge++){
  description[0]=(SurfaceBinding){.vram_addr=16,.size=16,.width=4,.height=1,.pitch=16,.id=0,.draw_dirty=true,.direct=true,.value=11};
  description[1]=(SurfaceBinding){.vram_addr=128,.size=16,.width=edge==0?0:4,.height=edge==1?0:1,.pitch=16,.id=1,.writeback_owed=true,.armed=true,.draw_dirty=edge<2,.direct=true,.value=22};
  setup(description,1,2);reference_range(&d,0,4096);memcpy(expected_ram,ram,sizeof(ram));unsigned expected_waits=waits;
  setup(description,1,2);pgraph_vk_download_surfaces_in_range_if_dirty(&d.pgraph,0,4096);assert(waits==expected_waits&&waits==1&&retirements==1&&!surfaces[1].writeback_owed&&!surfaces[1].armed&&!state.readback_batch.open&&!command_count&&!memcmp(expected_ram,ram,sizeof(ram)));
 }
 puts("Texture producer batch: 130 direct producers split into 3 waits; four explicit no-copy retirement cases passed");
 printf("Texture producer batch: 2000 single-range and 2000 ordered-pair workloads passed, exact RAM and copy order, overlap/older dependencies, capacity splits, synchronous fallbacks and callback retirement; modeled waits %u -> %u\n",total_before,total_after);
 return 0;
}
