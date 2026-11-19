/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#include "hw/xbox/nv2a/pgraph/vk/glsl.h"
#define UNISEC__COUNT 1 /* Unused generation fields, not an upload ABI fixture. */
#define NV2A_VERTEXSHADER_ATTRIBUTES 16
#define PGRAPH_VK_SLOTS 3
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define QTAILQ_FOREACH(n,h,f) for((n)=*(h);(n);(n)=(n)->next)
#define VK_CHECK(x) assert((x)==VK_SUCCESS)

/* Guard every test allocation, including words/layouts and final module owners. */
static struct {void *p;size_t size;bool live;} heap[8192];
static unsigned heap_n,heap_live;
static void *allocate(size_t n){assert(n && heap_n<8192);void *p=calloc(1,n);assert(p);heap[heap_n++]=(typeof(heap[0])){p,n,true};heap_live++;return p;}
static bool alive(const void *p){for(unsigned i=heap_n;i;i--)if(heap[i-1].p==p&&heap[i-1].live)return true;return false;}
static void release_allocation(void *p){if(!p)return;for(unsigned i=heap_n;i;i--)if(heap[i-1].p==p&&heap[i-1].live){heap[i-1].live=false;heap_live--;memset(p,0xdd,heap[i-1].size);free(p);return;}assert(!"double free or foreign owner");}
#define g_malloc0(n) allocate(n)
#define g_free(p) release_allocation(p)
#define free(p) release_allocation(p)

typedef struct LruNode {uint64_t hash;struct LruNode *next;} LruNode;
/* TYPES */
typedef struct Lru {LruNode *bins[1];unsigned num_free,victims,evictions,lookups,initializers;} Lru;
typedef struct PGRAPHVkState {
 VkDevice device;VkDescriptorSetLayout descriptor_set_layout;
 bool use_push_constants_for_uniform_attrs,in_command_buffer;
 uint64_t next_shader_module_identity;
 uint32_t submit_count,settled_submit;
 struct {bool enabled,failed;VkPhysicalDeviceFeatures features;ShaderModuleInfo *gpu_owners;unsigned live_objects;} shader_objects;
 Lru shader_module_cache;
 bool slot_fence_pending[3];uint32_t slot_submit[3];VkFence slot_fence[3];VkCommandBuffer slot_main[3],slot_sync[3];
} PGRAPHVkState;
typedef struct PGRAPHState {PGRAPHVkState *vk_renderer_state;} PGRAPHState;
static unsigned error_mode,native_live,native_destroyed,module_destroyed,source_creations;
static uintptr_t next_handle=1;
static bool native_handles[8192],wait_guard,wait_returned;
static unsigned waits,ram_drops,trash_drains;
static VkFence expected_fence;
static PGRAPHVkState *api_renderer;
static ShaderSource *api_source;
static VkShaderStageFlagBits api_stage;
static bool cost_active;
static bool xemu_cost_model_active(void){return cost_active;}
static void pgraph_vk_finish(PGRAPHState *,FinishReason);

void pgraph_vk_ref_shader_module(ShaderModuleInfo *);
void pgraph_vk_unref_shader_module(PGRAPHVkState *,ShaderModuleInfo *);
void pgraph_vk_destroy_shader_module(PGRAPHVkState *,ShaderModuleInfo *);
ShaderModuleInfo *pgraph_vk_create_shader_object_from_spirv(PGRAPHVkState *,VkShaderStageFlagBits,ShaderSource *,bool);

VKAPI_ATTR VkResult VKAPI_CALL vkCreateShadersEXT(VkDevice d,uint32_t n,const VkShaderCreateInfoEXT *ci,const VkAllocationCallbacks *a,VkShaderEXT *out){
 (void)d;(void)a;assert(n==1 && ci->sType==VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT && !ci->flags);
 assert(ci->stage==api_stage && ci->codeType==VK_SHADER_CODE_TYPE_SPIRV_EXT && ci->pCode==api_source->words && alive(ci->pCode));
 assert(ci->codeSize==api_source->num_words*4 && !memcmp(ci->pCode,api_source->words,ci->codeSize));
 assert(!strcmp(ci->pName,"main") && ci->setLayoutCount==1 && *ci->pSetLayouts==api_renderer->descriptor_set_layout);
 VkShaderStageFlags next=api_stage==VK_SHADER_STAGE_VERTEX_BIT ? VK_SHADER_STAGE_FRAGMENT_BIT|(api_renderer->shader_objects.features.geometryShader?VK_SHADER_STAGE_GEOMETRY_BIT:0) : api_stage==VK_SHADER_STAGE_GEOMETRY_BIT?VK_SHADER_STAGE_FRAGMENT_BIT:0;
 assert(ci->nextStage==next);
 assert(ci->pushConstantRangeCount==(unsigned)api_renderer->use_push_constants_for_uniform_attrs);
 if(ci->pushConstantRangeCount)assert(ci->pPushConstantRanges->stageFlags==VK_SHADER_STAGE_VERTEX_BIT && !ci->pPushConstantRanges->offset && ci->pPushConstantRanges->size==256);
 *out=VK_NULL_HANDLE;
 if(error_mode==0 || error_mode==2){assert(next_handle<8192);native_handles[next_handle]=true;native_live++;*out=(VkShaderEXT)next_handle++;}
 return error_mode==1||error_mode==2?VK_ERROR_OUT_OF_HOST_MEMORY:VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyShaderEXT(VkDevice d,VkShaderEXT s,const VkAllocationCallbacks *a){(void)d;(void)a;uintptr_t h=(uintptr_t)s;assert(h<8192&&native_handles[h]);if(wait_guard)assert(wait_returned);native_handles[h]=false;native_live--;native_destroyed++;}
VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(VkDevice d,VkShaderModule m,const VkAllocationCallbacks *a){(void)d;(void)a;assert(m);module_destroyed++;}
VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice d,uint32_t n,const VkFence *f,VkBool32 all,uint64_t timeout){(void)d;(void)timeout;assert(n==1&&all&&*f==expected_fence);assert(native_live && !wait_returned);waits++;wait_returned=true;return VK_SUCCESS;}
static void lr_gpu_ram_drop(uintptr_t p){assert(p);ram_drops++;}
static void trash_drain_retired(PGRAPHVkState *r){(void)r;trash_drains++;}
static unsigned lru_hash_to_bin(Lru *l,uint64_t hash){(void)l;(void)hash;return 0;}
static uint64_t xxh3_64(const void *data,size_t size,uint64_t seed){(void)data;(void)size;(void)seed;return 17;}
static bool lru_try_evict_one(Lru *l){l->evictions++;if(!l->victims)return false;l->victims--;l->num_free++;return true;}
static LruNode *lru_lookup(Lru *,uint64_t,const void *);

/* PRODUCTION */

/* The source-selected shutdown reason must evaluate synchronously under the
 * real finish_is_async policy, including a previously submitted/open recording. */
static void pgraph_vk_finish(PGRAPHState *pg,FinishReason reason){
 PGRAPHVkState *r=pg->vk_renderer_state;assert(!finish_is_async(reason));
 if(r->in_command_buffer){r->in_command_buffer=false;r->submit_count++;r->slot_fence_pending[0]=true;r->slot_submit[0]=r->submit_count;}
 pgraph_vk_wait_all_slots(r);
}

static PGRAPHVkState fresh(void){PGRAPHVkState r={.descriptor_set_layout=(VkDescriptorSetLayout)(uintptr_t)19};r.shader_objects.enabled=true;return r;}
static ShaderSource *source_new(bool layouts){
 ShaderSource *s=allocate(sizeof(*s));retro_atomic_int_init(&s->refs,1);s->words=allocate(16);s->num_words=4;s->words[0]=0x07230203;source_creations++;
 if(layouts){s->layouts=allocate(sizeof(*s->layouts));retro_atomic_int_init(&s->layouts->refs,1);s->layouts->uniforms.count=1;s->layouts->uniforms.size=16;s->layouts->uniforms.members=allocate(sizeof(ShaderUniform));s->layouts->uniforms.members[0]=(ShaderUniform){.name="value",.dim_v=4,.dim_a=1};}
 return s;
}
static ShaderModuleInfo *make_owner(PGRAPHVkState *r,VkShaderStageFlagBits stage){
 ShaderSource *s=source_new(true);void *words=s->words,*src=s;api_renderer=r;api_source=s;api_stage=stage;error_mode=0;
 ShaderModuleInfo *m=pgraph_vk_create_shader_object_from_spirv(r,stage,s,false);assert(m&&!m->refcnt&&m->stage==stage&&m->shader_object&&!m->module);assert(!alive(words)&&!alive(src));pgraph_vk_ref_shader_module(m);return m;
}
static void clean(void){assert(!heap_live&&!native_live);heap_n=0;wait_guard=false;wait_returned=false;}
static void creation(void){
 unsigned cases=0;
 for(unsigned geometry=0;geometry<2;geometry++)for(unsigned push=0;push<2;push++)for(unsigned optional=0;optional<2;optional++)for(unsigned stage=0;stage<3;stage++){
  VkShaderStageFlagBits stages[3]={VK_SHADER_STAGE_VERTEX_BIT,VK_SHADER_STAGE_GEOMETRY_BIT,VK_SHADER_STAGE_FRAGMENT_BIT};if(stage==1&&!geometry)continue;
  for(error_mode=0;error_mode<4;error_mode++){
   PGRAPHVkState r=fresh();r.shader_objects.features.geometryShader=geometry;r.use_push_constants_for_uniform_attrs=push;
   ShaderSource *s=source_new(true);void *words=s->words,*src=s;ShaderLayouts *layouts=s->layouts;api_renderer=&r;api_source=s;api_stage=stages[stage];
   ShaderModuleInfo *m=pgraph_vk_create_shader_object_from_spirv(&r,stages[stage],s,optional);assert(!alive(words)&&!alive(src));
   if(!error_mode){assert(m&&alive(layouts)&&retro_atomic_load_relaxed_int(&layouts->refs)==1&&m->layouts==layouts&&m->uniforms.uniforms==layouts->uniforms.members);assert(m->uniforms.allocation&&m->uniforms.total_size==16&&r.shader_objects.live_objects==1);pgraph_vk_ref_shader_module(m);pgraph_vk_unref_shader_module(&r,m);assert(!alive(layouts)&&!r.shader_objects.live_objects);}
   else assert(!m&&!alive(layouts)&&r.shader_objects.failed==!optional&&!r.shader_objects.live_objects);
   cases++;clean();
  }
 }
 /* Two synchronous creators share one immutable source/layout owner; words
  * end only after the second creation consumes the last source reference. */
 PGRAPHVkState r=fresh();ShaderSource *s=source_new(true);retro_atomic_int_init(&s->refs,2);void *words=s->words;ShaderLayouts *layouts=s->layouts;api_renderer=&r;api_source=s;api_stage=VK_SHADER_STAGE_VERTEX_BIT;error_mode=0;
 ShaderModuleInfo *a=pgraph_vk_create_shader_object_from_spirv(&r,api_stage,s,false);assert(alive(words));api_stage=VK_SHADER_STAGE_FRAGMENT_BIT;ShaderModuleInfo *b=pgraph_vk_create_shader_object_from_spirv(&r,api_stage,s,false);assert(!alive(words)&&a!=b&&a->identity!=b->identity&&a->layouts==b->layouts&&retro_atomic_load_relaxed_int(&layouts->refs)==2);pgraph_vk_ref_shader_module(a);pgraph_vk_ref_shader_module(b);pgraph_vk_unref_shader_module(&r,a);assert(alive(layouts));pgraph_vk_unref_shader_module(&r,b);clean();
 /* Queued native-kind1 jobs keep their captured target after drawing has
  * permanently selected ordinary mode; feature enablement did not change. */
 r=fresh();r.shader_objects.enabled=false;a=make_owner(&r,VK_SHADER_STAGE_VERTEX_BIT);assert(a->shader_object);pgraph_vk_unref_shader_module(&r,a);clean();
 printf("Shader-object native creation: %u success/error/null/partial-handle/layout/push/stage cases and shared final-source lend PASS\n",cases);
}
static void lifetime(void){
 PGRAPHVkState r=fresh();r.in_command_buffer=true;r.submit_count=10;r.settled_submit=9;ShaderModuleInfo *m=make_owner(&r,VK_SHADER_STAGE_VERTEX_BIT);
 pgraph_vk_shader_object_use(&r,m);assert(m->refcnt==2&&m->object_gpu_ref&&r.shader_objects.gpu_owners==m&&m->object_retire_after==11);pgraph_vk_shader_object_use(&r,m);assert(m->refcnt==2&&!m->object_gpu_next);
 r.settled_submit=10;pgraph_vk_shader_objects_retire(&r);assert(alive(m)&&m->refcnt==2);
 r.submit_count=11;pgraph_vk_shader_object_use(&r,m);r.settled_submit=11;pgraph_vk_shader_objects_retire(&r);assert(alive(m)&&m->refcnt==2&&m->object_retire_after==12);
 r.settled_submit=12;pgraph_vk_shader_objects_retire(&r);assert(!r.shader_objects.gpu_owners&&!m->object_gpu_ref&&!m->object_gpu_next&&m->refcnt==1);pgraph_vk_unref_shader_module(&r,m);clean();
 /* A pending head cannot hide a completed later list member. */
 r=fresh();r.in_command_buffer=true;r.submit_count=39;r.settled_submit=38;ShaderModuleInfo *a=make_owner(&r,VK_SHADER_STAGE_VERTEX_BIT),*b=make_owner(&r,VK_SHADER_STAGE_FRAGMENT_BIT);pgraph_vk_shader_object_use(&r,a);r.submit_count=40;pgraph_vk_shader_object_use(&r,b);r.submit_count=41;pgraph_vk_shader_object_use(&r,b);r.settled_submit=40;pgraph_vk_shader_objects_retire(&r);assert(alive(a)&&!a->object_gpu_ref&&a->refcnt==1&&b->object_gpu_ref&&b->refcnt==2&&r.shader_objects.gpu_owners==b&&!b->object_gpu_next);pgraph_vk_unref_shader_module(&r,a);pgraph_vk_unref_shader_module(&r,b);assert(alive(b)&&b->refcnt==1);r.settled_submit=42;pgraph_vk_shader_objects_retire(&r);assert(!alive(b)&&!r.shader_objects.gpu_owners);clean();
 /* Same owner crosses submission serial wrap while an older recording settles. */
 r=fresh();r.in_command_buffer=true;r.submit_count=UINT32_MAX-1;r.settled_submit=UINT32_MAX-2;m=make_owner(&r,VK_SHADER_STAGE_VERTEX_BIT);pgraph_vk_shader_object_use(&r,m);r.submit_count=UINT32_MAX;pgraph_vk_shader_object_use(&r,m);assert(m->object_retire_after==0);r.settled_submit=UINT32_MAX;pgraph_vk_shader_objects_retire(&r);assert(alive(m)&&m->object_gpu_ref&&m->refcnt==2);r.settled_submit=0;pgraph_vk_shader_objects_retire(&r);assert(m->refcnt==1&&!m->object_gpu_ref);pgraph_vk_unref_shader_module(&r,m);clean();
 puts("Shader-object GPU owners: same/new recordings, delayed last consumer, unsorted retire list, release-to-GPU-only owner and serial wrap PASS");
}
static void fences_and_reset(void){
 PGRAPHVkState r=fresh();r.in_command_buffer=true;r.submit_count=7;r.settled_submit=7;ShaderModuleInfo *m=make_owner(&r,VK_SHADER_STAGE_VERTEX_BIT);pgraph_vk_shader_object_use(&r,m);pgraph_vk_unref_shader_module(&r,m);
 /* Waiting all SUBMITTED slots cannot release an owner in an open recording. */
 pgraph_vk_wait_all_slots(&r);assert(alive(m)&&m->object_gpu_ref);
 r.in_command_buffer=false;r.submit_count=8;r.slot_fence_pending[0]=true;r.slot_submit[0]=8;r.slot_fence[0]=(VkFence)(uintptr_t)80;r.slot_main[0]=(VkCommandBuffer)(uintptr_t)1;r.slot_sync[0]=(VkCommandBuffer)(uintptr_t)2;expected_fence=r.slot_fence[0];wait_guard=true;wait_returned=false;waits=ram_drops=0;pgraph_vk_wait_all_slots(&r);assert(wait_returned&&waits==1&&ram_drops==2&&!alive(m)&&!r.shader_objects.gpu_owners&&!r.slot_fence_pending[0]);clean();
 /* Newest same-queue fence covers earlier slots; pins survive until return. */
 r=fresh();r.in_command_buffer=true;r.submit_count=UINT32_MAX;r.settled_submit=UINT32_MAX-2;m=make_owner(&r,VK_SHADER_STAGE_VERTEX_BIT);pgraph_vk_shader_object_use(&r,m);pgraph_vk_unref_shader_module(&r,m);r.in_command_buffer=false;r.submit_count=0;for(unsigned i=0;i<2;i++){r.slot_fence_pending[i]=true;r.slot_submit[i]=i?0:UINT32_MAX;r.slot_fence[i]=(VkFence)(uintptr_t)(90+i);r.slot_main[i]=(VkCommandBuffer)(uintptr_t)(1+i);r.slot_sync[i]=(VkCommandBuffer)(uintptr_t)(3+i);}expected_fence=r.slot_fence[1];wait_guard=true;wait_returned=false;waits=ram_drops=0;pgraph_vk_wait_all_slots(&r);assert(waits==1&&ram_drops==4&&!alive(m)&&!r.shader_objects.gpu_owners&&r.settled_submit==0);clean();
 r=fresh();r.in_command_buffer=true;r.submit_count=11;r.settled_submit=10;m=make_owner(&r,VK_SHADER_STAGE_VERTEX_BIT);pgraph_vk_shader_object_use(&r,m);pgraph_vk_unref_shader_module(&r,m);r.slot_main[0]=(VkCommandBuffer)(uintptr_t)1;r.slot_sync[0]=(VkCommandBuffer)(uintptr_t)2;r.slot_submit[0]=11;PGRAPHState pg={&r};pgraph_vk_slot_retired(&pg,0);assert(alive(m));r.slot_submit[0]=12;pgraph_vk_slot_retired(&pg,0);assert(!alive(m)&&!r.shader_objects.gpu_owners);clean();
 puts("Shader-object fences/reset: open-recording pin, latest same-queue fence before destruction, wrapped completion and actual slot-retired hooks PASS");
}
static void shutdown_policy(void){
 for(unsigned open=0;open<2;open++)for(unsigned cost=0;cost<2;cost++){
  PGRAPHVkState r=fresh();r.in_command_buffer=true;r.submit_count=17;r.settled_submit=16;ShaderModuleInfo *m=make_owner(&r,VK_SHADER_STAGE_VERTEX_BIT);pgraph_vk_shader_object_use(&r,m);pgraph_vk_unref_shader_module(&r,m);
  if(!open){r.in_command_buffer=false;r.submit_count=18;r.slot_fence_pending[0]=true;r.slot_submit[0]=18;}
  r.slot_fence[0]=(VkFence)(uintptr_t)180;r.slot_main[0]=(VkCommandBuffer)(uintptr_t)1;r.slot_sync[0]=(VkCommandBuffer)(uintptr_t)2;expected_fence=r.slot_fence[0];wait_guard=true;wait_returned=false;waits=0;cost_active=cost;PGRAPHState pg={&r};shutdown_owner_boundary(&pg);assert(waits==1&&wait_returned&&!alive(m)&&!r.shader_objects.gpu_owners&&!r.in_command_buffer);clean();
 }
 PGRAPHVkState empty=fresh();PGRAPHState pg={&empty};waits=0;shutdown_owner_boundary(&pg);assert(!waits);cost_active=false;
 puts("Shader-object shutdown: actual renderer boundary selects synchronous policy for open/submitted records with cost model off/on PASS");
}

static ShaderModuleCacheEntry cache_entries[16];static unsigned cache_n;
static LruNode *lru_lookup(Lru *l,uint64_t hash,const void *opaque){
 const ShaderModuleCacheKey *key=opaque;l->lookups++;
 for(LruNode *n=l->bins[0];n;n=n->next){ShaderModuleCacheEntry *e=container_of(n,ShaderModuleCacheEntry,node);if(n->hash==hash&&!memcmp(&e->key,key,sizeof(*key)))return n;}
 assert(l->num_free&&cache_n<16);l->num_free--;l->initializers++;ShaderModuleCacheEntry *e=&cache_entries[cache_n++];memset(e,0,sizeof(*e));e->key=*key;e->node.hash=hash;e->node.next=l->bins[0];l->bins[0]=&e->node;
 PGRAPHVkState *r=container_of(l,PGRAPHVkState,shader_module_cache);e->module_info=make_owner(r,key->kind);return &e->node;
}
static void drop_cache(PGRAPHVkState *r){for(unsigned i=0;i<cache_n;i++)if(cache_entries[i].module_info)pgraph_vk_unref_shader_module(r,cache_entries[i].module_info);cache_n=0;memset(cache_entries,0,sizeof(cache_entries));clean();}
static void admission(void){
 unsigned cases=0;ShaderModuleCacheKey key={.kind=VK_SHADER_STAGE_VERTEX_BIT,.native_kind=1};
 for(unsigned free_slots=0;free_slots<=5;free_slots++)for(unsigned victims=0;victims<=5;victims++){
  PGRAPHVkState r=fresh();r.shader_module_cache.num_free=free_slots;r.shader_module_cache.victims=victims;unsigned initial=source_creations;ShaderModuleInfo *m=get_and_ref_shader_module_for_key(&r,&key);bool admitted=free_slots+victims>=4;
  if(admitted){assert(m&&m->refcnt==2&&!r.shader_objects.failed&&r.shader_module_cache.num_free>=3&&r.shader_module_cache.initializers==1);pgraph_vk_unref_shader_module(&r,m);}
  else assert(!m&&r.shader_objects.failed&&!r.shader_module_cache.initializers&&source_creations==initial);
  cases++;drop_cache(&r);
 }
 PGRAPHVkState r=fresh();r.shader_module_cache.num_free=5;ShaderModuleInfo *a=get_and_ref_shader_module_for_key(&r,&key);pgraph_vk_unref_shader_module(&r,a);r.shader_module_cache.num_free=0;r.shader_module_cache.victims=0;unsigned looked=r.shader_module_cache.lookups,evicted=r.shader_module_cache.evictions;assert(get_and_ref_shader_module_for_key(&r,&key)==a&&r.shader_module_cache.lookups==looked+1&&r.shader_module_cache.evictions==evicted);pgraph_vk_unref_shader_module(&r,a);
 /* All keys deliberately hash to17: exact bytes still decide admission/hits. */
 ShaderModuleCacheKey collision=key;collision.kind=VK_SHADER_STAGE_FRAGMENT_BIT;r.shader_module_cache.victims=4;ShaderModuleInfo *b=get_and_ref_shader_module_for_key(&r,&collision);assert(b&&b!=a&&b->stage==VK_SHADER_STAGE_FRAGMENT_BIT);pgraph_vk_unref_shader_module(&r,b);drop_cache(&r);
 r=fresh();r.shader_module_cache.num_free=0;ShaderModuleCacheEntry empty={.node={.hash=17},.key=key};r.shader_module_cache.bins[0]=&empty.node;assert(!get_and_ref_shader_module_for_key(&r,&key)&&r.shader_objects.failed&&!r.shader_module_cache.lookups&&!r.shader_module_cache.evictions);clean();
 ShaderModuleCacheEntry entry={0};Lru l={0};assert(shader_module_cache_entry_pre_evict(&l,&entry.node));ShaderModuleInfo ordinary={.refcnt=5};entry.module_info=&ordinary;assert(shader_module_cache_entry_pre_evict(&l,&entry.node));ShaderModuleInfo object={.refcnt=1,.shader_object=(VkShaderEXT)(uintptr_t)1};entry.module_info=&object;assert(shader_module_cache_entry_pre_evict(&l,&entry.node));object.refcnt=2;assert(!shader_module_cache_entry_pre_evict(&l,&entry.node));object.refcnt=1;object.object_gpu_ref=true;assert(!shader_module_cache_entry_pre_evict(&l,&entry.node));
 printf("Shader-object admission: %u free/victim budgets, three-slot ordinary fallback reserve, zero-free exact hits, hash collisions, incomplete owner and binding/GPU eviction pins PASS\n",cases);
}
int main(void){creation();lifetime();fences_and_reset();shutdown_policy();admission();assert(!heap_live&&!native_live);puts("Source-bound shader-object ownership/admission PASS");return 0;}
