/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hw/xbox/nv2a/pgraph/spv/psh.h"
static unsigned allocations;
void *psh_test_allocate(size_t n){allocations++;return malloc(n);}
static uint32_t inputs(unsigned i)
{
    static const unsigned regs[]={0,1,2,3,4,5,8,9,10,11,12,13,14};uint32_t v=0;
    for(unsigned j=0;j<4;j++){unsigned b=regs[(i+j*5)%13] | (((i>>(j%3))&7)<<5) | ((i+j)&1?16:0);v=(v<<8)|b;}return v;
}
int main(int argc,char **argv)
{
    assert(argc==2);
    for(unsigned i=0;i<4096;i++){
        PshState s={.final_inputs_0=0x04200000,.final_inputs_1=0x1400,.combiner_control=(i%9) | ((i&7)<<8)};
        s.smooth_shading=i&1;s.depth_clipping=i&2;s.z_perspective=i&4;s.alpha_test=i&8;s.alpha_dynamic=i&16;s.window_clip_exclusive=i&32;s.depth_format=(i/64)%4;
        for(unsigned j=0;j<(s.combiner_control&255);j++){
            s.rgb_inputs[j]=inputs(i+j);s.alpha_inputs[j]=inputs(i+j+13);
            s.rgb_outputs[j]=((i+j)%8<<15) | ((i+j)&7)<<12 | (12<<8) | (4<<4) | 5;
            s.alpha_outputs[j]=((i+j+3)%8<<15) | (((i+j)&4)<<12) | (13<<8) | (12<<4) | 5;
        }
        SpvPshOptions opts={.spirv_1_6=!!(i&256),.ubo_binding=i%3};GlslcResult a,b;allocations=0;
        assert(pgraph_spv_psh_supported(&s,opts));assert(pgraph_spv_psh(&s,opts,&a));assert(allocations==2);
        s.point_sprite=!s.point_sprite;s.alphakill[0]=!s.alphakill[0];s.colorkey_mode[1]=123;s.dim_tex[2]=-99;s.border_adjust[3]=!s.border_adjust[3];s.surface_zeta_format=0xa5a5;
        assert(pgraph_spv_psh(&s,opts,&b));assert(allocations==4);assert(a.num_words==b.num_words && !memcmp(a.words,b.words,a.num_words*4));
        memset(&s,0xa5,sizeof(s));assert(a.uniforms.count==11 && a.uniforms.size==496 && !a.push_constants.count && !a.allocation);
        assert(a.allocation_words==a.num_words && a.words[0]==0x07230203 && a.words[1]==(opts.spirv_1_6?0x10600u:0x10500u));
        for(unsigned j=0;j<a.uniforms.count;j++){const GlslcUniform *u=a.uniforms.members+j;assert((const char *)u->name>=(const char *)(a.uniforms.members+a.uniforms.count));assert(u->offset+u->stride*(u->dim_a-1)+u->dim_v*4<=a.uniforms.size);}
        assert(!strcmp(a.uniforms.members[0].name,"clipRange") && !strcmp(a.uniforms.members[2].name,"consts"));
        char path[4096];snprintf(path,sizeof(path),"%s/psh-%u.spv",argv[1],i);FILE *f=fopen(path,"wb");assert(f);assert(fwrite(a.words,4,a.num_words,f)==a.num_words);fclose(f);
        glslc_result_free_words(&a);free(a.uniforms.members);glslc_result_free_words(&b);free(b.uniforms.members);
    }
    for(unsigned i=0;i<16;i++){
        PshState s={.final_inputs_0=0x04200000,.final_inputs_1=0x1400};SpvPshOptions opts={.ubo_binding=1};
        if(i==0)opts.ubo_binding=32;
        if(i==1)s.combiner_dynamic=true;
        if(i==2)s.combiner_control=9;
        if(i==3)s.final_inputs_0=s.final_inputs_1=0;
        if(i==4)s.shader_stage_program=1;
        if(i==5)s.g8b8_mip_flags=1;
        if(i==6)s.g8b8_signed[0]=1;
        if(i==7)s.tex_x8y24[1]=true;
        if(i==8)s.shadow_map[2]=1;
        if(i==9)s.depth_format=DEPTH_FORMAT_UI_CLAMP;
        if(i==10){s.point_sprite=true;s.rect_tex[3]=true;}
        if(i==11)s.other_stage_input=8;
        if(i==12)s.final_inputs_0=0x06200000;
        if(i==13)s.final_inputs_1=0x0f001400;
        if(i==14){s.combiner_control=1;s.rgb_outputs[0]=1<<4;}
        if(i==15){s.combiner_control=1;s.rgb_inputs[0]=0x0f000000;}
        allocations=0;GlslcResult a;memset(&a,0xa5,sizeof(a));assert(!pgraph_spv_psh(&s,opts,&a));assert(!allocations && !a.words && !a.uniforms.members && !a.push_constants.members);
    }
    puts("Direct PSH: 4096 deterministic/final-allocation/ignored-key/owned-layout modules and sixteen allocation-free unsupported cases PASS");return 0;
}
