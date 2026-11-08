/* SPDX-License-Identifier: GPL-2.0-or-later */
#define SUN_CAPTURE_GPU_FIXTURE
#include "sun-shadow-capture-owner.h"
#undef SUN_CAPTURE_GPU_FIXTURE

static bool emit_capture(void *opaque,const PgraphSunScene *s,const PgraphSunCapturePacket *packet)
{
    VkCommandBuffer cmd=*(VkCommandBuffer*)opaque;
    assert(s->view[0]==1);
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,packet->pass.pipeline);
    return pgraph_vk_sun_pass_draw(&packet->pass,cmd,&packet->draw);
}
static void capture_gpu_tests(VkRenderPass render,VkFramebuffer framebuffer,VkImage image,
                               Buffer output,VkShaderModule shader)
{
    for(unsigned failure=0;failure<3;failure++) {
        PgraphVkSunPass p={0};
        CHECK(pgraph_vk_sun_pass_create(&p,device,render,shader,VK_FORMAT_R32G32B32A32_SFLOAT,
            16,VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST));
        Buffer a=make_buffer(64,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        Buffer b=make_buffer(64,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        const float triangle[3][4]={{-1,-1,0,1},{3,-1,0,1},{-1,3,0,1}};
        memcpy((uint8_t*)a.mapped+16,triangle,sizeof(triangle));
        memcpy((uint8_t*)b.mapped+16,triangle,sizeof(triangle));
        PgraphSunCapture c={0};PgraphSunCapturePacket packets[2];Owner o=owner(&c);
        o.versions[1].handle=(uint64_t)(uintptr_t)a.buffer;
        o.versions[2].handle=(uint64_t)(uintptr_t)b.buffer;
        o.versions[6].handle=(uint64_t)(uintptr_t)p.pipeline;
        o.emit=emit_capture;o.emit_opaque=&command;o.fail_record=failure;
        PgraphVkSunDraw d={.vertex=a.buffer,.vertex_offset=16,.vertex_size=48,.count=3};
        identity(d.world_to_light,.25f);PgraphSunDrawResources refs=resources();
        assert(begin_capture(&c,packets,2,&o));
        assert(pgraph_sun_capture_append(&c,10,&p,&d,&refs));
        identity(d.world_to_light,.75f);
        assert(pgraph_sun_capture_append(&c,20,&p,&d,&refs));
        /* The normal binding can move to a replacement while captured input
         * A stays protected. No payload copy is performed by capture. */
        assert(!recyclable(&o,1));d.vertex=b.buffer;assert(recyclable(&o,2));
        drop_buffer(b);memset(&d,0,sizeof(d));
        PgraphVkSunPass release=p;memset(&p,0,sizeof(p));
        begin();assert(pgraph_vk_sun_pass_begin(&release,command,render,framebuffer,WIDTH,HEIGHT));
        PgraphSunUse use={.lifetime=3,.scene_epoch=11,.recording=7,.main_submit=1};
        assert(pgraph_sun_capture_submit(&c,use,record,&o)==(failure==0));
        assert(o.promoted==4 && !o.canceled);no_cpu_refs(&o);
        assert(!recyclable(&o,1) && !recyclable(&o,6));
        memset(packets,0x99,sizeof(packets));
        vkCmdEndRenderPass(command);
        VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1},.imageExtent={WIDTH,HEIGHT,1}};
        vkCmdCopyImageToBuffer(command,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,output.buffer,1,&copy);
        host_barrier(output,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
        submit(); /* real queue submission and fence completion */
        retire(&o,1,0,0);no_refs(&o);
        assert(recyclable(&o,1) && recyclable(&o,6));
        for(unsigned i=0;i<WIDTH*HEIGHT;i++)assert(fabsf(((float*)output.mapped)[i]-.25f)<1e-6f);
        drop_buffer(a);pgraph_vk_sun_pass_destroy(&release);
    }
    puts("Sun capture GPU: borrowed source replacement, overwritten host packets, fence handoff and partial-record failure passed");
}
