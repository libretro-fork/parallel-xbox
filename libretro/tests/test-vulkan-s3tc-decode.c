/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"
#define VK_CHECK(x) assert((x)==VK_SUCCESS)
#define CHECK VK_CHECK
#define DIV_ROUND_UP(x,n) (((x)+(n)-1)/(n))
#define PGRAPH_VK_AUX_SLOTS 8
#define BUFFER_STAGING_SRC 0
#define BUFFER_UNSWIZZLE_OUT 1
#define PGRAPH_VK_RING_UNSWIZZLE 1
static VkDevice device;
static VkPhysicalDeviceMemoryProperties memory_properties;
static unsigned errors;
static unsigned timestamp_bits;
static unsigned memory_type(unsigned bits, VkMemoryPropertyFlags flags)
{
    if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        for (unsigned i=0;i<memory_properties.memoryTypeCount;i++) {
            VkMemoryPropertyFlags actual=memory_properties.memoryTypes[i].propertyFlags;
            if((bits&(1u<<i)) && (actual&flags)==flags && (actual&VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) return i;
        }
    }
    for (unsigned i = 0; i < memory_properties.memoryTypeCount; i++) {
        if ((bits & (1u << i)) &&
            (memory_properties.memoryTypes[i].propertyFlags & flags) == flags) return i;
    }
    abort();
}
typedef struct { VkBuffer handle; VkDeviceMemory memory; uint8_t *mapped; } Buffer;
static Buffer buffer(size_t bytes)
{
    Buffer b = {0};
    VkBufferCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bytes, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT };
    CHECK(vkCreateBuffer(device, &ci, NULL, &b.handle));
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(device, b.handle, &req);
    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size, .memoryTypeIndex = memory_type(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
    printf("Host buffer memory flags: 0x%x\n", memory_properties.memoryTypes[ai.memoryTypeIndex].propertyFlags);
    CHECK(vkAllocateMemory(device, &ai, NULL, &b.memory));
    CHECK(vkBindBufferMemory(device, b.handle, b.memory, 0));
    CHECK(vkMapMemory(device, b.memory, 0, VK_WHOLE_SIZE, 0, (void **)&b.mapped));
    return b;
}
static void drop_buffer(Buffer b)
{
    vkUnmapMemory(device, b.memory); vkDestroyBuffer(device, b.handle, NULL);
    vkFreeMemory(device, b.memory, NULL);
}
static VKAPI_ATTR VkBool32 VKAPI_CALL validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT *data, void *user)
{
    (void)type; (void)user;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        errors++; fprintf(stderr, "%s\n", data->pMessage);
    }
    return VK_FALSE;
}

#include <stddef.h>
#include "hw/xbox/nv2a/pgraph/s3tc.h"
#include "bc-reference.h"
#define MAX(a,b) ((a)>(b)?(a):(b))
#define ROUND_UP(a,b) (((a)+(b)-1)&~((b)-1))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
typedef struct {unsigned width,height,depth,levels,dimensionality,color_format;bool cubemap,border;} TextureShape;
typedef struct {VkImage image;VkImageCreateInfo image_ci;VkImageLayout current_layout;bool logo,s2t_borrowed;struct {TextureShape state;size_t texture_vram_offset,texture_length;} key;} TextureBinding;
typedef struct {VkBuffer buffer;VkDeviceSize buffer_size;uint8_t *mapped;} StorageBuffer;
typedef struct {
 VkDevice device;VkPhysicalDevice physical_device;VkPhysicalDeviceProperties device_props;VkPipelineCache vk_pipeline_cache;
 bool bc_native;VkBuffer vram_buffer;StorageBuffer storage_buffers[2];
 unsigned aux_slot;bool in_aux_command_buffer;struct {bool pending;} aux[PGRAPH_VK_AUX_SLOTS];
    struct {
        bool enabled;
        VkDescriptorSetLayout set_layout;
        VkPipelineLayout layout;
        VkPipeline pipelines[2]; /* 2D array layer, 3D */
        struct S3TCDecodeSlot {
            VkDescriptorPool pool;
            VkDescriptorSet sets[96];
            VkImageView views[96];
            unsigned used;
        } slots[PGRAPH_VK_AUX_SLOTS];
    } s3tc_decode;
} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;size_t src,written,source,face_stride;} PGRAPHState;
typedef struct {PGRAPHState pgraph;uint8_t *vram_ptr;size_t *vram;} NV2AState;
static size_t memory_region_size(size_t *p){return *p;}
static bool pgraph_is_texture_format_compressed(PGRAPHState *p,unsigned f){(void)p;return f>=1&&f<=3;}
static unsigned kelvin_format_to_s3tc_format(unsigned f){assert(f>=1&&f<=3);return f-1;}
static size_t pgraph_get_cubemap_face_length(PGRAPHState *p,TextureShape s){(void)s;return p->face_stride;}
static size_t pgraph_get_texture_phys_addr(PGRAPHState *p,int i){(void)p;(void)i;assert(!"upload must use binding metadata without resolving DMA again");return 0;}
static VkDeviceSize pgraph_vk_staging_alloc(PGRAPHState *pg,size_t size,size_t align,uint8_t **out)
{pg->src=ROUND_UP(pg->src,align);assert(pg->src+size<=pg->vk_renderer_state->storage_buffers[0].buffer_size);size_t off=pg->src;pg->src+=size;pg->written+=size;*out=pg->vk_renderer_state->storage_buffers[0].mapped+off;return off;}
static void pgraph_vk_staging_flush(PGRAPHState *p,VkDeviceSize off,size_t size){(void)p;(void)off;(void)size;}
static void pgraph_vk_wait_for_aux(PGRAPHState *p){(void)p;/* The test waits every fence before teardown. */}
static VkShaderModule pgraph_vk_create_fixed_shader_module_from_glsl(PGRAPHVkState *r,VkShaderStageFlagBits stage,const char *source)
{
 assert(stage==VK_SHADER_STAGE_COMPUTE_BIT);GlslcResult result;GlslcOptions opts={0};
 if(!glslc_compile(GLSLC_STAGE_COMPUTE,source,&opts,&result)){fprintf(stderr,"line %d: %s\n",result.error_line,result.error);abort();}
 VkShaderModuleCreateInfo ci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=result.num_words*4,.pCode=result.words};
 VkShaderModule module;CHECK(vkCreateShaderModule(r->device,&ci,NULL,&module));free(result.words);return module;
}

/* CPU PRODUCTION */
static VkBuffer test_ram_handle;
/* PRODUCTION */
typedef struct {
    TextureBinding binding;
    VkDeviceMemory memory;
    VkFence fence;
    uint8_t *expected;
    size_t bytes, output;
    unsigned slot;
    VkImageView view;
} Case;
static int exhaustive_channel=-1;
static unsigned cases;
static size_t pixels;
static Case record_case(PGRAPHVkState *r, PGRAPHState *pg, VkCommandBuffer cmd,
    VkQueue queue, Buffer dst, TextureShape shape, unsigned kind, unsigned skip,
    bool queued)
{
    Case c={0};c.slot=r->aux_slot;assert(!r->aux[c.slot].pending);
    TextureBinding *b=&c.binding;b->key.state=shape;
    b->image_ci=(VkImageCreateInfo){.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType=shape.dimensionality==3?VK_IMAGE_TYPE_3D:VK_IMAGE_TYPE_2D,
        .format=VK_FORMAT_R8G8B8A8_UNORM,.extent={shape.width,shape.height,shape.depth},
        .mipLevels=shape.levels,.arrayLayers=shape.cubemap?6:1,.samples=VK_SAMPLE_COUNT_1_BIT,
        .tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
        .flags=shape.cubemap?VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT:0};
    b->current_layout=VK_IMAGE_LAYOUT_UNDEFINED;assert(s3tc_storage_image(r,&b->image_ci));
    CHECK(vkCreateImage(device,&b->image_ci,NULL,&b->image));VkMemoryRequirements req;vkGetImageMemoryRequirements(device,b->image,&req);
    VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=memory_type(req.memoryTypeBits,0)};
    CHECK(vkAllocateMemory(device,&ai,NULL,&c.memory));CHECK(vkBindImageMemory(device,b->image,c.memory,0));
    NV2AState *nv=container_of(pg,NV2AState,pgraph);
    unsigned mode=shape.color_format-1,bs=mode?16:8;
    pg->source=512+(queued?c.slot*1024*1024:0)+((kind==1||kind==3)?0:1);
    b->key.texture_vram_offset=pg->source;
    pg->face_stride=1024*1024;
    b->key.texture_length=shape.cubemap?pg->face_stride*6:0;
    S3TCUploadLevel levels[96];unsigned n=s3tc_plan_levels(&shape,&b->image_ci,pg->source,pg->face_stride,skip,bs,levels);
    assert(n && n==(shape.cubemap?(6-__builtin_popcount(skip)):1)*shape.levels);
    uint32_t random=cases+77;
    for(unsigned j=0;j<n;j++) for(size_t k=0;k<levels[j].size;k++) {
        random=random*1664525u+1013904223u;nv->vram_ptr[levels[j].source+k]=random>>24;
    }
    if(exhaustive_channel>=0) {
        assert(n==1 && levels[0].width==1024 && levels[0].height==1024);
        unsigned ch=exhaustive_channel,shift=ch==0?11:ch==1?5:0,mask=ch==1?63:31;
        for(unsigned i=0;i<65536;i++) {
            uint8_t *block=nv->vram_ptr+levels[0].source+i*bs;
            memset(block,0,bs);unsigned off=mode?8:0,c0=(i&mask)<<shift,c1=((i/(mask+1))&mask)<<shift;
            block[off]=c0;block[off+1]=c0>>8;block[off+2]=c1;block[off+3]=c1>>8;memset(block+off+4,0xe4,4);
            if(mode==1)for(unsigned j=0;j<8;j++)block[j]=2*j|((2*j+1)<<4);
            if(mode==2){block[0]=i;block[1]=i>>8;uint64_t selectors=0;for(unsigned j=0;j<16;j++)selectors|=(uint64_t)(j%8)<<(3*j);for(unsigned j=0;j<6;j++)block[2+j]=selectors>>(8*j);}
        }
    }
    VkBufferImageCopy regions[96];unsigned nr=0;size_t bytes=0;
    for(unsigned layer=0;layer<b->image_ci.arrayLayers;layer++) for(unsigned mip=0;mip<shape.levels;mip++) {
        unsigned w=MAX(shape.width>>mip,1u),h=MAX(shape.height>>mip,1u),d=MAX(shape.depth>>mip,1u);
        regions[nr++]=(VkBufferImageCopy){.bufferOffset=bytes+16,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,mip,layer,1},.imageExtent={w,h,d}};
        bytes+=16+(size_t)w*h*d*4+16;
    }
    c.bytes=bytes;c.output=queued?c.slot*1024*1024:0;c.expected=malloc(bytes);assert(c.expected);
    memset(c.expected,0xAB,bytes);memset(dst.mapped+c.output,0xAB,bytes);
    for(unsigned j=0;j<nr;j++) {
        VkBufferImageCopy *region=&regions[j];unsigned w=region->imageExtent.width,h=region->imageExtent.height,d=region->imageExtent.depth;
        uint8_t *out=c.expected+region->bufferOffset;
        if(skip&(1u<<region->imageSubresource.baseArrayLayer)) memset(out,0,(size_t)w*h*d*4);
        else {
            S3TCUploadLevel *l=NULL;
            for(unsigned k=0;k<n;k++) if(levels[k].layer==region->imageSubresource.baseArrayLayer&&levels[k].mip==region->imageSubresource.mipLevel)l=&levels[k];
            assert(l);unsigned block=0;
            for(unsigned z0=0;z0<l->depth;z0+=4) for(unsigned by=0;by<(l->height+3)/4;by++) for(unsigned bx=0;bx<(l->width+3)/4;bx++)
            for(unsigned z=z0;z<z0+4&&z<l->depth;z++) {
                const uint8_t *input=nv->vram_ptr+l->source+block++*bs;
                for(unsigned yy=0;yy<4;yy++) for(unsigned xx=0;xx<4;xx++) {
                    unsigned x=bx*4+xx,y=by*4+yy;
                    if(x>=l->crop&&y>=l->crop&&x-l->crop<w&&y-l->crop<h)
                        reference_pixel(input,mode,yy*4+xx,out+(((size_t)z*h+y-l->crop)*w+x-l->crop)*4);
                }
            }
        }
        pixels+=(size_t)w*h*d;region->bufferOffset+=c.output;
    }
    pg->src=queued?c.slot*1024*1024+32:32;pg->written=0;
    r->vram_buffer=kind?test_ram_handle:VK_NULL_HANDLE;
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cmd,&begin));
    if(skip) {
        VkImageMemoryBarrier initial={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=b->image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,shape.levels,0,b->image_ci.arrayLayers}};
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&initial);
        VkClearColorValue zero={0};vkCmdClearColorImage(cmd,b->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&zero,1,&initial.subresourceRange);
        b->current_layout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }
    if(kind==3) for(unsigned j=0;j<n;j++) {
        assert(levels[j].size<=65536);
        vkCmdUpdateBuffer(cmd,test_ram_handle,levels[j].source,levels[j].size,nv->vram_ptr+levels[j].source);
    }
    if((size_t)shape.width*shape.height*shape.depth>=65536) assert(upload_s3tc_image_gpu(pg,cmd,b,skip));
    else assert(s3tc_decode_levels(pg,cmd,b,levels,n,mode));
    size_t packed=0;for(unsigned j=0;j<n;j++)packed+=ROUND_UP(levels[j].size,16);
    assert(pg->written==((kind==1||kind==3)?0:packed));
    if(kind!=1)for(unsigned j=0;j<n;j++)memset(nv->vram_ptr+levels[j].source,0xEE,levels[j].size);
    c.view=r->s3tc_decode.slots[c.slot].views[0];
    VkImageMemoryBarrier image={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.oldLayout=b->current_layout,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=b->image,
        .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,shape.levels,0,b->image_ci.arrayLayers}};
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&image);
    vkCmdCopyImageToBuffer(cmd,b->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,dst.handle,nr,regions);
    VkBufferMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=dst.handle,.offset=c.output,.size=bytes};
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&host,0,NULL);
    CHECK(vkEndCommandBuffer(cmd));VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};CHECK(vkCreateFence(device,&fi,NULL,&c.fence));
    VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};CHECK(vkQueueSubmit(queue,1,&submit,c.fence));r->aux[c.slot].pending=true;
    cases++;return c;
}
static void complete_case(PGRAPHVkState *r,Case *c,Buffer dst,VkCommandBuffer cmd)
{
    CHECK(vkWaitForFences(device,1,&c->fence,VK_TRUE,10000000000ULL));r->aux[c->slot].pending=false;
    assert(r->s3tc_decode.slots[c->slot].views[0]==c->view);
    if(memcmp(dst.mapped+c->output,c->expected,c->bytes)){fprintf(stderr,"case slot %u mismatched\n",c->slot);abort();}
    vkDestroyImage(device,c->binding.image,NULL);vkFreeMemory(device,c->memory,NULL);vkDestroyFence(device,c->fence,NULL);free(c->expected);CHECK(vkResetCommandBuffer(cmd,0));
}

static void check_rejections(PGRAPHVkState *r,PGRAPHState *pg,VkCommandBuffer cmd)
{
    TextureBinding b={.image_ci={.format=VK_FORMAT_R8G8B8A8_UNORM,.usage=VK_IMAGE_USAGE_STORAGE_BIT}};
    S3TCUploadLevel l={.source=512,.size=16,.width=4,.height=4,.depth=1,.out_width=4,.out_height=4};
    pg->written=0;pg->src=17;unsigned used=r->s3tc_decode.slots[r->aux_slot].used;
    assert(!s3tc_decode_levels(pg,cmd,&b,&l,0,2));assert(!s3tc_decode_levels(pg,cmd,&b,&l,97,2));
    r->s3tc_decode.enabled=false;assert(!s3tc_decode_levels(pg,cmd,&b,&l,1,2));assert(!s3tc_storage_image(r,&b.image_ci));r->s3tc_decode.enabled=true;
    b.s2t_borrowed=true;assert(!s3tc_decode_levels(pg,cmd,&b,&l,1,2));b.s2t_borrowed=false;
    b.image_ci.usage=0;assert(!s3tc_decode_levels(pg,cmd,&b,&l,1,2));b.image_ci.usage=VK_IMAGE_USAGE_STORAGE_BIT;
    b.image_ci.format=VK_FORMAT_R8_UNORM;assert(!s3tc_decode_levels(pg,cmd,&b,&l,1,2));b.image_ci.format=VK_FORMAT_R8G8B8A8_UNORM;
    NV2AState *nv=container_of(pg,NV2AState,pgraph);l.source=*nv->vram;assert(!s3tc_decode_levels(pg,cmd,&b,&l,1,2));l.source=512;
    uint32_t limit=r->device_props.limits.maxStorageBufferRange;r->device_props.limits.maxStorageBufferRange=8;assert(!s3tc_decode_levels(pg,cmd,&b,&l,1,2));r->device_props.limits.maxStorageBufferRange=limit;
    uint32_t z=r->device_props.limits.maxComputeWorkGroupCount[2];r->device_props.limits.maxComputeWorkGroupCount[2]=0;assert(!s3tc_decode_levels(pg,cmd,&b,&l,1,2));r->device_props.limits.maxComputeWorkGroupCount[2]=z;
    VkDeviceSize size=r->storage_buffers[0].buffer_size;r->storage_buffers[0].buffer_size=16;r->vram_buffer=VK_NULL_HANDLE;assert(!s3tc_decode_levels(pg,cmd,&b,&l,1,2));r->storage_buffers[0].buffer_size=size;
    b.key.state=(TextureShape){.width=4,.height=4,.depth=1,.levels=1,.dimensionality=3,.color_format=3};
    assert(!upload_s3tc_image_gpu(pg,cmd,&b,0));r->bc_native=true;assert(!upload_s3tc_image_gpu(pg,cmd,&b,0));r->bc_native=false;
    assert(!pg->written&&pg->src==17&&r->s3tc_decode.slots[r->aux_slot].used==used);
    puts("S3TC preflight: disabled/native/small, format/usage, source bounds, storage/dispatch/capacity rejection leave staging and descriptors untouched");
}

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
static double seconds(void)
{
#ifdef _WIN32
    LARGE_INTEGER f,t;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&t);return(double)t.QuadPart/f.QuadPart;
#else
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;
#endif
}
static void benchmark(PGRAPHVkState *r,PGRAPHState *pg,VkCommandBuffer *commands,VkQueue queue)
{
    if(!timestamp_bits){puts("Benchmark skipped: queue has no timestamps");return;}
    NV2AState *nv=container_of(pg,NV2AState,pgraph);
    uint8_t *original=nv->vram_ptr; nv->vram_ptr=malloc(*nv->vram); assert(nv->vram_ptr);
    unsigned widths[]={32,64,128,256};
    VkQueryPoolCreateInfo qci={.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,.queryType=VK_QUERY_TYPE_TIMESTAMP,.queryCount=2};
    VkQueryPool queries;CHECK(vkCreateQueryPool(device,&qci,NULL,&queries));
    for(unsigned chain=0;chain<2;chain++)for(unsigned wi=0;wi<4;wi++)for(unsigned format=0;format<3;format++) {
        unsigned w=widths[wi],h=w,d=8,bs=format?16:8;
        TextureBinding binding[3]={0};VkDeviceMemory memory[3];S3TCUploadLevel levels[96];
        TextureShape shape={.width=w,.height=h,.depth=d,.levels=chain?4:1,.dimensionality=3,.color_format=format+1};
        for(unsigned path=0;path<3;path++) {
            binding[path].key.state=shape;
            binding[path].image_ci=(VkImageCreateInfo){.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_3D,
                .format=VK_FORMAT_R8G8B8A8_UNORM,.extent={w,h,d},.mipLevels=shape.levels,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,
                .tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|(path?VK_IMAGE_USAGE_STORAGE_BIT:0)};
            CHECK(vkCreateImage(device,&binding[path].image_ci,NULL,&binding[path].image));VkMemoryRequirements req;vkGetImageMemoryRequirements(device,binding[path].image,&req);
            VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=memory_type(req.memoryTypeBits,0)};
            CHECK(vkAllocateMemory(device,&ai,NULL,&memory[path]));CHECK(vkBindImageMemory(device,binding[path].image,memory[path],0));
            binding[path].current_layout=VK_IMAGE_LAYOUT_UNDEFINED;
        }
        pg->source=512;unsigned count=s3tc_plan_levels(&shape,&binding[1].image_ci,pg->source,0,0,bs,levels);assert(count==shape.levels);
        size_t input_size=0;for(unsigned j=0;j<count;j++)input_size+=levels[j].size;
        unsigned seed=57;for(size_t j=0;j<input_size;j++){seed=seed*1664525u+1013904223u;nv->vram_ptr[pg->source+j]=seed>>24;}
        /* Stable cached-host buffer models imported VRAM reads; no timed shadow copy. */
        memcpy(original+pg->source,nv->vram_ptr+pg->source,input_size);
        double wall[3]={0},gpu[3]={0};unsigned n=12;
        for(unsigned round=0;round<9;round++)for(unsigned k=0;k<3;k++) {
            unsigned path=(k+round)%3;
            for(unsigned iteration=0;iteration<n;iteration++) {
                r->aux_slot=(round*n+iteration)%8;VkCommandBuffer cmd=commands[r->aux_slot];
                pg->src=32;pg->written=0;r->vram_buffer=path==2?test_ram_handle:VK_NULL_HANDLE;
                double start=seconds();
                VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cmd,&begin));
                vkCmdResetQueryPool(cmd,queries,0,2);vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,queries,0);
                if(path) assert(s3tc_decode_levels(pg,cmd,&binding[path],levels,count,format));
                else {
                    TextureLevel level[16];VkBufferImageCopy regions[16];
                    for(unsigned m=0;m<count;m++) {
                        S3TCUploadLevel *l=&levels[m];
                        decode_level_3d(pg,&level[m],format,nv->vram_ptr+l->source,l->width,l->height,l->depth);
                        regions[m]=(VkBufferImageCopy){.bufferOffset=level[m].offset,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,m,0,1},.imageExtent={l->width,l->height,l->depth}};
                    }
                    size_t decoded_size=level[count-1].offset+level[count-1].size-level[0].offset;
                    pgraph_vk_staging_flush(pg,level[0].offset,decoded_size);
                    VkBufferMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,
                        .dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
                        .buffer=r->storage_buffers[0].buffer,.offset=level[0].offset,.size=decoded_size};
                    VkImageMemoryBarrier image={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=binding[0].current_layout?VK_ACCESS_MEMORY_READ_BIT:0,
                        .dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.oldLayout=binding[0].current_layout,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=binding[0].image,
                        .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,shape.levels,0,1}};
                    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,1,&host,1,&image);
                    vkCmdCopyBufferToImage(cmd,r->storage_buffers[0].buffer,binding[0].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,count,regions);
                    image.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;image.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
                    image.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;image.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,NULL,0,NULL,1,&image);
                    binding[0].current_layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                }
                vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,queries,1);CHECK(vkEndCommandBuffer(cmd));
                VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;CHECK(vkCreateFence(device,&fi,NULL,&fence));
                VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};CHECK(vkQueueSubmit(queue,1,&submit,fence));
                CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ULL));double end=seconds();
                uint64_t timestamps[2];CHECK(vkGetQueryPoolResults(device,queries,0,2,sizeof(timestamps),timestamps,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT));
                uint64_t ticks=timestamps[1]-timestamps[0];if(timestamp_bits<64)ticks&=(UINT64_C(1)<<timestamp_bits)-1;
                if(round){wall[path]+=end-start;gpu[path]+=ticks*r->device_props.limits.timestampPeriod/1e9;}
                vkDestroyFence(device,fence,NULL);CHECK(vkResetCommandBuffer(cmd,0));
            }
        }
        printf("bench width=%u depth=%u levels=%u format=%u cpu_us=%.3f snapshot_us=%.3f direct_us=%.3f cpu_gpu_us=%.3f snapshot_gpu_us=%.3f direct_gpu_us=%.3f\n",w,d,shape.levels,format,
            wall[0]*1e6/(8*n),wall[1]*1e6/(8*n),wall[2]*1e6/(8*n),gpu[0]*1e6/(8*n),gpu[1]*1e6/(8*n),gpu[2]*1e6/(8*n));
        for(unsigned path=0;path<3;path++){vkDestroyImage(device,binding[path].image,NULL);vkFreeMemory(device,memory[path],NULL);}
    }
    vkDestroyQueryPool(device,queries,NULL);
    free(nv->vram_ptr); nv->vram_ptr=original;
}
int main(void)
{
    const char *layers[] = { "VK_LAYER_KHRONOS_validation" };
    const char *extensions[] = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME };
    VkValidationFeatureEnableEXT enabled = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT validation_features = { .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .enabledValidationFeatureCount = 1, .pEnabledValidationFeatures = &enabled };
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_2 };
    bool validation_enabled = !getenv("VK_S3TC_NO_VALIDATION");
    VkInstanceCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = validation_enabled ? &validation_features : NULL, .pApplicationInfo = &app, .enabledLayerCount = validation_enabled ? 1 : 0,
        .ppEnabledLayerNames = layers, .enabledExtensionCount = validation_enabled ? 2 : 0, .ppEnabledExtensionNames = extensions };
    VkInstance instance; CHECK(vkCreateInstance(&ci, NULL, &instance));
    PFN_vkCreateDebugUtilsMessengerEXT create = (void *)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    PFN_vkDestroyDebugUtilsMessengerEXT destroy = (void *)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    VkDebugUtilsMessengerCreateInfoEXT mi = { .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT, .messageType = 7,
        .pfnUserCallback = validation };
    VkDebugUtilsMessengerEXT messenger=VK_NULL_HANDLE; if(validation_enabled) CHECK(create(instance, &mi, NULL, &messenger));
    uint32_t n=0;CHECK(vkEnumeratePhysicalDevices(instance,&n,NULL));assert(n);
    VkPhysicalDevice *devices=malloc(n*sizeof(*devices));assert(devices);CHECK(vkEnumeratePhysicalDevices(instance,&n,devices));
    VkPhysicalDevice gpu=devices[0];free(devices);
    vkGetPhysicalDeviceMemoryProperties(gpu, &memory_properties);
    VkPhysicalDeviceProperties properties; vkGetPhysicalDeviceProperties(gpu, &properties);
    printf("S3TC Vulkan oracle: %s; validation=%d\n", properties.deviceName, validation_enabled);
    uint32_t nq = 32; VkQueueFamilyProperties queues[32]; vkGetPhysicalDeviceQueueFamilyProperties(gpu, &nq, queues);
    unsigned family = 0; while (family < nq && (queues[family].queueFlags & (VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT)) != (VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT)) family++;
    assert(family < nq);timestamp_bits=queues[family].timestampValidBits;
    float priority = 1;
    VkDeviceQueueCreateInfo qi = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority };
    VkDeviceCreateInfo di = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi };
    CHECK(vkCreateDevice(gpu, &di, NULL, &device));
    VkQueue queue; vkGetDeviceQueue(device, family, 0, &queue);
    VkCommandPoolCreateInfo pi = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = family };
    VkCommandPool pool; CHECK(vkCreateCommandPool(device, &pi, NULL, &pool));
    VkCommandBufferAllocateInfo ca = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 8 };
    VkCommandBuffer commands[8]; CHECK(vkAllocateCommandBuffers(device, &ca, commands));



 PGRAPHVkState r={.in_aux_command_buffer=true,.physical_device=gpu,.device=device,.device_props=properties};
 r.s3tc_decode.enabled=true;
 size_t capacity=16*1024*1024;
 Buffer ram=buffer(capacity),src=buffer(capacity),dst=buffer(capacity);test_ram_handle=ram.handle;
 r.storage_buffers[0]=(StorageBuffer){src.handle,capacity,src.mapped};
 NV2AState nv={.pgraph={.vk_renderer_state=&r},.vram_ptr=ram.mapped,.vram=&capacity};PGRAPHState *pg=&nv.pgraph;
 unsigned widths[]={1,3,7,16,64},heights[]={1,5,8,64},depths[]={1,3,4,5,8};
 for(unsigned mode=0;mode<3;mode++) for(unsigned kind=0;kind<3;kind++)
 for(unsigned wi=0;wi<5;wi++) for(unsigned hi=0;hi<4;hi++) for(unsigned zi=0;zi<5;zi++) {
  TextureShape shape={.width=widths[wi],.height=heights[hi],.depth=depths[zi],.levels=wi>=2&&hi>=1&&zi>=1?3:1,.dimensionality=3,.color_format=mode+1};
  r.aux_slot=cases%8;Case c=record_case(&r,pg,commands[r.aux_slot],queue,dst,shape,kind,0,false);complete_case(&r,&c,dst,commands[r.aux_slot]);
 }
 for(unsigned mode=0;mode<3;mode++)for(unsigned kind=0;kind<3;kind++)for(unsigned cube=0;cube<2;cube++)for(unsigned border=0;border<=cube;border++)for(unsigned mask=0;mask<2;mask++) {
  TextureShape shape={.width=64,.height=64,.depth=1,.levels=7,.dimensionality=2,.color_format=mode+1,.cubemap=cube,.border=border};
  unsigned skip=cube&&mask?0x25:0;
  r.aux_slot=cases%8;Case c=record_case(&r,pg,commands[r.aux_slot],queue,dst,shape,kind,skip,false);complete_case(&r,&c,dst,commands[r.aux_slot]);
 }
 for(unsigned kind=0;kind<3;kind++) {
  Case batch[8];TextureShape shape={.width=32,.height=16,.depth=5,.levels=3,.dimensionality=3,.color_format=3};
  for(unsigned j=0;j<8;j++){r.aux_slot=j;batch[j]=record_case(&r,pg,commands[j],queue,dst,shape,kind,0,true);}
  for(unsigned j=0;j<8;j++)complete_case(&r,&batch[j],dst,commands[j]);
 }
 for(unsigned mode=0;mode<3;mode++) {
  TextureShape shape={.width=64,.height=32,.depth=5,.levels=3,.dimensionality=3,.color_format=mode+1};
  r.aux_slot=cases%8;Case c=record_case(&r,pg,commands[r.aux_slot],queue,dst,shape,3,0,false);complete_case(&r,&c,dst,commands[r.aux_slot]);
 }
 for(unsigned mode=0;mode<3;mode++)for(unsigned kind=0;kind<3;kind++) {
  TextureShape shape={.width=256,.height=256,.depth=8,.levels=1,.dimensionality=3,.color_format=mode+1};
  r.aux_slot=cases%8;Case c=record_case(&r,pg,commands[r.aux_slot],queue,dst,shape,kind,0,false);complete_case(&r,&c,dst,commands[r.aux_slot]);
 }
 for(unsigned mode=0;mode<3;mode++)for(unsigned kind=0;kind<3;kind++)for(unsigned border=0;border<2;border++)for(unsigned mask=0;mask<2;mask++) {
  TextureShape shape={.width=256,.height=256,.depth=1,.levels=9,.dimensionality=2,.color_format=mode+1,.cubemap=true,.border=border};
  r.aux_slot=cases%8;Case c=record_case(&r,pg,commands[r.aux_slot],queue,dst,shape,kind,mask?0x25:0,false);complete_case(&r,&c,dst,commands[r.aux_slot]);
 }
 for(unsigned mode=0;mode<3;mode++)for(int ch=0;ch<3;ch++) {
  exhaustive_channel=ch;TextureShape shape={.width=1024,.height=1024,.depth=1,.levels=1,.dimensionality=2,.color_format=mode+1};
  r.aux_slot=cases%8;Case c=record_case(&r,pg,commands[r.aux_slot],queue,dst,shape,1,0,false);complete_case(&r,&c,dst,commands[r.aux_slot]);
 }
 exhaustive_channel=-1;check_rejections(&r,pg,commands[0]);
 if(getenv("VK_S3TC_BENCH"))benchmark(&r,pg,commands,queue);
 s3tc_decode_finalize(pg);drop_buffer(ram);drop_buffer(src);drop_buffer(dst);vkDestroyCommandPool(device,pool,NULL);vkDestroyDevice(device,NULL);if(messenger)destroy(instance,messenger,NULL);vkDestroyInstance(instance,NULL);
 printf("S3TC Vulkan: %u cases, %zu exact pixels, snapshot/direct/unaligned, mips/cubes/borders/skips, eight queued lifetimes; validation_errors=%u\n",cases,pixels,errors);return errors?1:0;
}
