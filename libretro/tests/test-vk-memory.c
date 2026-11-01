/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <volk.h>
#include "hw/xbox/nv2a/pgraph/vk/memory.h"
#include "vk-boot-image-trace.h"

typedef struct {
    bool live,mapped,dedicated;
    VkDeviceSize size;
    uint32_t type;
    unsigned kind,resource;
    unsigned char *bytes;
} Memory;
typedef struct {
    bool live,host,linear,required;
    VkDeviceSize size,alignment,offset;
    uint32_t bits;
    unsigned memory;
} Resource;
static Memory memories[4096];
static Resource buffers[2048],images[2048];
static VkPhysicalDeviceMemoryProperties properties;
static unsigned creates,frees,maps,unmaps,flushes,invalidates,live_memory,live_resources;
static unsigned denied_types;
static bool map_fail,bind_fail,require_dedicated;
static VkDeviceSize allocation_limit=UINT64_MAX,alignment=256,last_offset,last_size;
static uint32_t requirement_bits=7;
static unsigned number(uintptr_t handle){assert(handle && handle<4096);return (unsigned)handle;}
#define HANDLE(type,n) ((type)(uintptr_t)(n))
static VKAPI_ATTR void VKAPI_CALL get_memory(VkPhysicalDevice p,VkPhysicalDeviceMemoryProperties *out)
{(void)p;*out=properties;}
static VKAPI_ATTR void VKAPI_CALL get_properties(VkPhysicalDevice p,VkPhysicalDeviceProperties *out)
{(void)p;memset(out,0,sizeof(*out));out->limits.nonCoherentAtomSize=256;out->limits.maxMemoryAllocationCount=4096;}
static VKAPI_ATTR void VKAPI_CALL get_memory2(VkPhysicalDevice p,VkPhysicalDeviceMemoryProperties2 *out)
{(void)p;out->memoryProperties=properties;VkPhysicalDeviceMemoryBudgetPropertiesEXT *b=out->pNext;for(unsigned i=0;i<2;i++){b->heapBudget[i]=16u<<20;b->heapUsage[i]=0;}}
static VKAPI_ATTR VkResult VKAPI_CALL allocate_memory(VkDevice d,const VkMemoryAllocateInfo *ci,const VkAllocationCallbacks *cb,VkDeviceMemory *out)
{
    (void)d;(void)cb;
    if((denied_types&(1u<<ci->memoryTypeIndex))||ci->allocationSize>allocation_limit)return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    unsigned i=1;while(i<4096&&memories[i].live)i++;assert(i<4096);
    Memory *m=&memories[i];*m=(Memory){.live=true,.size=ci->allocationSize,.type=ci->memoryTypeIndex};
    m->bytes=calloc(1,(size_t)m->size);assert(m->bytes);
    if(ci->pNext){const VkMemoryDedicatedAllocateInfo *di=ci->pNext;assert(di->sType==VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO);assert(!!di->image!=!!di->buffer);m->dedicated=true;m->kind=di->image?2:1;m->resource=(unsigned)(uintptr_t)(di->image?di->image:(VkImage)di->buffer);}
    *out=HANDLE(VkDeviceMemory,i);creates++;live_memory++;return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL free_memory(VkDevice d,VkDeviceMemory memory,const VkAllocationCallbacks *cb)
{
    (void)d;(void)cb;unsigned n=number((uintptr_t)memory);Memory *m=&memories[n];assert(m->live&&!m->mapped);
    for(unsigned i=1;i<2048;i++){assert(!buffers[i].live||buffers[i].memory!=n);assert(!images[i].live||images[i].memory!=n);}
    free(m->bytes);memset(m,0,sizeof(*m));frees++;live_memory--;
}
static VKAPI_ATTR VkResult VKAPI_CALL map_memory(VkDevice d,VkDeviceMemory memory,VkDeviceSize offset,VkDeviceSize size,VkMemoryMapFlags flags,void **out)
{
    (void)d;(void)flags;Memory *m=&memories[number((uintptr_t)memory)];assert(m->live&&!m->mapped&&!offset&&size==VK_WHOLE_SIZE);assert(properties.memoryTypes[m->type].propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if(map_fail)return VK_ERROR_MEMORY_MAP_FAILED;
    m->mapped=true;*out=m->bytes;maps++;return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL unmap_memory(VkDevice d,VkDeviceMemory memory)
{(void)d;Memory *m=&memories[number((uintptr_t)memory)];assert(m->live&&m->mapped);m->mapped=false;unmaps++;}
static unsigned resource_create(Resource *array,VkDeviceSize size,bool linear)
{
    unsigned i=1;while(i<2048&&array[i].live)i++;assert(i<2048);
    array[i]=(Resource){.live=true,.size=size,.alignment=alignment,.bits=requirement_bits,.linear=linear,.required=require_dedicated};live_resources++;return i;
}
static VKAPI_ATTR VkResult VKAPI_CALL create_buffer(VkDevice d,const VkBufferCreateInfo *ci,const VkAllocationCallbacks *cb,VkBuffer *out)
{(void)d;(void)cb;*out=HANDLE(VkBuffer,resource_create(buffers,ci->size,true));return VK_SUCCESS;}
static VKAPI_ATTR VkResult VKAPI_CALL create_image(VkDevice d,const VkImageCreateInfo *ci,const VkAllocationCallbacks *cb,VkImage *out)
{(void)d;(void)cb;*out=HANDLE(VkImage,resource_create(images,ci->extent.width,ci->tiling==VK_IMAGE_TILING_LINEAR));return VK_SUCCESS;}
static VKAPI_ATTR void VKAPI_CALL destroy_buffer(VkDevice d,VkBuffer buffer,const VkAllocationCallbacks *cb)
{(void)d;(void)cb;Resource *r=&buffers[number((uintptr_t)buffer)];assert(r->live);memset(r,0,sizeof(*r));live_resources--;}
static VKAPI_ATTR void VKAPI_CALL destroy_image(VkDevice d,VkImage image,const VkAllocationCallbacks *cb)
{(void)d;(void)cb;Resource *r=&images[number((uintptr_t)image)];assert(r->live);memset(r,0,sizeof(*r));live_resources--;}
static void requirements(Resource *r,VkMemoryRequirements2 *out)
{out->memoryRequirements=(VkMemoryRequirements){r->size,r->alignment,r->bits};VkMemoryDedicatedRequirements *di=out->pNext;di->requiresDedicatedAllocation=r->required;di->prefersDedicatedAllocation=true;}
static VKAPI_ATTR void VKAPI_CALL buffer_requirements(VkDevice d,const VkBufferMemoryRequirementsInfo2 *ci,VkMemoryRequirements2 *out)
{(void)d;requirements(&buffers[number((uintptr_t)ci->buffer)],out);}
static VKAPI_ATTR void VKAPI_CALL image_requirements(VkDevice d,const VkImageMemoryRequirementsInfo2 *ci,VkMemoryRequirements2 *out)
{(void)d;requirements(&images[number((uintptr_t)ci->image)],out);}
static VkResult bind_resource(Resource *r,VkDeviceMemory memory,VkDeviceSize offset,unsigned kind,unsigned id)
{
    if(bind_fail)return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    Memory *m=&memories[number((uintptr_t)memory)];assert(m->live&&offset%r->alignment==0&&offset<=m->size&&r->size<=m->size-offset);
    if(r->required)assert(m->dedicated);
    if(m->dedicated){assert(m->kind==kind&&m->resource==id&&offset==0&&m->size==r->size);}
    unsigned resource_class=r->linear?1:2;
    if(!m->kind)m->kind=resource_class;
    else if(!m->dedicated)assert(m->kind==resource_class);
    for(unsigned i=1;i<2048;i++){
        Resource *others[]={&buffers[i],&images[i]};
        for(unsigned j=0;j<2;j++){Resource *o=others[j];if(o->live&&o!=r&&o->memory==(unsigned)(uintptr_t)memory)assert(offset+r->size<=o->offset||o->offset+o->size<=offset);}
    }
    r->memory=(unsigned)(uintptr_t)memory;r->offset=offset;return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL bind_buffer(VkDevice d,VkBuffer buffer,VkDeviceMemory memory,VkDeviceSize offset)
{(void)d;unsigned i=number((uintptr_t)buffer);return bind_resource(&buffers[i],memory,offset,1,i);}
static VKAPI_ATTR VkResult VKAPI_CALL bind_image(VkDevice d,VkImage image,VkDeviceMemory memory,VkDeviceSize offset)
{(void)d;unsigned i=number((uintptr_t)image);return bind_resource(&images[i],memory,offset,2,i);}
static VkResult cache_ranges(uint32_t count,const VkMappedMemoryRange *range)
{
    assert(count==1);Memory *m=&memories[number((uintptr_t)range->memory)];assert(m->live&&m->mapped&&range->offset%256==0&&range->size);
    assert(range->offset<=m->size&&range->size<=m->size-range->offset);
    assert(range->size%256==0||range->offset+range->size==m->size);
    last_offset=range->offset;last_size=range->size;return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL flush(VkDevice d,uint32_t count,const VkMappedMemoryRange *ranges)
{(void)d;flushes++;return cache_ranges(count,ranges);}
static VKAPI_ATTR VkResult VKAPI_CALL invalidate(VkDevice d,uint32_t count,const VkMappedMemoryRange *ranges)
{(void)d;invalidates++;return cache_ranges(count,ranges);}

PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties=get_memory;
PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties=get_properties;
PFN_vkGetPhysicalDeviceMemoryProperties2 vkGetPhysicalDeviceMemoryProperties2=get_memory2;
PFN_vkAllocateMemory vkAllocateMemory=allocate_memory;
PFN_vkFreeMemory vkFreeMemory=free_memory;
PFN_vkMapMemory vkMapMemory=map_memory;
PFN_vkUnmapMemory vkUnmapMemory=unmap_memory;
PFN_vkCreateBuffer vkCreateBuffer=create_buffer;
PFN_vkCreateImage vkCreateImage=create_image;
PFN_vkDestroyBuffer vkDestroyBuffer=destroy_buffer;
PFN_vkDestroyImage vkDestroyImage=destroy_image;
PFN_vkGetBufferMemoryRequirements2 vkGetBufferMemoryRequirements2=buffer_requirements;
PFN_vkGetImageMemoryRequirements2 vkGetImageMemoryRequirements2=image_requirements;
PFN_vkBindBufferMemory vkBindBufferMemory=bind_buffer;
PFN_vkBindImageMemory vkBindImageMemory=bind_image;
PFN_vkFlushMappedMemoryRanges vkFlushMappedMemoryRanges=flush;
PFN_vkInvalidateMappedMemoryRanges vkInvalidateMappedMemoryRanges=invalidate;

static uint32_t seed=731;
static unsigned random_value(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static VkBufferCreateInfo buffer_info(VkDeviceSize size){return (VkBufferCreateInfo){.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=size,.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};}
static VkImageCreateInfo image_info(unsigned size){return (VkImageCreateInfo){.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.extent={size,1,1},.tiling=VK_IMAGE_TILING_OPTIMAL};}
typedef struct ReclaimImage {
    XemuVkAllocator heap;
    VkImage image;
    XemuVkAllocation allocation;
    unsigned calls;
} ReclaimImage;
static void reclaim_image(void *opaque)
{
    ReclaimImage *r=opaque;r->calls++;
    if (r->image) {
        xemu_vk_destroy_image(r->heap,r->image,r->allocation);
        r->image=VK_NULL_HANDLE;r->allocation=NULL;
    }
}

static void replay_boot_images(bool old_capacity)
{
    XemuVkAllocator heap;
    properties.memoryHeaps[0].size=32u<<20;
    requirement_bits=1;alignment=4;
    assert(xemu_vk_allocator_create(HANDLE(VkPhysicalDevice,1),HANDLE(VkDevice,1),false,&heap)==VK_SUCCESS);
    VkDeviceSize capacity=old_capacity?(384ull<<20):xemu_vk_image_reservation(8);
    assert(xemu_vk_allocator_reserve(heap,0,0,capacity/256)==VK_SUCCESS);
    unsigned fixed_creates=creates,fixed_maps=maps;
    VkImage handles[BOOT_TRACE_SLOTS]={0};XemuVkAllocation spans[BOOT_TRACE_SLOTS]={0};
    bool exhausted=false;requirement_bits=1;
    for(unsigned i=0;i<sizeof(boot_trace)/sizeof(boot_trace[0]);i++) {
        unsigned slot=boot_trace[i].slot;
        if (!boot_trace[i].bytes) {
            assert(handles[slot]);xemu_vk_destroy_image(heap,handles[slot],spans[slot]);
            handles[slot]=VK_NULL_HANDLE;spans[slot]=NULL;
        } else {
            assert(!handles[slot]);alignment=boot_trace[i].alignment;
            VkImageCreateInfo ci=image_info(boot_trace[i].bytes);
            VkResult result=xemu_vk_create_image(heap,&ci,&(XemuVkMemoryRequest){0},&handles[slot],&spans[slot]);
            if(result!=VK_SUCCESS){
                assert(i>1&&old_capacity&&!handles[slot]&&!spans[slot]);
                assert(result==VK_ERROR_OUT_OF_DEVICE_MEMORY);
                assert(boot_trace[i].bytes==78643200u/256);
                XemuVkBudget observed[VK_MAX_MEMORY_HEAPS];
                xemu_vk_get_heap_budgets(heap,observed);
                assert(observed[0].usage==365405184u/256);
                exhausted=true;break;
            }
        }
    }
    assert(exhausted==old_capacity);
    assert(creates==fixed_creates&&maps==fixed_maps);
    for(unsigned i=0;i<BOOT_TRACE_SLOTS;i++)if(handles[i])xemu_vk_destroy_image(heap,handles[i],spans[i]);
    xemu_vk_allocator_destroy(heap);
    assert(!live_memory&&!live_resources);
}

int main(void)
{
    properties.memoryHeapCount=2;properties.memoryHeaps[0].size=properties.memoryHeaps[1].size=8u<<20;
    properties.memoryTypeCount=3;properties.memoryTypes[0]=(VkMemoryType){VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,0};
    properties.memoryTypes[1]=(VkMemoryType){VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,1};
    properties.memoryTypes[2]=(VkMemoryType){VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT,1};
    XemuVkAllocator a;assert(xemu_vk_allocator_create(HANDLE(VkPhysicalDevice,1),HANDLE(VkDevice,1),true,&a)==VK_SUCCESS);
    XemuVkMemoryRequest gpu={0},host={.host=true},dedicated={.host=true,.dedicated=true};
    VkImage ims[1000];XemuVkAllocation allocations[1000];VkImageCreateInfo ii=image_info(257);
    unsigned before=creates;
    for(unsigned i=0;i<1000;i++)assert(xemu_vk_create_image(a,&ii,&gpu,&ims[i],&allocations[i])==VK_SUCCESS);
    assert(creates-before==1); /* ignore preferred dedicated; suballocate */
    for(unsigned i=0;i<1000;i++)xemu_vk_destroy_image(a,ims[i],allocations[i]);
    for(unsigned i=0;i<1000;i++){assert(xemu_vk_create_image(a,&ii,&gpu,&ims[0],&allocations[0])==VK_SUCCESS);xemu_vk_destroy_image(a,ims[0],allocations[0]);}
    assert(creates-before==1); /* reuse the warm empty block */
    requirement_bits=4;VkBuffer b1,b2;XemuVkAllocation m1,m2;VkBufferCreateInfo bi=buffer_info(257);
    assert(xemu_vk_create_buffer(a,&bi,&host,&b1,&m1)==VK_SUCCESS);
    bi.size=513;assert(xemu_vk_create_buffer(a,&bi,&host,&b2,&m2)==VK_SUCCESS);
    assert(buffers[(uintptr_t)b2].offset==512);
    void *p1,*p2;assert(xemu_vk_map_memory(a,m1,&p1)==VK_SUCCESS&&xemu_vk_map_memory(a,m2,&p2)==VK_SUCCESS);
    assert((char*)p2-(char*)p1==512);unsigned mapped=maps;
    for(unsigned i=0;i<1000;i++)assert(xemu_vk_map_memory(a,m1,&p1)==VK_SUCCESS);
    assert(maps==mapped);
    assert(xemu_vk_flush_allocation(a,m1,256,1)==VK_SUCCESS&&last_offset==256&&last_size==256);
    assert(xemu_vk_invalidate_allocation(a,m2,0,VK_WHOLE_SIZE)==VK_SUCCESS&&last_offset==512&&last_size==768);
    assert(xemu_vk_flush_allocation(a,m1,257,1)!=VK_SUCCESS);
    xemu_vk_destroy_buffer(a,b1,m1);xemu_vk_destroy_buffer(a,b2,m2);
    bi.size=513;assert(xemu_vk_create_buffer(a,&bi,&dedicated,&b1,&m1)==VK_SUCCESS);
    assert(memories[buffers[(uintptr_t)b1].memory].dedicated);
    assert(xemu_vk_flush_allocation(a,m1,512,1)==VK_SUCCESS&&last_offset==512&&last_size==1);
    xemu_vk_destroy_buffer(a,b1,m1);
    requirement_bits=7;require_dedicated=true;
    assert(xemu_vk_create_image(a,&ii,&gpu,&ims[0],&allocations[0])==VK_SUCCESS);
    assert(memories[images[(uintptr_t)ims[0]].memory].dedicated);xemu_vk_destroy_image(a,ims[0],allocations[0]);require_dedicated=false;
    xemu_vk_trim_memory(a);assert(!live_memory);
    denied_types=1;assert(xemu_vk_create_image(a,&ii,&gpu,&ims[0],&allocations[0])==VK_SUCCESS);
    assert(memories[images[(uintptr_t)ims[0]].memory].type!=0);xemu_vk_destroy_image(a,ims[0],allocations[0]);denied_types=0;
    xemu_vk_trim_memory(a);allocation_limit=1024;
    assert(xemu_vk_create_image(a,&ii,&gpu,&ims[0],&allocations[0])==VK_SUCCESS);xemu_vk_destroy_image(a,ims[0],allocations[0]);allocation_limit=UINT64_MAX;
    xemu_vk_trim_memory(a);requirement_bits=2;map_fail=true;
    assert(xemu_vk_create_buffer(a,&bi,&host,&b1,&m1)==VK_ERROR_MEMORY_MAP_FAILED&&!b1&&!m1&&!live_memory&&!live_resources);map_fail=false;
    requirement_bits=7;bind_fail=true;assert(xemu_vk_create_image(a,&ii,&gpu,&ims[0],&allocations[0])!=VK_SUCCESS&&!ims[0]&&!allocations[0]&&!live_resources);bind_fail=false;
    typedef struct {VkBuffer b;VkImage image;XemuVkAllocation allocation;unsigned char tag;} Live;
    Live live[512]={0};
    for(unsigned n=0;n<40000;n++) {
        unsigned index=random_value()%512;Live *l=&live[index];
        if(l->allocation){
            Resource *r=l->b?&buffers[(uintptr_t)l->b]:&images[(uintptr_t)l->image];
            for(VkDeviceSize i=0;i<r->size;i++)assert(memories[r->memory].bytes[r->offset+i]==l->tag);
            if(l->b)xemu_vk_destroy_buffer(a,l->b,l->allocation);else xemu_vk_destroy_image(a,l->image,l->allocation);
            memset(l,0,sizeof(*l));
        }else{
            alignment=1u<<(4+random_value()%9);unsigned bytes=1+random_value()%4000;
            bool use_host=random_value()%2;requirement_bits=use_host?6:7;
            if(random_value()%2){bi=buffer_info(bytes);assert(xemu_vk_create_buffer(a,&bi,use_host?&host:&gpu,&l->b,&l->allocation)==VK_SUCCESS);}
            else {ii=image_info(bytes);assert(xemu_vk_create_image(a,&ii,&gpu,&l->image,&l->allocation)==VK_SUCCESS);}
            Resource *r=l->b?&buffers[(uintptr_t)l->b]:&images[(uintptr_t)l->image];l->tag=(unsigned char)random_value();memset(memories[r->memory].bytes+r->offset,l->tag,(size_t)r->size);
        }
        if(n%317==0)xemu_vk_trim_memory(a); /* cannot touch any live resource */
    }
    for(unsigned i=0;i<512;i++)if(live[i].allocation){if(live[i].b)xemu_vk_destroy_buffer(a,live[i].b,live[i].allocation);else xemu_vk_destroy_image(a,live[i].image,live[i].allocation);}
    xemu_vk_allocator_destroy(a);assert(!live_memory&&!live_resources&&creates==frees&&maps==unmaps);
    /* A sealed gameplay heap never allocates/maps/frees backing memory,
     * including exhaustion, incompatible types and mandatory dedicated requests. */
    requirement_bits=7;alignment=256;
    assert(xemu_vk_allocator_create(HANDLE(VkPhysicalDevice,1),HANDLE(VkDevice,1),false,&a)==VK_SUCCESS);
    assert(xemu_vk_allocator_reserve(a,1u<<20,1u<<20,1u<<20)==VK_SUCCESS);
    unsigned fixed_creates=creates,fixed_maps=maps,fixed_frees=frees;
    XemuVkBudget budgets[VK_MAX_MEMORY_HEAPS];
    xemu_vk_get_heap_budgets(a,budgets);
    assert(budgets[0].usage==0&&budgets[1].usage==0);
    ReclaimImage retired={.heap=a};ii=image_info(1u<<20);
    assert(xemu_vk_create_image(a,&ii,&gpu,&retired.image,&retired.allocation)==VK_SUCCESS);
    xemu_vk_allocator_set_reclaim(a,reclaim_image,&retired);
    /* Restrict to the image type: pressure must release the retired resource,
     * not spill to another type or allocate another driver block. */
    requirement_bits=1;
    assert(xemu_vk_create_image(a,&ii,&gpu,&ims[0],&allocations[0])==VK_SUCCESS);
    assert(retired.calls==1&&!retired.image);
    xemu_vk_destroy_image(a,ims[0],allocations[0]);requirement_bits=7;
    /* Exactly consume the host reservation, then reject one byte more.
     * This is the startup buffer packing boundary, not a guessed margin. */
    VkBuffer packed[16];XemuVkAllocation packed_allocations[16];
    bi=buffer_info(65536);
    for(unsigned n=0;n<16;n++)
        assert(xemu_vk_create_buffer(a,&bi,&host,&packed[n],&packed_allocations[n])==VK_SUCCESS);
    bi=buffer_info(1);
    assert(xemu_vk_create_buffer(a,&bi,&host,&b1,&m1)!=VK_SUCCESS&&!b1&&!m1);
    xemu_vk_get_heap_budgets(a,budgets);assert(budgets[1].usage==(1u<<20));
    for(unsigned n=0;n<16;n++) xemu_vk_destroy_buffer(a,packed[n],packed_allocations[n]);
    xemu_vk_get_heap_budgets(a,budgets);assert(budgets[1].usage==0);
    for (unsigned n=0;n<4000;n++) {
        ii=image_info(4096);
        assert(xemu_vk_create_image(a,&ii,&gpu,&ims[0],&allocations[0])==VK_SUCCESS);
        xemu_vk_destroy_image(a,ims[0],allocations[0]);
        bi=buffer_info(4096);
        assert(xemu_vk_create_buffer(a,&bi,&host,&b1,&m1)==VK_SUCCESS);
        xemu_vk_destroy_buffer(a,b1,m1);
        xemu_vk_trim_memory(a);
    }
    bi=buffer_info(4u<<20);
    assert(xemu_vk_create_buffer(a,&bi,&host,&b1,&m1)!=VK_SUCCESS&&!b1&&!m1);
    require_dedicated=true;ii=image_info(4096);
    assert(xemu_vk_create_image(a,&ii,&gpu,&ims[0],&allocations[0])!=VK_SUCCESS&&!ims[0]&&!allocations[0]);
    require_dedicated=false;requirement_bits=2;
    bi=buffer_info(4096);
    assert(xemu_vk_create_buffer(a,&bi,&host,&b1,&m1)!=VK_SUCCESS&&!b1&&!m1);
    assert(creates==fixed_creates&&maps==fixed_maps&&frees==fixed_frees);
    xemu_vk_allocator_destroy(a);
    assert(!live_memory&&!live_resources&&creates==frees&&maps==unmaps);
    puts("PASS: fixed startup reservation, 4000 gameplay reuse cycles, capacity/type/dedicated exhaustion: zero driver allocations, maps or frees");
    assert(xemu_vk_image_reservation(0)==(128ull<<20));
    assert(xemu_vk_image_reservation(1)==(128ull<<20));
    assert(xemu_vk_image_reservation(2)==(128ull<<20));
    assert(xemu_vk_image_reservation(4)==(512ull<<20));
    assert(xemu_vk_image_reservation(8)>=BOOT_TRACE_PEAK_BYTES);
    assert(xemu_vk_image_reservation(UINT32_MAX)==UINT64_MAX);
    replay_boot_images(true);replay_boot_images(false);
    assert(creates==frees&&maps==unmaps);
    puts("PASS: complete NGB cold-boot image lifetimes: old pool reproduces OOM; corrected startup reservation fits with no driver growth");
    printf("PASS: 40000 fragmented allocation schedules, alignment/class separation, 1000 images per driver block, warm reuse, persistent maps, atom-isolated flush/invalidate, dedicated requirements, type/OOM fallback and bind/map rollback; no leaks\n");
}
