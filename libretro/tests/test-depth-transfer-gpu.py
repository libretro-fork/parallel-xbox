# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare production depth-to-texture pack dependencies with readback packing."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
base=repo/'hw/xbox/nv2a/pgraph/vk'
def function(source,name):
    m=re.search(r'^(?:static )?(?:void|VkBuffer|VkDeviceSize) '+name+r'\(',source,re.M)
    assert m,name
    end=source.index('{',m.start())+1;depth=1
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[m.start():end]
surface=(base/'surface.c').read_text();texture=(base/'texture.c').read_text()
readback='\n'.join(function(surface,n) for n in ['depth_stencil_plane_size','packed_readback_to_host_barrier','pack_depth_readback'])
a=texture.index('    VkBuffer texture_source_buffer;',texture.index('static void copy_zeta_surface_to_texture('))
b=texture.index('    pgraph_vk_transition_image_layout',a)
wrapper="""
static VkBuffer texture_pack_readback(PGRAPHState *pg,SurfaceBinding *surface,
    VkCommandBuffer cmd,VkBuffer input,size_t bytes,bool direct,bool downscale,
    VkDeviceSize offset,bool *host)
{
    (void)direct;(void)downscale;(void)offset;
    PGRAPHVkState *r=pg->vk_renderer_state;
    unsigned cw=surface->width,ch=surface->height;
    bool use_compute_to_convert_depth_stencil=true;
    size_t copied_image_size=ROUND_UP((size_t)cw*ch*4,
        r->device_props.limits.minStorageBufferOffsetAlignment)+(size_t)cw*ch;
    VkDeviceSize input_size=depth_stencil_plane_size(cw,ch,
        r->device_props.limits.minStorageBufferOffsetAlignment);
    StorageBuffer *dst_storage_buffer=&r->storage_buffers[BUFFER_COMPUTE_DST];
    assert(input==dst_storage_buffer->buffer&&bytes==(size_t)cw*ch*4);
    /* TEXTURE DEPENDENCIES */
    VkBufferCopy copy={.size=bytes};
    vkCmdCopyBuffer(cmd,texture_source_buffer,r->storage_buffers[BUFFER_STAGING_DST].buffer,1,&copy);
    VkBufferMemoryBarrier reuse={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .buffer=texture_source_buffer,.size=bytes};
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,0,NULL,1,&reuse,0,NULL);
    *host=false;return r->storage_buffers[BUFFER_STAGING_DST].buffer;
}
""".replace('/* TEXTURE DEPENDENCIES */',texture[a:b])
compute=(base/'surface-compute.c').read_text().replace('#include "hw/xbox/nv2a/pgraph/pgraph.h"','').replace('#include "renderer.h"','').replace('#include "compute-dispatch.h"','#include "hw/xbox/nv2a/pgraph/vk/compute-dispatch.h"')
code=Path(__file__).with_name('test-depth-host-pack-vulkan.c').read_text().replace('/* COMPUTE PRODUCTION */',compute).replace('/* READBACK PRODUCTION */',readback+'\n'+wrapper)
code=code.replace('mode<3;mode++){unsigned w=mode==0?64:mode==1?640:1280,h=mode==0?64:mode==1?480:720;', 'mode<6;mode++){unsigned widths[]={1,3,7,64,640,1280},heights[]={1,5,9,64,480,720};unsigned w=widths[mode],h=heights[mode];')
code=code.replace('VkBufferCopy bc={.size=base+pixels}', 'VkBufferCopy bc={.size=base+ROUND_UP(pixels,4)}')
code=code.replace('total_cases++;', 'if(!f)for(size_t i=0;i<(size_t)w*h;i++)assert(((uint32_t*)r.storage_buffers[2].mapped)[i]==(((uint32_t)(i*37)&0xffffff)<<8|(uint8_t)(i*13)));total_cases++;')
code=code.replace('pg.scale=scale;SurfaceBinding','w*=scale;h*=scale;pg.scale=1;SurfaceBinding').replace('unsigned iw=w*scale,ih=h*scale;','unsigned iw=w,ih=h;')
code=code.replace('r.device_props.limits.maxStorageBufferRange=variant?props.limits.maxStorageBufferRange:(uint32_t)(bytes-1);','r.device_props.limits.maxStorageBufferRange=props.limits.maxStorageBufferRange;')
code=code.replace('VkBuffer result=pack_depth_readback(', 'VkBuffer result=(variant?texture_pack_readback:pack_depth_readback)(')
# Force the baseline through its device-pack/transfer path too.
code=code.replace('bool host=false;VkBuffer result=', 'bool host=false;r.device_props.limits.maxStorageBufferRange=(uint32_t)(bytes-1);VkBuffer result=')
code=code.replace('host==!!variant&&copies==!variant','!host&&copies==1')
code=code.replace('old %.3fus host %.3fus','readback %.3fus texture-pack %.3fus').replace('copy 1 -> 0','one GPU pack and one fixture readback copy per path')
code=code.replace('2400 synchronous production packs','4800 packs: 2400 production texture dependency chains and 2400 readback reference chains; D24 independent word oracle; no validation layer')
# Keep two distinct uploads and output snapshots alive until one submission ends.
code=code.replace('StorageBuffer upload=buffer(64<<20,true);','StorageBuffer upload=buffer(128<<20,true);')
a=code.index('uint32_t *in=(uint32_t*)upload.mapped;')
b=code.index('uint8_t *reference=',a)
code=code[:a]+"""
assert(base+ROUND_UP(pixels,4)<(32u<<20)&&bytes*2<(64u<<20));
for(unsigned generation=0;generation<2;generation++){
 uint8_t *data=upload.mapped+((size_t)generation<<25);
 memset(data,0xa5,base+ROUND_UP(pixels,4));uint32_t *in=(uint32_t*)data;
 for(size_t i=0;i<pixels;i++){
  if(f){float z=(float)((i*37+generation*17)%100000)/100000;memcpy(in+i,&z,4);}
  else in[i]=(uint32_t)(i*37+generation*17)&0xffffff;
  data[base+i]=(uint8_t)(i*13+generation*31);
 }
}
"""+code[b:]
a=code.index('bool host=false;r.device_props.limits.maxStorageBufferRange=')
b=code.index('vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT',a)
code=code[:a]+"""
for(unsigned chain=0;chain<2;chain++){
 VkBufferCopy input_copy={.srcOffset=(VkDeviceSize)chain<<25,.size=base+ROUND_UP(pixels,4)};
 vkCmdCopyBuffer(command,upload.buffer,r.storage_buffers[0].buffer,1,&input_copy);
 copies=0;bool host=false;r.device_props.limits.maxStorageBufferRange=(uint32_t)(bytes-1);
 VkBuffer result=(variant?texture_pack_readback:pack_depth_readback)(&pg,&s,command,r.storage_buffers[0].buffer,bytes,false,false,0,&host);
 assert(result==r.storage_buffers[2].buffer&&!host&&copies==1);
 VkBufferMemoryBarrier visible={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
  .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,
  .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
  .buffer=result,.size=bytes};
 vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,1,&visible,0,NULL);
 VkBufferCopy snapshot={.dstOffset=(64u<<20)+(VkDeviceSize)chain*bytes,.size=bytes};
 vkCmdCopyBuffer(command,result,upload.buffer,1,&snapshot);
}
packed_readback_to_host_barrier(command,upload.buffer,false,64u<<20,bytes*2);
"""+code[b:]
# The fixture destination needs reuse ordering after the prior snapshot read.
needle='    VkBufferCopy copy={.size=bytes};'
code=code.replace(needle,"""
    VkBufferMemoryBarrier target={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .buffer=r->storage_buffers[BUFFER_STAGING_DST].buffer,.size=bytes};
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,0,NULL,1,&target,0,NULL);
"""+needle)
code=code.replace('malloc(bytes)','malloc(bytes*2)')
code=code.replace('memcpy(reference,r.storage_buffers[2].mapped,bytes)','memcpy(reference,upload.mapped+(64u<<20),bytes*2)')
code=code.replace('memcmp(reference,r.storage_buffers[2].mapped,bytes)','memcmp(reference,upload.mapped+(64u<<20),bytes*2)')
a=code.index('if(!f)for(size_t i=0;i<(size_t)w*h;i++)assert(')
b=code.index('total_cases++;',a)
code=code[:a]+"""if(!f)for(unsigned generation=0;generation<2;generation++)for(size_t i=0;i<(size_t)w*h;i++)
 assert(((uint32_t*)(upload.mapped+(64u<<20)))[generation*(size_t)w*h+i]==
 ((((uint32_t)(i*37+generation*17)&0xffffff)<<8)|(uint8_t)(i*13+generation*31)));
"""+code[b:]
code=code.replace('4800 packs: 2400 production texture dependency chains and 2400 readback reference chains;',
 '9600 packs: 4800 production texture dependency chains and 4800 readback reference chains; two distinct inputs per submission, no inter-chain CPU wait;')
code=code.replace('one GPU pack and one fixture readback copy per path','two packs per submission; times include fixture transfers')
assert code.count('for(unsigned chain=0;chain<2;chain++)')==1
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='depth-texture-gpu-') as tmp:
    root=Path(tmp);(root/'test.c').write_text(code);exe=root/('test.exe' if os.name=='nt' else 'test')
    link=[str(Path(os.environ.get('WINDIR','C:/Windows'))/'System32/vulkan-1.dll')] if os.name=='nt' else ['-lvulkan']
    subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-misleading-indentation','-Wno-missing-field-initializers','-I'+str(repo),'-I'+str(repo/'deps/vulkan-headers'),'-I'+str(repo/'deps/spirv-headers/include'),str(root/'test.c'),str(repo/'hw/xbox/nv2a/pgraph/spv/depth.c'),*link,'-o',str(exe)],env=env,check=True)
    subprocess.run([str(exe)],env=env,check=True,timeout=120)
