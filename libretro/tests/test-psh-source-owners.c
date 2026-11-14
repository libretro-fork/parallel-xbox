/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "hw/xbox/nv2a/pgraph/glsl/psh.h"
MString *baseline_gen_psh(const PshState *,GenPshGlslOptions);
void mstring_test_group(unsigned);
size_t mstring_test_norm_formats(unsigned);
size_t mstring_test_created(unsigned);
size_t mstring_test_live(unsigned);
size_t mstring_test_retained_bytes(unsigned);
void mstring_test_cleanup(unsigned);
#define REQUIRE(c) do {if(!(c)){fprintf(stderr,"ownership failure case=%u: %s\n",test,#c);return 1;}}while(0)
static PshState state_for(unsigned flags,unsigned depth,unsigned pattern)
{
    PshState s={.combiner_control=1+(flags%8),.final_inputs_0=0x04200000,.final_inputs_1=0x1400};
    s.smooth_shading=flags&1;s.z_perspective=flags&2;s.depth_clipping=flags&4;
    s.window_clip_exclusive=flags&8;s.alpha_test=flags&16;s.alpha_dynamic=flags&32;
    s.combiner_dynamic=flags&64;s.point_sprite=flags&128;s.depth_format=depth;
    s.other_stage_input=1u<<20;
    for(unsigned i=0;i<4;i++){s.dim_tex[i]=2;for(unsigned a=0;a<4;a++)s.compare_mode[i][a]=(flags>>(a+1))&1;}
    for(unsigned i=0;i<8;i++){
        s.rgb_inputs[i]=0x08200420;s.alpha_inputs[i]=0x18201420;
        s.rgb_outputs[i]=12<<8;s.alpha_outputs[i]=12<<8;
    }
    switch(pattern){
    case 0:break;
    case 1:case 23:s.shader_stage_program=1|(1<<5)|(1<<10)|(1<<15);break;
    case 2:s.shader_stage_program=1<<5;s.rect_tex[1]=true;break;
    case 3:s.shader_stage_program=2<<15;s.dim_tex[3]=3;break;
    case 4:s.shader_stage_program=3; s.tex_cubemap[0]=true;break;
    case 5:s.shader_stage_program=3;break;
    case 6:s.shader_stage_program=4|(4<<5)|(4<<10)|(4<<15);break;
    case 7:s.shader_stage_program=5|(5<<5)|(5<<10)|(5<<15);break;
    case 8:s.shader_stage_program=1|(6<<5);break;
    case 9:s.shader_stage_program=1|(7<<10);break;
    case 10:s.shader_stage_program=1|(17<<5)|(9<<10);break;
    case 11:s.shader_stage_program=1|(17<<5)|(10<<10);break;
    case 12:s.shader_stage_program=1|(17<<5)|(11<<10);s.tex_cubemap[2]=true;break;
    case 13:s.shader_stage_program=1|(17<<5)|(17<<10)|(12<<15);s.tex_cubemap[3]=true;break;
    case 14:s.shader_stage_program=1|(17<<5)|(17<<10)|(13<<15);s.dim_tex[3]=3;break;
    case 15:s.shader_stage_program=1|(17<<5)|(17<<10)|(14<<15);s.tex_cubemap[3]=true;break;
    case 16:s.shader_stage_program=1|(15<<5);break;
    case 17:s.shader_stage_program=1|(16<<15);break;
    case 18:s.shader_stage_program=1<<5;s.g8b8_signed[1]=3;break;
    case 19:s.shader_stage_program=1;s.conv_tex[0]=CONVOLUTION_FILTER_GAUSSIAN;s.rect_tex[0]=flags&1;break;
    case 20:s.shader_stage_program=1;s.shadow_map[0]=1;s.rect_tex[0]=flags&1;s.shadow_depth_func=flags%8;break;
    case 21:s.shader_stage_program=1;s.shadow_map[0]=2|4|8|16;s.rect_tex[0]=flags&1;s.shadow_depth_func=flags%8;break;
    case 22:s.shader_stage_program=1;s.depth_format|=DEPTH_FORMAT_UI_CLAMP;break;
    }
    for(unsigned i=0;i<4;i++){
        s.alphakill[i]=(flags>>i)&1;
        if(pattern==23)s.colorkey_mode[i]=(flags+i)%4;
    }
    return s;
}
int main(void)
{
    unsigned test=0;size_t leaked=0,maximum=0,bytes=0;
    for(unsigned flags=0;flags<256;flags++)for(unsigned depth=0;depth<4;depth++)
    for(unsigned pattern=0;pattern<24;pattern++)for(unsigned target=0;target<3;target++,test++){
        PshState s=state_for(flags,depth,pattern);
        GenPshGlslOptions opts={.vulkan=target==2,.locations=target!=0,.ubo_binding=1,.tex_binding=2};
        size_t old_created=mstring_test_created(0),new_created=mstring_test_created(1);
        size_t before_formats=mstring_test_norm_formats(0);
        mstring_test_group(0);MString *old=baseline_gen_psh(&s,opts);
        REQUIRE(mstring_test_live(0)==2);
        REQUIRE(mstring_test_norm_formats(0)>=before_formats+4);
        mstring_test_group(1);MString *now=pgraph_glsl_gen_psh(&s,opts);
        REQUIRE(mstring_test_live(1)==1);
        REQUIRE(mstring_test_norm_formats(1)==0);
        size_t parent_created=mstring_test_created(0)-old_created;
        size_t candidate_created=mstring_test_created(1)-new_created;
        REQUIRE(candidate_created+1==parent_created);
        REQUIRE(mstring_get_length(now)>0);
        REQUIRE(!strcmp(mstring_get_str(old),mstring_get_str(now)));
        bytes+=mstring_get_length(now);
        mstring_unref(now);mstring_unref(old);
        REQUIRE(mstring_test_live(1)==0);REQUIRE(mstring_test_live(0)==1);
        size_t retained=mstring_test_retained_bytes(0);leaked+=retained;if(retained>maximum)maximum=retained;
        /* Test-only reclamation of the measured historical leak; never hide
         * a candidate leak or keep a second whole source payload snapshot. */
        mstring_test_cleanup(0);REQUIRE(mstring_test_live(0)==0);
    }
    printf("PASS PSH source owners: %u sources byte-identical; %zu source bytes; candidate retains no temporary owners\n",test,bytes);
    printf("Historical clip owner leak: %zu aggregate bytes, %zu maximum bytes per source (real MString/GString capacities)\n",leaked,maximum);
    printf("Normalization-name format calls: parent %zu, candidate %zu\n",mstring_test_norm_formats(0),mstring_test_norm_formats(1));
    printf("MString record allocations: parent %zu, candidate %zu\n",mstring_test_created(0),mstring_test_created(1));
    return 0;
}
