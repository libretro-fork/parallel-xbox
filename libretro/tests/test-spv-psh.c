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
    for(unsigned i=0;i<4096;i++){
        PshState s={.final_inputs_0=0x20080000,.final_inputs_1=0x1800};
        const unsigned modes[]={PS_TEXTUREMODES_NONE,PS_TEXTUREMODES_PROJECT2D,PS_TEXTUREMODES_PASSTHRU,PS_TEXTUREMODES_CLIPPLANE};
        s.alpha_dynamic=!!(i&256);s.alpha_test=!!(i&512);s.point_sprite=!!(i&1024);s.smooth_shading=!!(i&2048);
        unsigned samples=0,rects=0;
        for(unsigned j=0;j<4;j++){
            unsigned mode=modes[(i>>(j*2))&3];s.shader_stage_program|=mode<<(j*5);s.dim_tex[j]=2;
            s.rect_tex[j]=!!(i&(1u<<(j+8))) && !(j==3 && s.point_sprite);s.alphakill[j]=!!(i&32);
            for(unsigned k=0;k<4;k++)s.compare_mode[j][k]=!!(i&(1u<<(k+4)));
            samples+=mode==PS_TEXTUREMODES_PROJECT2D;rects+=mode==PS_TEXTUREMODES_PROJECT2D && s.rect_tex[j];
        }
        SpvPshOptions opts={.spirv_1_6=!!(i&512),.ubo_binding=1,.tex_binding=i&1?28:2};GlslcResult a,b;allocations=0;
        assert(pgraph_spv_psh(&s,opts,&a) && allocations==2);
        unsigned loads=0,queries=0;
        for(size_t k=5;k<a.num_words;k+=a.words[k]>>16){unsigned op=a.words[k]&65535;loads+=op==87;queries+=op==103;}
        assert(loads==samples && queries==rects);
        unsigned mask=s.alpha_dynamic && samples;assert(a.uniforms.count==11+!!rects+mask);
        assert(a.uniforms.size==(rects?(s.alpha_dynamic?576:560):mask?512:496));
        for(size_t j=0;j<a.uniforms.count;j++){
            const GlslcUniform *u=a.uniforms.members+j;
            assert(u->offset+u->stride*(u->dim_a-1)+u->dim_v*4<=a.uniforms.size);
            assert((const char *)u->name>=(const char *)(a.uniforms.members+a.uniforms.count));
        }
        s.surface_zeta_format=0xa5a5;memset(s.rgb_inputs,0xa5,sizeof(s.rgb_inputs));memset(s.alpha_inputs,0x5a,sizeof(s.alpha_inputs));
        assert(pgraph_spv_psh(&s,opts,&b) && allocations==4);assert(a.num_words==b.num_words && !memcmp(a.words,b.words,a.num_words*4));
        memset(&s,0xa5,sizeof(s));char path[4096];snprintf(path,sizeof(path),"%s/tex-%u.spv",argv[1],i);FILE *f=fopen(path,"wb");assert(f);assert(fwrite(a.words,4,a.num_words,f)==a.num_words);fclose(f);
        glslc_result_free_words(&a);free(a.uniforms.members);glslc_result_free_words(&b);free(b.uniforms.members);
    }
    for(unsigned i=0;i<9;i++){
        PshState s={.final_inputs_0=0x20080000,.final_inputs_1=0x1800,.shader_stage_program=PS_TEXTUREMODES_PROJECT2D};s.dim_tex[0]=2;
        SpvPshOptions opts={.ubo_binding=1,.tex_binding=2};GlslcResult a;
        if(i==0)s.border_adjust[0]=true;
        if(i==1)s.conv_tex[0]=CONVOLUTION_FILTER_QUINCUNX;
        if(i==2)s.colorkey_mode[0]=COLOR_KEY_DISCARD;
        if(i==3)s.dim_tex[0]=3;
        if(i==4)s.tex_cubemap[0]=true;
        if(i==5)opts.tex_binding=29;
        if(i==6)opts.ubo_binding=3;
        if(i==7)s.shader_stage_program=PS_TEXTUREMODES_PROJECT3D;
        if(i==8){s.shader_stage_program=PS_TEXTUREMODES_PASSTHRU;s.border_adjust[0]=true;}
        allocations=0;assert(!pgraph_spv_psh(&s,opts,&a));assert(!allocations && !a.words && !a.uniforms.members);
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
    puts("Direct PSH: 8192 deterministic/final-allocation/ignored-key/owned-layout modules and twenty-five allocation-free unsupported cases PASS");return 0;
}
