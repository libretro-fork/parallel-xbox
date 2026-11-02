# SPDX-License-Identifier: GPL-2.0-or-later
"""Byte-checked BC upload benchmark. --cpu-tail compares final-slice staging
against frozen 3b8b00a; both timed paths submit identical staged GPU uploads.
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
source=(repo/'hw/xbox/nv2a/pgraph/vk/texture.c').read_text()
parts=[re.search(r'typedef struct TextureLevel \{.*?\} TextureLevel;',source,re.S)[0]]
for name in ['stage_level','level_from_vram','volume_blocks_direct','stage_blocks_3d','texture_level_copy','volume_block_slice_size','copy_volume_block_slices']:
    m=re.search(r'^static [^\n]*'+name+r'\(',source,re.M);assert m,name
    end=source.index('{',m.start())+1;depth=1
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    parts.append(source[m.start():end])
# Experimental eligibility is fixture-only; never changes renderer defaults.
parts[3]="""static bool volume_blocks_direct(unsigned w,unsigned h,unsigned d){
 unsigned long long bw=(w+3)/4,blocks=bw*((h+3)/4);
 unsigned long long grouped=blocks*(d/4)+(d%4==1?1:d%4?blocks:0);
 return (h<=4&&w<=16)||grouped<=64||bw*d<=64;
}"""
parts[4]=parts[4].replace('(ordered || volume_blocks_direct(width, height, depth))',
    '(ordered || bw==1 || volume_blocks_direct(width,height,depth))')
measured=parts[4].replace('stage_blocks_3d(', 'measured_stage_blocks_3d(',1)
code=Path(__file__).with_name('test-vulkan-volume-blocks.c').read_text().replace('/* PRODUCTION */','\n'.join(parts)+'\n#undef memcpy\n'+measured)
# Match the renderer's cached/coherent host-memory preference where available.
a=code.index('static unsigned memory_type(');b=code.index('typedef struct { VkBuffer handle;',a)
code=code[:a]+"""
static unsigned selected_host_flags;
static unsigned memory_type(unsigned bits,VkMemoryPropertyFlags required)
{
 unsigned best=32;int best_score=-1;
 for(unsigned i=0;i<memory_properties.memoryTypeCount;i++){
  VkMemoryPropertyFlags flags=memory_properties.memoryTypes[i].propertyFlags;
  if(!(bits&(1u<<i))||(flags&required)!=required)continue;
  int score=(required&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)?
   ((flags&VK_MEMORY_PROPERTY_HOST_CACHED_BIT)?8:0)+((flags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)?4:0):0;
  if(score>best_score){best=i;best_score=score;}
 }
 assert(best<32);
 if(required&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)selected_host_flags=memory_properties.memoryTypes[best].propertyFlags;
 return best;
}
"""+code[b:]
a=code.index('        bool before_direct=');b=code.index('        if(eligible&&!before_direct)',a)
code=code[:a]+"""
        bool before_direct=kind==0&&(depth==1||blocks==1);
        unsigned effective=blocks*(depth/4)+(depth%4==1?1:depth%4?blocks:0);
        bool eligible=kind==0&&(depth==1||w<=4||(h<=4&&w<=16)||effective<=64||((w+3)/4)*depth<=64);
"""+code[b:]
code=code.replace('assert(saved_regions==0);','assert(saved_regions>0);')
clock="""
#ifdef _WIN32
#include <windows.h>
static double now_us(void){LARGE_INTEGER v,f;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&v);return (double)v.QuadPart*1e6/(double)f.QuadPart;}
#else
#include <time.h>
static double now_us(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e6+t.tv_nsec/1e3;}
#endif
static int compare_double(const void *a,const void *b){double x=*(const double*)a,y=*(const double*)b;return (x>y)-(x<y);}
static double median64(double *v){qsort(v,64,sizeof(*v),compare_double);return (v[31]+v[32])/2;}
"""
code=code.replace('#include "hw/xbox/nv2a/nv2a_regs.h"', '#include "hw/xbox/nv2a/nv2a_regs.h"\n'+clock)
needle='        unsigned w=widths[wi], h=heights[hi], depth=depths[di];'
code=code.replace(needle,needle+"""
        if(f==1||kind!=0||mip!=0)continue;
        if(!((w==20&&h==12&&depth==17)||(w==64&&h==12&&depth==5)||
             (w==8&&h==128&&depth==5)||(w==32&&h==32&&depth==4)||
             (w==128&&h==128&&depth==4)))continue;
""")
code=code.replace('    const unsigned widths[] =', '    puts("EXPERIMENTAL ROUTES; conservative renderer defaults unchanged. BENCH: 8 warm + 64 timed uploads/path, alternating order; medians; CPU preparation+upload recording, GPU upload timestamp; coherent modeled RAM, not external-host import or game FPS");\n    const unsigned widths[] =')
bench="""
        printf("Host memory flags=0x%x cached=%u coherent=%u\\n",selected_host_flags,!!(selected_host_flags&VK_MEMORY_PROPERTY_HOST_CACHED_BIT),!!(selected_host_flags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
        assert(queues[family].timestampValidBits);
        VkQueryPoolCreateInfo qci={.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,.queryType=VK_QUERY_TYPE_TIMESTAMP,.queryCount=2};
        VkQueryPool query;CHECK(vkCreateQueryPool(device,&qci,NULL,&query));
        double cpu[2][64],cpu_stage[2][64],gpu_time[2][64];unsigned direct_regions=0;bool direct_used=false;
        for(unsigned rep=0;rep<72;rep++)for(unsigned order=0;order<2;order++){
            unsigned variant=(rep+order)%2;
            CHECK(vkResetCommandBuffer(cmd,0));CHECK(vkResetFences(device,1,&fence));
            renderer.vram_buffer=variant?upload.handle:VK_NULL_HANDLE;state.pgraph.head=0;
            double start=now_us();
            TextureLevel measured={0};measured_stage_blocks_3d(&state.pgraph,&measured,upload.mapped+offset,w,h,depth,bs);
            double staging_time=now_us()-start;
            CHECK(vkBeginCommandBuffer(cmd,&bi));vkCmdResetQueryPool(cmd,query,0,2);
            ib.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;ib.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            ib.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;ib.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&ib);
            vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,query,0);
            VkBufferImageCopy measured_copy=texture_level_copy(&measured,0,mip);
            size_t sliced_size=volume_block_slice_size(&measured,formats[f]);
            submitted_regions=submitted_commands=0;
            if(sliced_size)copy_volume_block_slices(cmd,upload.handle,image,&measured,0,mip,sliced_size);
            else vkCmdCopyBufferToImage(cmd,measured.source==LEVEL_VRAM?upload.handle:staging.handle,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&measured_copy);
            vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,query,1);
            double prepared=now_us()-start;
            if(variant){direct_regions=submitted_regions;direct_used=measured.source==LEVEL_VRAM;}
            ib.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;ib.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            ib.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;ib.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&ib);
            measured_copy.bufferOffset=0;measured_copy.bufferRowLength=0;
            vkCmdCopyImageToBuffer(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,output.handle,1,&measured_copy);
            vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&bb,0,NULL);
            CHECK(vkEndCommandBuffer(cmd));CHECK(vkQueueSubmit(queue,1,&si,fence));
            CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ULL));
            assert(!memcmp(output.mapped,expected,bytes));
            uint64_t stamps[2];CHECK(vkGetQueryPoolResults(device,query,0,2,sizeof(stamps),stamps,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT));
            uint64_t ticks=stamps[1]-stamps[0];
            if(queues[family].timestampValidBits<64)ticks&=(1ull<<queues[family].timestampValidBits)-1;
            if(rep>=8){cpu_stage[variant][rep-8]=staging_time;cpu[variant][rep-8]=prepared;gpu_time[variant][rep-8]=ticks*properties.limits.timestampPeriod/1000;}
        }
        printf("BENCH BC%u %ux%ux%u bytes=%zu route=%s regions=%u CPU-stage/direct-us=%.3f/%.3f GPU-stage/direct-us=%.3f/%.3f exact-bytes\\n",f?3:1,w,h,depth,bytes,direct_used?"direct":"fallback-control",direct_regions,median64(cpu[0]),median64(cpu[1]),median64(gpu_time[0]),median64(gpu_time[1]));
        printf("STAGING-ONLY BC%u %ux%ux%u baseline/current-us=%.3f/%.3f\\n",f?3:1,w,h,depth,median64(cpu_stage[0]),median64(cpu_stage[1]));
        vkDestroyQueryPool(device,query,NULL);
"""
needle='        vkDestroyFence(device, fence, NULL);'
assert needle in code
code=code.replace(needle,bench+needle)
if any(flag in sys.argv[3:] for flag in ('--cpu-layout','--cpu-loop','--cpu-tail')):
    reference='bc-volume-staging-sequential-reference.h' if '--cpu-loop' in sys.argv[3:] else 'bc-volume-staging-reference.h'
    if '--cpu-tail' in sys.argv[3:]:
        reference='bc1-tail-staging-reference.h'
    baseline=(repo/'libretro/tests'/reference).read_text()
    position=code.index('static unsigned selected_host_flags;')
    code=code[:position]+baseline+'\n'+code[position:]
    code=code.replace('renderer.vram_buffer=variant?upload.handle:VK_NULL_HANDLE;', 'renderer.vram_buffer=VK_NULL_HANDLE;')
    code=code.replace('measured_stage_blocks_3d(&state.pgraph,&measured,', '(variant?measured_stage_blocks_3d:baseline_stage_blocks_3d)(&state.pgraph,&measured,')
    code=code.replace('(w==128&&h==128&&depth==4)', '(w==128&&h==128&&depth==4)||(w==256&&h==256&&depth==129)')
    code=code.replace('CPU-stage/direct-us','CPU-baseline/current-us').replace('GPU-stage/direct-us','GPU-baseline/current-us')
    code=code.replace('fallback-control','staged')
    code=code.replace('    assert(saved_regions>0);','    puts("Setup-oracle counters below are outside timed CPU-layout paths.");\n    assert(saved_regions>0);')
    code=code.replace('EXPERIMENTAL ROUTES; conservative renderer defaults unchanged.', 'CPU LAYOUT COMPARISON; both timed paths use identical staged GPU uploads.')
if '--cpu-loop' in sys.argv[3:] or '--cpu-tail' in sys.argv[3:]:
    code=code.replace('(w==256&&h==256&&depth==129)', '(w==256&&h==256&&(depth==4||depth==16||depth==32||depth==129))')
    code=code.replace('depths[] = {1,2,4,5,9,17,65,129}', 'depths[] = {1,2,4,5,9,16,17,32,65,129}')
    code=code.replace('widths[] = {1,4,5,8,12,16,17,20,32,64,128,256,260}', 'widths[] = {1,4,5,8,12,16,17,20,32,64,128,256,257,260,512}')
    code=code.replace('heights[] = {1,4,5,8,9,12,20,32,36,128,256}', 'heights[] = {1,4,5,8,9,12,20,32,36,128,249,253,256,512}')
    rectangular='((w==128&&h==512)||(w==512&&h==128)||(w==257&&h==249)||(w==257&&h==253))&&(depth==5||depth==129)'
    square='(w==256&&h==256&&(depth==4||depth==16||depth==32||depth==129))'
    code=code.replace(square,'('+square+'||('+rectangular+'))')

if '--uncached-memory' in sys.argv[3:]:
    code=code.replace('((flags&VK_MEMORY_PROPERTY_HOST_CACHED_BIT)?8:0)', '((flags&VK_MEMORY_PROPERTY_HOST_CACHED_BIT)?0:8)')
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='volume-bench-') as tmp:
    root=Path(tmp);(root/'test.c').write_text(code);exe=root/('test.exe' if os.name=='nt' else 'test')
    link=[str(Path(os.environ.get('WINDIR','C:/Windows'))/'System32/vulkan-1.dll')] if os.name=='nt' else ['-lvulkan']
    subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-I'+str(repo),'-I'+str(repo/'deps/vulkan-headers'),str(root/'test.c'),*link,'-o',str(exe)],env=env,check=True)
    subprocess.run([str(exe)],env=env,check=True,timeout=120)
