/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* The GSdx GSVulkanHeap and parallel-dc vk_heap use sorted free spans and
 * persistent mappings. This heap follows that model, with Xbox buffer/image
 * lifetimes, required dedicated allocations and noncoherent ranges. */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <vk_dispatch.h>
#include "memory.h"

#define IMAGE_BLOCK_BYTES (16ull << 20)
#define HOST_BLOCK_BYTES (16ull << 20)
#define MEMORY_RECORDS 256
#define FIXED_RECORDS 65536
#define LINEAR_SPANS 128 /* Twenty startup buffers, including both ring sets. */

typedef struct Span { VkDeviceSize offset, size; } Span;
typedef struct Block {
    struct Block *next;
    VkDeviceMemory memory;
    VkDeviceSize size, used, max_free;
    void *mapped;
    uint32_t type, live, free_count, free_capacity;
    bool linear, dedicated, fixed;
    Span *free;
} Block;

struct XemuVkMemory {
    struct XemuVkMemory *next;
    Block *block;
    VkDeviceSize offset, size, reserved;
};
typedef struct MemoryPage {
    struct MemoryPage *next;
    struct XemuVkMemory records[MEMORY_RECORDS];
} MemoryPage;
struct XemuVkHeap {
    VkDevice device;
    VkPhysicalDevice physical;
    VkPhysicalDeviceMemoryProperties props;
    VkDeviceSize atom;
    Block *blocks;
    MemoryPage *pages;
    XemuVkAllocation free_records;
    XemuVkAllocation fixed_records;
    uint32_t fixed_next;
    VkDeviceSize reserved[VK_MAX_MEMORY_HEAPS];
    VkDeviceSize external_usage[VK_MAX_MEMORY_HEAPS];
    VkDeviceSize budget[VK_MAX_MEMORY_HEAPS];
    uint32_t allocation_count, max_allocations, last_budget_frame;
    bool memory_budget, sealed;
    void (*reclaim)(void *);
    void *reclaim_opaque;
};

static bool align_size(VkDeviceSize value, VkDeviceSize alignment,
                        VkDeviceSize *out)
{
    if (!alignment || value > UINT64_MAX - (alignment - 1)) return false;
    *out = (value + alignment - 1) & ~(alignment - 1);
    return true;
}

static VkDeviceSize block_size(XemuVkAllocator a, uint32_t type, bool host,
                                VkDeviceSize need)
{
    VkDeviceSize size = host ? HOST_BLOCK_BYTES : IMAGE_BLOCK_BYTES;
    VkDeviceSize eighth = a->props.memoryHeaps[a->props.memoryTypes[type].heapIndex].size / 8;
    if (eighth && size > eighth) size = eighth;
    uint32_t heap = a->props.memoryTypes[type].heapIndex;
    VkDeviceSize usage = a->external_usage[heap] + a->reserved[heap];
    VkDeviceSize headroom = a->budget[heap] > usage ? a->budget[heap] - usage : 0;
    if (size > headroom) size = headroom;
    if (size < need) size = need;
    VkDeviceSize rounded;
    return align_size(size, 65536, &rounded) ? rounded : need;
}

static bool spans_reserve(Block *b, uint32_t need)
{
    if (b->free_capacity >= need) return true;
    if (b->fixed) return false;
    uint32_t cap = b->free_capacity ? b->free_capacity : 16;
    while (cap < need) {
        if (cap > UINT32_MAX / 2) return false;
        cap *= 2;
    }
#if SIZE_MAX <= UINT32_MAX
    if ((size_t)cap > SIZE_MAX / sizeof(Span)) return false;
#endif
    Span *spans = realloc(b->free, (size_t)cap * sizeof(*spans));
    if (!spans) return false;
    b->free = spans;
    b->free_capacity = cap;
    return true;
}

static XemuVkAllocation record_get(XemuVkAllocator a)
{
    if (!a->free_records) {
        if (a->sealed) {
            if (a->fixed_next == FIXED_RECORDS) return NULL;
            return &a->fixed_records[a->fixed_next++];
        }
        MemoryPage *p = calloc(1, sizeof(*p));
        if (!p) return NULL;
        p->next = a->pages;
        a->pages = p;
        for (unsigned i = 0; i < MEMORY_RECORDS; i++) {
            p->records[i].next = a->free_records;
            a->free_records = &p->records[i];
        }
    }
    XemuVkAllocation result = a->free_records;
    a->free_records = result->next;
    memset(result, 0, sizeof(*result));
    return result;
}

static void record_put(XemuVkAllocator a, XemuVkAllocation allocation)
{
    allocation->block = NULL;
    allocation->next = a->free_records;
    a->free_records = allocation;
}

static void block_drop(XemuVkAllocator a, Block *b)
{
    assert(!b->live);
    Block **link = &a->blocks;
    while (*link != b) link = &(*link)->next;
    *link = b->next;
    if (b->mapped) vkUnmapMemory(a->device, b->memory);
    vkFreeMemory(a->device, b->memory, NULL);
    a->reserved[a->props.memoryTypes[b->type].heapIndex] -= b->size;
    a->allocation_count--;
    free(b->free);
    free(b);
}

void xemu_vk_trim_memory(XemuVkAllocator a)
{
    if (a->sealed) return;
    for (Block *b = a->blocks, *next; b; b = next) {
        next = b->next;
        if (!b->live && !b->fixed) block_drop(a, b);
    }
}

static VkResult block_new(XemuVkAllocator a, uint32_t type, bool linear,
    bool host, bool dedicated, VkDeviceSize bytes, VkBuffer buffer,
    VkImage image, Block **out)
{
    if (a->sealed && image == VK_NULL_HANDLE) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if (a->allocation_count >= a->max_allocations) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    Block *b = calloc(1, sizeof(*b));
    if (!b) return VK_ERROR_OUT_OF_HOST_MEMORY;
    if (!dedicated && !spans_reserve(b, 16)) { free(b); return VK_ERROR_OUT_OF_HOST_MEMORY; }
    VkMemoryDedicatedAllocateInfo di = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .image = image, .buffer = buffer,
    };
    VkMemoryAllocateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = dedicated ? &di : NULL,
        .allocationSize = bytes, .memoryTypeIndex = type,
    };
    VkResult result = vkAllocateMemory(a->device, &ci, NULL, &b->memory);
    if (result != VK_SUCCESS) { free(b->free); free(b); return result; }
    if (host) {
        result = vkMapMemory(a->device, b->memory, 0, VK_WHOLE_SIZE, 0, &b->mapped);
        if (result != VK_SUCCESS) {
            vkFreeMemory(a->device, b->memory, NULL);
            free(b->free); free(b); return result;
        }
    }
    b->size = bytes; b->type = type; b->linear = linear; b->dedicated = dedicated;
    b->max_free = bytes;
    if (!dedicated) { b->free[0] = (Span){0,bytes}; b->free_count = 1; }
    b->next = a->blocks; a->blocks = b;
    a->reserved[a->props.memoryTypes[type].heapIndex] += bytes;
    a->allocation_count++;
    *out = b;
    return VK_SUCCESS;
}

static VkResult block_take(Block *b, VkDeviceSize size, VkDeviceSize alignment,
                            XemuVkAllocation out)
{
    VkDeviceSize max_free = 0;
    for (uint32_t i = 0; i < b->free_count; i++) {
        Span span = b->free[i];
        if (span.size > max_free) max_free = span.size;
        VkDeviceSize offset;
        if (!align_size(span.offset, alignment, &offset) || offset < span.offset ||
            offset - span.offset > span.size || size > span.size - (offset - span.offset)) continue;
        /* Reserve enough metadata for every live allocation to be returned
         * in any order. Free never allocates and cannot lose a span on OOM. */
        if (b->live > UINT32_MAX - b->free_count - 2 ||
            !spans_reserve(b, b->live + b->free_count + 2)) return VK_ERROR_OUT_OF_HOST_MEMORY;
        VkDeviceSize prefix = offset - span.offset;
        VkDeviceSize suffix = span.size - prefix - size;
        if (prefix && suffix) {
            memmove(b->free+i+2,b->free+i+1,(b->free_count-i-1)*sizeof(Span));
            b->free[i] = (Span){span.offset,prefix};
            b->free[i+1] = (Span){offset+size,suffix};
            b->free_count++;
        } else if (prefix || suffix) {
            b->free[i] = prefix ? (Span){span.offset,prefix} : (Span){offset+size,suffix};
        } else {
            memmove(b->free+i,b->free+i+1,(b->free_count-i-1)*sizeof(Span));
            b->free_count--;
        }
        b->used += size; b->live++;
        out->block = b; out->offset = offset; out->reserved = size;
        return VK_SUCCESS;
    }
    b->max_free = max_free;
    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

static void allocation_free(XemuVkAllocator a, XemuVkAllocation allocation)
{
    if (!allocation) return;
    Block *b = allocation->block;
    assert(b && b->live && allocation->reserved <= b->used);
    b->live--; b->used -= allocation->reserved;
    if (!b->dedicated) {
        uint32_t lo = 0, hi = b->free_count;
        while (lo < hi) { uint32_t mid = lo + (hi-lo)/2;
            if (b->free[mid].offset < allocation->offset) lo = mid+1; else hi = mid; }
        VkDeviceSize start = allocation->offset, size = allocation->reserved;
        assert(lo == 0 || b->free[lo-1].offset+b->free[lo-1].size <= start);
        assert(lo == b->free_count || start+size <= b->free[lo].offset);
        if (lo && b->free[lo-1].offset+b->free[lo-1].size == start) {
            lo--; start = b->free[lo].offset; size += b->free[lo].size;
            memmove(b->free+lo,b->free+lo+1,(--b->free_count-lo)*sizeof(Span));
        }
        if (lo < b->free_count && start+size == b->free[lo].offset) {
            size += b->free[lo].size;
            memmove(b->free+lo,b->free+lo+1,(--b->free_count-lo)*sizeof(Span));
        }
        assert(b->free_count < b->free_capacity);
        memmove(b->free+lo+1,b->free+lo,(b->free_count-lo)*sizeof(Span));
        b->free[lo] = (Span){start,size}; b->free_count++;
        if (size > b->max_free) b->max_free = size;
    }
    if (!b->live && !b->fixed) {
        uint32_t heap = a->props.memoryTypes[b->type].heapIndex;
        if (b->dedicated || a->sealed ||
            a->external_usage[heap] + a->reserved[heap] > a->budget[heap]) block_drop(a,b);
        else {
            /* One warm empty block per kind. Give obsolete spare blocks
             * back instead of retaining every peak for the whole session. */
            for (Block *other=a->blocks,*next; other; other=next) {
                next=other->next;
                if (other!=b && !other->live && !other->fixed && !other->dedicated &&
                    other->type==b->type && other->linear==b->linear) block_drop(a,other);
            }
        }
    }
    record_put(a,allocation);
}

static VkResult allocate(XemuVkAllocator a, const VkMemoryRequirements *req,
    const XemuVkMemoryRequest *request, bool linear, bool driver_dedicated,
    VkBuffer buffer, VkImage image, XemuVkAllocation *out)
{
    *out = NULL;
    if (!req->size || !req->alignment || (req->alignment & (req->alignment-1))) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    XemuVkAllocation allocation = record_get(a);
    if (!allocation) return VK_ERROR_OUT_OF_HOST_MEMORY;
    const bool dedicated = request->dedicated || driver_dedicated;
    uint32_t types[VK_MAX_MEMORY_TYPES], count=0;
    int scores[VK_MAX_MEMORY_TYPES];
    for (uint32_t i=0;i<a->props.memoryTypeCount;i++) {
        VkMemoryPropertyFlags flags=a->props.memoryTypes[i].propertyFlags;
        if (!(req->memoryTypeBits & (1u<<i)) ||
            (flags & (VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT|VK_MEMORY_PROPERTY_PROTECTED_BIT)) ||
            (request->host && !(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))) continue;
        int score = request->host ?
            ((flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)?8:0) + ((flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)?4:0) :
            ((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)?16:0) - ((flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)?1:0);
        uint32_t pos=count++;
        while (pos && scores[pos-1]<score) { types[pos]=types[pos-1]; scores[pos]=scores[pos-1]; pos--; }
        types[pos]=i; scores[pos]=score;
    }
    VkResult result=VK_ERROR_OUT_OF_DEVICE_MEMORY;
    for (unsigned retry=0;retry<2;retry++) {
        if (retry) {
            if ((!dedicated || image != VK_NULL_HANDLE) && a->reclaim)
                a->reclaim(a->reclaim_opaque);
            xemu_vk_trim_memory(a);
        }
        for (uint32_t t=0;t<count;t++) {
            uint32_t type=types[t];
            VkMemoryPropertyFlags flags=a->props.memoryTypes[type].propertyFlags;
            VkDeviceSize size=req->size, alignment=req->alignment;
            if (!dedicated && request->host && !(flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                if (alignment<a->atom) alignment=a->atom;
                if (!align_size(size,a->atom,&size)) continue;
            }
            if (!dedicated) for (Block *b=a->blocks;b;b=b->next) {
                if (b->dedicated || b->type!=type || b->linear!=linear ||
                    (request->host && !b->mapped) || b->max_free<size) continue;
                result=block_take(b,size,alignment,allocation);
                if (result==VK_SUCCESS) goto success;
                if (result==VK_ERROR_OUT_OF_HOST_MEMORY) goto fail;
            }
            VkDeviceSize bytes=dedicated?req->size:block_size(a,type,request->host,size);
            Block *b;
            /* Keep startup buffer backing fixed. Images may exceed the
             * reservation during scene transitions or require another type.
             * Try overflow only after reclaiming already-completed images. */
            if (a->sealed && (image == VK_NULL_HANDLE || !retry)) continue;
            result=block_new(a,type,linear,request->host,dedicated,bytes,buffer,image,&b);
            if (result!=VK_SUCCESS && !dedicated && bytes!=size)
                result=block_new(a,type,linear,request->host,false,size,buffer,image,&b);
            if (result!=VK_SUCCESS) continue;
            if (dedicated) {
                b->live=1; b->used=req->size;
                allocation->block=b; allocation->offset=0; allocation->reserved=req->size;
                goto success;
            }
            result=block_take(b,size,alignment,allocation);
            if (result==VK_SUCCESS) goto success;
            block_drop(a,b);
            if (result==VK_ERROR_OUT_OF_HOST_MEMORY) goto fail;
        }
    }
fail:
    record_put(a,allocation);
    return result;
success:
    allocation->size=req->size;
    *out=allocation;
    return VK_SUCCESS;
}

void xemu_vk_update_budget(XemuVkAllocator a, uint32_t frame)
{
    if ((uint32_t)(frame-a->last_budget_frame)<30) return;
    a->last_budget_frame=frame;
    if (!a->memory_budget) return;
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
    VkPhysicalDeviceMemoryProperties2 props={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,.pNext=&budget};
    vkGetPhysicalDeviceMemoryProperties2(a->physical,&props);
    for (uint32_t i=0;i<a->props.memoryHeapCount;i++) {
        if (budget.heapBudget[i]) a->budget[i]=budget.heapBudget[i];
        a->external_usage[i]=budget.heapUsage[i]>a->reserved[i]?budget.heapUsage[i]-a->reserved[i]:0;
    }
}

VkDeviceSize xemu_vk_image_reservation(uint32_t scale)
{
    /* Cold-boot render targets coexist with the first game targets, unlike
     * a freshly loaded savestate. Leave room for that transition before
     * sealing: 128 MiB at 1x/2x, 512 MiB at 4x, 2 GiB at 8x. */
    VkDeviceSize factor=scale?scale:1;
    const VkDeviceSize per_scale=32ull<<20;
    if (factor>UINT64_MAX/per_scale/factor) return UINT64_MAX;
    VkDeviceSize bytes=factor*factor*per_scale;
    return bytes<(128ull<<20)?(128ull<<20):bytes;
}

VkResult xemu_vk_allocator_create(VkPhysicalDevice physical, VkDevice device,
                                  bool memory_budget, XemuVkAllocator *out)
{
    *out=NULL;
    XemuVkAllocator a=calloc(1,sizeof(*a));
    if (!a) return VK_ERROR_OUT_OF_HOST_MEMORY;
    a->device=device; a->physical=physical; a->memory_budget=memory_budget;
    vkGetPhysicalDeviceMemoryProperties(physical,&a->props);
    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(physical,&props);
    a->atom=props.limits.nonCoherentAtomSize?props.limits.nonCoherentAtomSize:1;
    a->max_allocations=props.limits.maxMemoryAllocationCount;
    for (uint32_t i=0;i<a->props.memoryHeapCount;i++) a->budget[i]=a->props.memoryHeaps[i].size/10*9;
    a->last_budget_frame=UINT32_MAX-30;
    xemu_vk_update_budget(a,0);
    *out=a; return VK_SUCCESS;
}

/* Probe common buffer/image memory compatibility before committing the fixed
 * reservation. Image requirements or peaks outside that reservation can use
 * reclaim-first overflow backing; startup buffer backing remains fixed. */
VkResult xemu_vk_allocator_reserve(XemuVkAllocator a, VkDeviceSize host_bytes,
    VkDeviceSize device_bytes, VkDeviceSize image_bytes)
{
    if (a->sealed) return VK_ERROR_INITIALIZATION_FAILED;
    Block *original_blocks=a->blocks;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkBufferCreateInfo bc = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = 4096,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkImageCreateInfo ic = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {64,64,1},
        .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkResult result = vkCreateBuffer(a->device, &bc, NULL, &buffer);
    if (result != VK_SUCCESS) return result;
    result = vkCreateImage(a->device, &ic, NULL, &image);
    if (result != VK_SUCCESS) goto cleanup;
    VkMemoryDedicatedRequirements bd = {.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
    VkMemoryDedicatedRequirements id = {.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
    VkMemoryRequirements2 br = {.sType=VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,.pNext=&bd};
    VkMemoryRequirements2 ir = {.sType=VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,.pNext=&id};
    VkBufferMemoryRequirementsInfo2 bq = {.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,.buffer=buffer};
    VkImageMemoryRequirementsInfo2 iq = {.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,.image=image};
    vkGetBufferMemoryRequirements2(a->device,&bq,&br);
    vkGetImageMemoryRequirements2(a->device,&iq,&ir);
    if (bd.requiresDedicatedAllocation || id.requiresDedicatedAllocation) {
        result = VK_ERROR_FEATURE_NOT_PRESENT; goto cleanup;
    }
    const VkDeviceSize sizes[] = {host_bytes,device_bytes,image_bytes};
    const uint32_t bits[] = {br.memoryRequirements.memoryTypeBits,
                            br.memoryRequirements.memoryTypeBits,ir.memoryRequirements.memoryTypeBits};
    for (unsigned kind=0;kind<3;kind++) {
        if (!sizes[kind]) continue;
        uint32_t best=UINT32_MAX;
        int best_score=-100;
        for (uint32_t type=0;type<a->props.memoryTypeCount;type++) {
            VkMemoryPropertyFlags flags=a->props.memoryTypes[type].propertyFlags;
            if (!(bits[kind] & (1u<<type)) ||
                (flags & (VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT|VK_MEMORY_PROPERTY_PROTECTED_BIT)) ||
                (!kind && !(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))) continue;
            uint32_t heap=a->props.memoryTypes[type].heapIndex;
            VkDeviceSize usage=a->external_usage[heap]+a->reserved[heap];
            if (usage>a->budget[heap] || sizes[kind]>a->budget[heap]-usage) continue;
            int score=!kind ?
                ((flags&VK_MEMORY_PROPERTY_HOST_CACHED_BIT)?8:0)+((flags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)?4:0) :
                ((flags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)?16:0)-((flags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)?1:0);
            if (score>best_score) {best=type;best_score=score;}
        }
        if (best==UINT32_MAX) {result=VK_ERROR_OUT_OF_DEVICE_MEMORY;goto cleanup;}
        Block *block;
        result=block_new(a,best,kind!=2,kind==0,false,sizes[kind],VK_NULL_HANDLE,VK_NULL_HANDLE,&block);
        if (result!=VK_SUCCESS) goto cleanup;
        /* Two possible free spans per record, including alignment gaps. */
        if (!spans_reserve(block,kind==2?2*FIXED_RECORDS+3:LINEAR_SPANS)) {
            result=VK_ERROR_OUT_OF_HOST_MEMORY;goto cleanup;
        }
        block->fixed=true;
    }
    /* Reserve the address space without touching unused records. The owner
     * consumes these monotonically before recycling its free list. */
    a->fixed_records=calloc(FIXED_RECORDS,sizeof(*a->fixed_records));
    if (!a->fixed_records) {result=VK_ERROR_OUT_OF_HOST_MEMORY;goto cleanup;}
    a->sealed=true;
cleanup:
    if (image) vkDestroyImage(a->device,image,NULL);
    vkDestroyBuffer(a->device,buffer,NULL);
    if (result!=VK_SUCCESS) {
        while(a->blocks!=original_blocks) block_drop(a,a->blocks);
        free(a->fixed_records);a->fixed_records=NULL;
    }
    return result;
}

void xemu_vk_allocator_set_reclaim(XemuVkAllocator a,
    void (*reclaim)(void *), void *opaque)
{
    a->reclaim=reclaim;
    a->reclaim_opaque=opaque;
}

void xemu_vk_allocator_destroy(XemuVkAllocator a)
{
    if (!a) return;
    while (a->blocks) { assert(!a->blocks->live); block_drop(a,a->blocks); }
    while (a->pages) { MemoryPage *p=a->pages; a->pages=p->next; free(p); }
    free(a->fixed_records);
    free(a);
}

VkResult xemu_vk_create_buffer(XemuVkAllocator a, const VkBufferCreateInfo *ci,
    const XemuVkMemoryRequest *request, VkBuffer *buffer, XemuVkAllocation *allocation)
{
    *buffer=VK_NULL_HANDLE; *allocation=NULL;
    VkResult result=vkCreateBuffer(a->device,ci,NULL,buffer);
    if (result!=VK_SUCCESS) return result;
    VkMemoryDedicatedRequirements dedicated={.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
    VkMemoryRequirements2 req={.sType=VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,.pNext=&dedicated};
    VkBufferMemoryRequirementsInfo2 info={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,.buffer=*buffer};
    vkGetBufferMemoryRequirements2(a->device,&info,&req);
    result=allocate(a,&req.memoryRequirements,request,true,dedicated.requiresDedicatedAllocation,*buffer,VK_NULL_HANDLE,allocation);
    if (result==VK_SUCCESS) result=vkBindBufferMemory(a->device,*buffer,(*allocation)->block->memory,(*allocation)->offset);
    if (result!=VK_SUCCESS) {
        vkDestroyBuffer(a->device,*buffer,NULL); *buffer=VK_NULL_HANDLE;
        allocation_free(a,*allocation); *allocation=NULL;
    }
    return result;
}

VkResult xemu_vk_create_image(XemuVkAllocator a, const VkImageCreateInfo *ci,
    const XemuVkMemoryRequest *request, VkImage *image, XemuVkAllocation *allocation)
{
    *image=VK_NULL_HANDLE; *allocation=NULL;
    VkResult result=vkCreateImage(a->device,ci,NULL,image);
    if ((result==VK_ERROR_OUT_OF_DEVICE_MEMORY || result==VK_ERROR_OUT_OF_HOST_MEMORY) &&
        a->reclaim) {
        /* Driver image objects can exhaust memory before requirements or
         * suballocation are reached. Reclaim completed images here too. */
        a->reclaim(a->reclaim_opaque);
        *image=VK_NULL_HANDLE;
        result=vkCreateImage(a->device,ci,NULL,image);
    }
    if (result!=VK_SUCCESS) {
        fprintf(stderr,"nv2a: Vulkan image creation failed: %ux%ux%u format=%u usage=%x result=%d\n",
            ci->extent.width,ci->extent.height,ci->extent.depth,ci->format,ci->usage,result);
        *image=VK_NULL_HANDLE;
        return result;
    }
    VkMemoryDedicatedRequirements dedicated={.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
    VkMemoryRequirements2 req={.sType=VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,.pNext=&dedicated};
    VkImageMemoryRequirementsInfo2 info={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,.image=*image};
    vkGetImageMemoryRequirements2(a->device,&info,&req);
    result=allocate(a,&req.memoryRequirements,request,ci->tiling==VK_IMAGE_TILING_LINEAR,dedicated.requiresDedicatedAllocation,VK_NULL_HANDLE,*image,allocation);
    const char *stage="allocate";
    if (result==VK_SUCCESS) {
        stage="bind";
        result=vkBindImageMemory(a->device,*image,(*allocation)->block->memory,(*allocation)->offset);
    }
    if (result!=VK_SUCCESS) {
        fprintf(stderr,"nv2a: Vulkan image allocation failed: stage=%s %ux%ux%u format=%u size=%llu alignment=%llu types=%x dedicated=%u result=%d\n",
            stage,ci->extent.width,ci->extent.height,ci->extent.depth,ci->format,
            (unsigned long long)req.memoryRequirements.size,
            (unsigned long long)req.memoryRequirements.alignment,
            req.memoryRequirements.memoryTypeBits,(unsigned)(request->dedicated||dedicated.requiresDedicatedAllocation),result);
        for (Block *b=a->blocks;b;b=b->next) if (!b->linear)
            fprintf(stderr,"nv2a: image pool type=%u capacity=%llu used=%llu max_free=%llu live=%u\n",
                b->type,(unsigned long long)b->size,(unsigned long long)b->used,
                (unsigned long long)b->max_free,b->live);
        vkDestroyImage(a->device,*image,NULL); *image=VK_NULL_HANDLE;
        allocation_free(a,*allocation); *allocation=NULL;
    }
    return result;
}

void xemu_vk_destroy_buffer(XemuVkAllocator a, VkBuffer buffer, XemuVkAllocation allocation)
{
    if (buffer) vkDestroyBuffer(a->device,buffer,NULL);
    allocation_free(a,allocation);
}
void xemu_vk_destroy_image(XemuVkAllocator a, VkImage image, XemuVkAllocation allocation)
{
    if (image) vkDestroyImage(a->device,image,NULL);
    allocation_free(a,allocation);
}
VkResult xemu_vk_map_memory(XemuVkAllocator a, XemuVkAllocation allocation, void **out)
{
    (void)a;
    *out=NULL;
    if (!allocation || !allocation->block->mapped) return VK_ERROR_MEMORY_MAP_FAILED;
    *out=(char*)allocation->block->mapped+allocation->offset;
    return VK_SUCCESS;
}

static VkResult cache_range(XemuVkAllocator a, XemuVkAllocation allocation,
    VkDeviceSize offset, VkDeviceSize size, bool invalidate)
{
    if (!size) return VK_SUCCESS;
    if (!allocation || !allocation->block->mapped || offset>allocation->size) return VK_ERROR_MEMORY_MAP_FAILED;
    if (size==VK_WHOLE_SIZE) size=allocation->size-offset;
    if (size>allocation->size-offset) return VK_ERROR_MEMORY_MAP_FAILED;
    if (!size) return VK_SUCCESS;
    Block *b=allocation->block;
    if (a->props.memoryTypes[b->type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) return VK_SUCCESS;
    VkDeviceSize begin=allocation->offset+offset, end;
    if (!align_size(begin+size,a->atom,&end) || end>b->size) end=b->size;
    begin&=~(a->atom-1);
    VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=b->memory,.offset=begin,.size=end-begin};
    return invalidate?vkInvalidateMappedMemoryRanges(a->device,1,&range):vkFlushMappedMemoryRanges(a->device,1,&range);
}
VkResult xemu_vk_flush_allocation(XemuVkAllocator a, XemuVkAllocation allocation, VkDeviceSize offset, VkDeviceSize size)
{ return cache_range(a,allocation,offset,size,false); }
VkResult xemu_vk_invalidate_allocation(XemuVkAllocator a, XemuVkAllocation allocation, VkDeviceSize offset, VkDeviceSize size)
{ return cache_range(a,allocation,offset,size,true); }
void xemu_vk_get_allocation_memory_properties(XemuVkAllocator a, XemuVkAllocation allocation, VkMemoryPropertyFlags *out)
{ *out=a->props.memoryTypes[allocation->block->type].propertyFlags; }
void xemu_vk_get_allocation_info(XemuVkAllocator a, XemuVkAllocation allocation, XemuVkAllocationInfo *out)
{ (void)a; *out=(XemuVkAllocationInfo){allocation->size,allocation->block->type}; }
void xemu_vk_get_memory_properties(XemuVkAllocator a, const VkPhysicalDeviceMemoryProperties **out)
{ *out=&a->props; }
void xemu_vk_get_heap_budgets(XemuVkAllocator a, XemuVkBudget *out)
{
    for (uint32_t i=0;i<a->props.memoryHeapCount;i++)
        out[i]=(XemuVkBudget){a->external_usage[i],a->budget[i]};
    /* Evict against live spans after reservation. Fixed spare backing cannot
     * be returned, so counting it as live would evict reusable images without
     * reducing pressure. Overflow blocks are returned when they become empty. */
    for (Block *b=a->blocks;b;b=b->next)
        out[a->props.memoryTypes[b->type].heapIndex].usage += a->sealed?b->used:b->size;
}
