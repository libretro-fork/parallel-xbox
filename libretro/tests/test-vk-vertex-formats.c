/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VK_NO_PROTOTYPES
#include "hw/xbox/nv2a/pgraph/vk/vertex-format.h"
#include "hw/xbox/nv2a/nv2a_regs.h"
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define ROUND_UP(x,a) (((x)+(a)-1)&~((uint64_t)(a)-1))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define NV2A_VERTEXSHADER_ATTRIBUTES 16
#define BUFFER_VERTEX_INLINE_STAGING 0
#define nv2a_profile_inc_counter(...) ((void)0)
#define NV2A_VK_DGROUP_BEGIN(...) ((void)0)
#define NV2A_VK_DGROUP_END() ((void)0)
#define NV2A_VK_DPRINTF(...) ((void)0)
typedef uint64_t hwaddr;
typedef struct {
    unsigned format,count,size,stride,inline_array_offset;hwaddr offset;bool dma_select;
    float inline_value[4];
} VertexAttribute;
typedef struct { uint8_t *mapped;VkDeviceSize buffer_offset,buffer_size; } StorageBuffer;
typedef struct {
    VkPhysicalDevice physical_device;
    uint8_t vertex_format_conversion[VK_FORMAT_R32G32B32A32_SFLOAT+1];
    bool vertex_conversion_required;
    VkVertexInputAttributeDescription vertex_attribute_descriptions[16];
    VkVertexInputBindingDescription vertex_binding_descriptions[16];
    int vertex_attribute_to_description_location[16];
    int num_active_vertex_attribute_descriptions,num_active_vertex_binding_descriptions;
    hwaddr vertex_attribute_offsets[16];
    bool vertex_attr_cow[16];struct {hwaddr addr,size;} vertex_attr_span[16];
    StorageBuffer storage_buffers[1];
} PGRAPHVkState;
typedef struct {
    PGRAPHVkState *vk_renderer_state;VertexAttribute vertex_attributes[16];
    uint16_t compressed_attrs,uniform_attrs,swizzle_attrs;
    hwaddr dma_vertex_a,dma_vertex_b;uint32_t inline_array[4096];
} PGRAPHState;
typedef struct { PGRAPHState pgraph;uint8_t *vram_ptr; } NV2AState;
typedef struct { int failed; } Error;
static unsigned queries;
static bool supported[256],rotate;
static uint8_t ram[65536],stage[65536],alternate[65536],gold[65536];
static void vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice gpu,VkFormat format,VkFormatProperties *out) {
    assert(format<256);queries++;memset(out,0,sizeof(*out));
    if(supported[format]) out->bufferFeatures=VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT;
}
static void error_setg(Error **error,const char *format,unsigned value) {
    static Error failure;failure.failed=1;*error=&failure;
}
static uint8_t *nv_dma_map(NV2AState *d,hwaddr addr,hwaddr *size) { *size=sizeof(ram);return d->vram_ptr; }
static hwaddr pgraph_vertex_fetch_span(unsigned count,size_t stride,size_t width) {
    return count?(hwaddr)(count-1)*stride+width:0;
}
static void update_memory_buffer(NV2AState *d,int index,hwaddr addr,hwaddr size) { assert(false); }
static bool ensure_buffer_space(PGRAPHState *pg,int index,VkDeviceSize size) {
    assert(index==0 && size<=pg->vk_renderer_state->storage_buffers[0].buffer_size);return false;
}
static bool pgraph_vk_buffer_has_space_for(PGRAPHState *pg,int index,VkDeviceSize size,unsigned alignment) {
    StorageBuffer *b=&pg->vk_renderer_state->storage_buffers[0];return b->buffer_offset+size<=b->buffer_size;
}
static void begin_pre_draw(PGRAPHState *pg) {
    PGRAPHVkState *r=pg->vk_renderer_state;
    for(int j=0;j<r->num_active_vertex_attribute_descriptions;j++)
        assert(supported[r->vertex_attribute_descriptions[j].format]);
    if(rotate) { r->storage_buffers[0].mapped=alternate;r->storage_buffers[0].buffer_offset=48; }
}
static VkDeviceSize pgraph_vk_update_vertex_inline_buffer(PGRAPHState *pg,void **data,VkDeviceSize *sizes,size_t count) {
    StorageBuffer *b=&pg->vk_renderer_state->storage_buffers[0];VkDeviceSize offset=b->buffer_offset;
    for(size_t j=0;j<count;j++) { memcpy(b->mapped+b->buffer_offset,data[j],sizes[j]);b->buffer_offset+=sizes[j]; }
    return offset;
}
/* TYPES */
/* PRODUCTION */
static uint32_t seed=0x83afe1c2;
static uint32_t random_value(void) { seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed; }
/* Independent byte oracle: no typed/unaligned reads and no production
 * conversion dispatch. The instruction's format determines the result. */
static unsigned oracle(uint8_t *out,const uint8_t *in,VkFormat format) {
    if(format==VK_FORMAT_R8G8B8_UNORM) { for(unsigned k=0;k<3;k++) out[k]=in[k];out[3]=255;return 4; }
    if(format==VK_FORMAT_R16G16B16_SNORM) { for(unsigned k=0;k<6;k++) out[k]=in[k];out[6]=255;out[7]=127;return 8; }
    unsigned n=format==VK_FORMAT_R16_SSCALED?1:format==VK_FORMAT_R16G16_SSCALED?2:
               format==VK_FORMAT_R16G16B16_SSCALED?3:4;
    for(unsigned k=0;k<n;k++) {
        int value=(unsigned)in[k*2]|((unsigned)in[k*2+1]<<8);
        if(value>=32768) value-=65536;
        float f=(float)value;memcpy(out+k*4,&f,4);
    }
    return n*4;
}
static void reset(PGRAPHVkState *r,NV2AState *d) {
    memset(r,0,sizeof(*r));memset(d,0,sizeof(*d));d->pgraph.vk_renderer_state=r;d->vram_ptr=ram;
    for(unsigned j=0;j<16;j++) r->vertex_attribute_to_description_location[j]=-1;
    r->storage_buffers[0]=(StorageBuffer){stage,32,sizeof(stage)};
    memset(stage,0xcc,sizeof(stage));memset(alternate,0xcc,sizeof(alternate));memset(gold,0xcc,sizeof(gold));
}
int main(void) {
    const VkFormat formats[]={VK_FORMAT_R8G8B8_UNORM,VK_FORMAT_R16G16B16_SNORM,VK_FORMAT_R16_SSCALED,
        VK_FORMAT_R16G16_SSCALED,VK_FORMAT_R16G16B16_SSCALED,VK_FORMAT_R16G16B16A16_SSCALED};
    assert(sizeof(VertexBufferRemap)==400);
    for(unsigned j=0;j<sizeof(ram);j++) ram[j]=(uint8_t)random_value();
    unsigned cases=0;
    for(unsigned f=0;f<6;f++) for(unsigned stride_pad=0;stride_pad<6;stride_pad++)
    for(unsigned base=1;base<=8;base++) for(unsigned start=0;start<5;start++) for(unsigned count=0;count<9;count++) {
        PGRAPHVkState r;NV2AState d;reset(&r,&d);memset(supported,1,sizeof(supported));
        supported[formats[f]]=false;queries=0;Error *err=NULL;pgraph_vk_init_vertex_formats(&d.pgraph,&err);
        assert(!err && r.vertex_conversion_required && queries==18);unsigned before=queries;
        size_t unit,n;get_size_and_count_for_format(formats[f],&unit,&n);
        unsigned width=unit*n,stride=width+stride_pad,end=start+count;
        r.vertex_attribute_to_description_location[0]=0;
        r.vertex_attribute_descriptions[0]=(VkVertexInputAttributeDescription){.format=formats[f]};
        r.vertex_binding_descriptions[0].stride=stride;r.vertex_attribute_offsets[0]=base;
        r.num_active_vertex_attribute_descriptions=r.num_active_vertex_binding_descriptions=1;
        VertexBufferRemap remap=remap_unaligned_attributes(&d.pgraph,end);
        assert(remap.attributes==1 && remap.map[0].read_size==width);
        enum VertexConvert kind;VkFormat fallback=pgraph_vk_vertex_substitute_format(formats[f],&kind);
        assert(r.vertex_attribute_descriptions[0].format==fallback);
        assert(r.vertex_binding_descriptions[0].stride==remap.map[0].new_stride && remap.map[0].convert==kind);
        for(unsigned v=start;v<end;v++) oracle(gold+32+v*remap.map[0].new_stride,ram+base+v*stride,formats[f]);
        copy_remapped_attributes_to_inline_buffer(&d.pgraph,&remap,start,end);
        assert(!memcmp(stage,gold,sizeof(stage)) && queries==before);
        assert(r.vertex_attribute_offsets[0]==32);
        /* A new renderer/device does not inherit the previous fallback. */
        memset(supported,1,sizeof(supported));err=NULL;pgraph_vk_init_vertex_formats(&d.pgraph,&err);
        assert(!err && !r.vertex_conversion_required && !r.vertex_format_conversion[formats[f]]);
        cases++;
    }
    /* Inline layouts retain the NV2A's four-byte footprint for each source
     * attribute, including three-byte and six-byte values. */
    for(unsigned converted=0;converted<2;converted++) for(unsigned components=1;components<=4;components++)
    for(unsigned count=1;count<=17;count++) for(unsigned rotated=0;rotated<2;rotated++) {
        PGRAPHVkState r;NV2AState d;reset(&r,&d);PGRAPHState *pg=&d.pgraph;
        memset(supported,1,sizeof(supported));if(converted) for(unsigned f=0;f<6;f++) supported[formats[f]]=false;
        Error *err=NULL;pgraph_vk_init_vertex_formats(pg,&err);assert(!err);
        pg->vertex_attributes[0]=(VertexAttribute){.format=NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL,.count=3,.size=1};
        pg->vertex_attributes[1]=(VertexAttribute){.format=NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S1,.count=3,.size=2};
        pg->vertex_attributes[2]=(VertexAttribute){.format=NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S32K,.count=components,.size=2};
        pg->vertex_attributes[3]=(VertexAttribute){.format=NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F,.count=4,.size=4};
        unsigned stride=0;for(unsigned j=0;j<4;j++) {
            pg->vertex_attributes[j].inline_array_offset=stride;stride+=pgraph_inline_attr_size(&pg->vertex_attributes[j]);
        }
        memcpy(pg->inline_array,ram,(size_t)stride*count);
        pgraph_vk_bind_vertex_attributes(&d,0,count-1,true,stride,count-1);
        float provoking[4][4];for(unsigned j=0;j<4;j++) memcpy(provoking[j],pg->vertex_attributes[j].inline_value,16);
        VkFormat source_formats[4];unsigned source_offsets[4];
        for(unsigned j=0;j<4;j++) { source_formats[j]=r.vertex_attribute_descriptions[j].format;source_offsets[j]=pg->vertex_attributes[j].inline_array_offset; }
        unsigned before=queries;rotate=rotated;
        VkDeviceSize offset=upload_converted_inline_array(pg,(VkDeviceSize)stride*count,stride,count);
        assert(offset==(rotated?48:32) && queries==before);
        unsigned output_stride=r.vertex_binding_descriptions[0].stride;
        for(unsigned v=0;v<count;v++) for(unsigned j=0;j<4;j++) {
            const uint8_t *in=(const uint8_t *)pg->inline_array+v*stride+source_offsets[j];
            uint8_t *out=gold+offset+v*output_stride+r.vertex_attribute_offsets[j];
            if(converted && j<3) oracle(out,in,source_formats[j]);
            else {
                size_t unit,n;get_size_and_count_for_format(source_formats[j],&unit,&n);
                memcpy(out,in,unit*n);
            }
        }
        uint8_t *actual=rotated?alternate:stage;
        if(!converted) memcpy(gold+offset,pg->inline_array,(size_t)stride*count);
        assert(!memcmp(actual,gold,sizeof(gold)));
        for(unsigned j=0;j<4;j++) {
            assert(pg->vertex_attributes[j].inline_array_offset==source_offsets[j]);
            assert(!memcmp(provoking[j],pg->vertex_attributes[j].inline_value,16));
        }
        cases++;
    }
    PGRAPHVkState r;NV2AState d;reset(&r,&d);memset(supported,1,sizeof(supported));
    supported[VK_FORMAT_R32_SFLOAT]=false;Error *err=NULL;pgraph_vk_init_vertex_formats(&d.pgraph,&err);assert(err);
    memset(supported,1,sizeof(supported));supported[VK_FORMAT_R16_SSCALED]=supported[VK_FORMAT_R32_SFLOAT]=false;
    err=NULL;pgraph_vk_init_vertex_formats(&d.pgraph,&err);assert(err);
    memset(supported,1,sizeof(supported));supported[VK_FORMAT_R16G16B16_SNORM]=supported[VK_FORMAT_R16G16B16A16_SNORM]=false;
    queries=0;err=NULL;pgraph_vk_init_vertex_formats(&d.pgraph,&err);assert(err && queries==12);
    int16_t *all=malloc(65536*sizeof(*all));float *converted=malloc(65536*sizeof(*converted));assert(all && converted);
    for(unsigned i=0;i<65536;i++) all[i]=(int16_t)((int)i-32768);
    pgraph_vk_convert_vertex_attribute((uint8_t *)converted,(const uint8_t *)all,65536,2,4,2,VTX_CONVERT_S16_TO_F32);
    for(unsigned i=0;i<65536;i++) assert(converted[i]==(float)((int)i-32768));
    free(all);free(converted);
    printf("vertex formats: %u array/inline cases, startup-only queries, missing capabilities, rotation and provoking-value preservation passed\n",cases);
    puts("all 65536 signed 16-bit values convert exactly to float");
}
