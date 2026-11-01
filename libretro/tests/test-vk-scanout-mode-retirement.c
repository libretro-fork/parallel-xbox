/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define MiB (1024ull*1024)
typedef struct {struct{unsigned width,height;}extent;} ImageInfo;
typedef struct {unsigned image,image_view,allocation,image_scratch,allocation_scratch;ImageInfo image_ci,scratch_ci;struct{unsigned bytes_per_pixel;}fmt;}SurfaceBinding;
struct PGRAPHVkScanoutMode{SurfaceBinding out;uint64_t last_used,aux_after,display_after;};
typedef struct{struct PGRAPHVkScanoutMode scanout_modes[8];uint64_t aux_settled;struct{uint64_t settled;}display;SurfaceBinding *color_binding,*zeta_binding;unsigned device,allocator;}PGRAPHVkState;
static unsigned pooled,destroyed,views;static bool accept_pool;
static bool pgraph_vk_image_pool_put_with_view(PGRAPHVkState*r,unsigned image,unsigned alloc,const ImageInfo*ci,unsigned view){assert(r->device==7&&image&&alloc==image&&ci->extent.width&&view==1000+image);if(accept_pool)pooled++;return accept_pool;}
static bool pgraph_vk_image_pool_put(PGRAPHVkState*r,unsigned image,unsigned alloc,const ImageInfo*ci){assert(r->device==7&&image>=101&&alloc==image&&ci->extent.width);if(accept_pool)pooled++;return accept_pool;}
static void vkDestroyImageView(unsigned device,unsigned view,const void*p){assert(device==7&&view>=1001&&!p);views++;}
static void xemu_vk_destroy_image(unsigned allocator,unsigned image,unsigned alloc){assert(allocator==9&&image&&alloc==image);destroyed++;}
static unsigned aux_polls,display_polls;static bool gpu_aux,gpu_display;
static void pgraph_vk_poll_aux_retired(PGRAPHVkState*r){aux_polls++;if(gpu_aux)r->aux_settled=100;}
static void pgraph_vk_poll_display_retired(PGRAPHVkState*r){display_polls++;if(gpu_display)r->display.settled=100;}
/* PRODUCTION */
static uint32_t seed=0x493ab51;
static uint32_t random_word(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
int main(void){
 unsigned schedules=0;
 for(unsigned trial=0;trial<30000;trial++){
  PGRAPHVkState r={.device=7,.allocator=9,.aux_settled=10,.display={.settled=20}};
  uint64_t bytes[8]={0};unsigned occupied=0,eligible=0;
  for(unsigned i=0;i<8;i++)if(random_word()%4){
   struct PGRAPHVkScanoutMode*m=&r.scanout_modes[i];SurfaceBinding*o=&m->out;
   o->image=o->allocation=i+1;o->image_view=1001+i;o->image_ci.extent.width=1+random_word()%16;o->image_ci.extent.height=256*1024;o->fmt.bytes_per_pixel=4;
   if(random_word()%3==0){o->image_scratch=o->allocation_scratch=101+i;o->scratch_ci=o->image_ci;}
   bytes[i]=(uint64_t)o->image_ci.extent.width*MiB*(o->image_scratch?2:1);
   m->last_used=random_word()%100;m->aux_after=9+random_word()%3;m->display_after=19+random_word()%3;occupied|=1u<<i;
   if(m->aux_after<=r.aux_settled&&m->display_after<=r.display.settled)eligible|=1u<<i;
   assert(scanout_mode_bytes(m)==bytes[i]);
  }
  uint64_t required=(1+random_word()%48)*MiB,limit=required<32*MiB?32*MiB-required:0;
  bool possible=false;
  for(unsigned subset=0;subset<256;subset++)if(!(subset&~eligible)){
   uint64_t kept=0;unsigned remaining=occupied&~subset;
   for(unsigned i=0;i<8;i++)if(remaining&(1u<<i))kept+=bytes[i];
   if(remaining!=255&&kept<=limit){possible=true;break;}
  }
  PGRAPHVkState before=r;unsigned next=99,evict=99;bool ok=plan_scanout_mode(&r,required,&next,&evict);
  assert(!memcmp(&r,&before,sizeof(r))&&ok==possible);
  if(ok){
   assert(next<8&&!(evict&~eligible)&&(!(occupied&(1u<<next))||(evict&(1u<<next))));
   uint64_t kept=0;for(unsigned i=0;i<8;i++)if((occupied&~evict)&(1u<<i))kept+=bytes[i];assert(kept<=limit);
   /* Every eviction must be the oldest ready remaining mode. */
   unsigned left=evict;while(left){unsigned oldest=8;for(unsigned i=0;i<8;i++)if((eligible&(1u<<i))&&(oldest==8||r.scanout_modes[i].last_used<r.scanout_modes[oldest].last_used))oldest=i;assert(oldest<8&&(left&(1u<<oldest)));left&=~(1u<<oldest);eligible&=~(1u<<oldest);}
   accept_pool=trial&1;pooled=destroyed=views=0;unsigned images=0,victims=0;
   for(unsigned i=0;i<8;i++)if(evict&(1u<<i)){images+=1+(r.scanout_modes[i].out.image_scratch!=0);victims++;release_scanout_mode(&r,&r.scanout_modes[i]);struct PGRAPHVkScanoutMode zero={0};assert(!memcmp(&zero,&r.scanout_modes[i],sizeof(zero)));}
   assert(pooled+destroyed==images&&views==(accept_pool?0:victims));
  }else assert(next==99&&evict==99);
  schedules++;
 }
 /* Exercise the actual pressure/retry/release path with independent GPU domains. */
 PGRAPHVkState r={.device=7,.allocator=9,.aux_settled=10,.display={.settled=20}};
 for(unsigned i=0;i<8;i++){
  struct PGRAPHVkScanoutMode*m=&r.scanout_modes[i];m->out.image=m->out.allocation=i+1;m->out.image_view=1001+i;
  m->out.image_ci.extent.width=1024;m->out.image_ci.extent.height=256;m->out.fmt.bytes_per_pixel=4;
  m->last_used=i;m->aux_after=11;m->display_after=21;
 }
 unsigned next=99;pooled=destroyed=views=aux_polls=display_polls=0;accept_pool=false;
 for(unsigned n=0;n<16;n++)assert(!reserve_scanout_mode(&r,MiB,&next));
 assert(!destroyed&&!views&&next==99&&aux_polls==16&&display_polls==16);
 gpu_aux=true;assert(!reserve_scanout_mode(&r,MiB,&next));assert(!destroyed&&r.aux_settled==100&&r.display.settled==20);
 gpu_display=true;assert(reserve_scanout_mode(&r,MiB,&next));assert(next==0&&destroyed==1&&views==1&&!r.scanout_modes[0].out.image);
 unsigned before_aux=aux_polls,before_display=display_polls;
 assert(reserve_scanout_mode(&r,MiB,&next)&&aux_polls==before_aux&&display_polls==before_display&&destroyed==1);
 puts("PASS: 30000 mode pressure/lifetime schedules against all 256 eviction subsets; independent queue retry and bounded resource release");
 assert(schedules==30000);
}
