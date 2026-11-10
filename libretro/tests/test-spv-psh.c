/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <spirv/unified1/spirv.h>
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
    for(unsigned i=0;i<4096;i++){
        PshState s={.combiner_dynamic=true};const unsigned modes[]={0,1,4,5};
        s.alpha_dynamic=!!(i&256);s.point_sprite=!!(i&512);s.smooth_shading=!!(i&1024);s.depth_format=(i/1024)%4;
        unsigned samples=0,rects=0;
        for(unsigned j=0;j<4;j++){
            unsigned mode=modes[(i>>(j*2))&3];s.shader_stage_program|=mode<<(j*5);s.dim_tex[j]=2;
            s.rect_tex[j]=!!(i&(1u<<(j+8))) && !(j==3 && s.point_sprite);samples+=mode==1;rects+=mode==1 && s.rect_tex[j];
            for(unsigned k=0;k<4;k++)s.compare_mode[j][k]=!!(i&(1u<<(k+4)));
        }
        SpvPshOptions opts={.spirv_1_6=!!(i&2048),.ubo_binding=1,.tex_binding=2};GlslcResult a,b;allocations=0;
        assert(pgraph_spv_psh(&s,opts,&a) && allocations==2);unsigned loops=0;
        for(size_t k=5;k<a.num_words;k+=a.words[k]>>16)loops+=(a.words[k]&65535)==246;
        assert(loops==2);unsigned mask=s.alpha_dynamic && samples;
        assert(a.uniforms.count==18+!!rects+mask);assert(a.uniforms.size==(rects?(s.alpha_dynamic?576:560):mask?512:496)+528);
        unsigned arrays=0;
        for(size_t j=0;j<a.uniforms.count;j++){
            const GlslcUniform *u=a.uniforms.members+j;assert(u->offset+u->stride*(u->dim_a-1)+u->dim_v*4<=a.uniforms.size);
            assert((const char *)u->name>=(const char *)(a.uniforms.members+a.uniforms.count));
            if(!strncmp(u->name,"comb",4) && u->dim_a==8){assert(u->dim_v==1 && u->stride==16);arrays++;}
        }
        assert(arrays==4);
        s.combiner_control=0xffffffffu;s.final_inputs_0=s.final_inputs_1=0xa5a5a5a5;
        memset(s.rgb_inputs,0x5a,sizeof(s.rgb_inputs));memset(s.alpha_inputs,0xa5,sizeof(s.alpha_inputs));memset(s.rgb_outputs,0x5a,sizeof(s.rgb_outputs));memset(s.alpha_outputs,0xa5,sizeof(s.alpha_outputs));
        assert(pgraph_spv_psh(&s,opts,&b) && allocations==4);assert(a.num_words==b.num_words && !memcmp(a.words,b.words,a.num_words*4));
        memset(&s,0xa5,sizeof(s));char path[4096];snprintf(path,sizeof(path),"%s/dynamic-%u.spv",argv[1],i);FILE *f=fopen(path,"wb");assert(f);assert(fwrite(a.words,4,a.num_words,f)==a.num_words);fclose(f);
        glslc_result_free_words(&a);free(a.uniforms.members);glslc_result_free_words(&b);free(b.uniforms.members);
    }
    for(unsigned i=0;i<4096;i++){
        PshState s={.final_inputs_0=0x20080000,.final_inputs_1=0x1800};s.combiner_dynamic=!!(i&1024);s.alpha_dynamic=!!(i&512);s.point_sprite=!!(i&2048);
        unsigned rects=0,borders=0,keys=0,kernels=0;
        for(unsigned j=0;j<4;j++){
            s.shader_stage_program|=PS_TEXTUREMODES_PROJECT2D<<(j*5);s.dim_tex[j]=2;s.rect_tex[j]=!!(i&(1u<<(j+4))) && !(j==3 && s.point_sprite);s.border_adjust[j]=!!(i&(1u<<j));s.colorkey_mode[j]=(i>>(j+3))&3;
            unsigned k=(i>>(j+7))%3;s.conv_tex[j]=k==0?CONVOLUTION_FILTER_DISABLED:k==1?CONVOLUTION_FILTER_GAUSSIAN:CONVOLUTION_FILTER_QUINCUNX;
            rects+=s.rect_tex[j];borders+=s.border_adjust[j];keys+=s.colorkey_mode[j]!=COLOR_KEY_NONE;kernels+=k!=0;
        }
        SpvPshOptions opts={.spirv_1_6=!!(i&256),.ubo_binding=1,.tex_binding=2};GlslcResult a,b;allocations=0;
        assert(pgraph_spv_psh(&s,opts,&a) && allocations==2);unsigned loops=0,samples=0,queries=0;
        for(size_t k=5;k<a.num_words;k+=a.words[k]>>16){unsigned op=a.words[k]&65535;loops+=op==246;samples+=op==87;queries+=op==103;}
        assert(loops==1+s.combiner_dynamic+kernels && samples==4 && queries==rects+kernels);
        unsigned expected=11+!!rects+s.alpha_dynamic+(s.combiner_dynamic?7:0)+(borders?2:0)+(keys?2:0);assert(a.uniforms.count==expected);
        expected=(rects?(s.alpha_dynamic?576:560):s.alpha_dynamic?512:496)+(s.combiner_dynamic?528:0)+(borders?128:0)+(keys?128:0);assert(a.uniforms.size==expected && a.uniforms.size<=1360);
        for(size_t j=0;j<a.uniforms.count;j++){
            const GlslcUniform *u=a.uniforms.members+j;assert(u->offset+u->stride*(u->dim_a-1)+u->dim_v*4<=a.uniforms.size);
            if(!strncmp(u->name,"border",6))assert(u->dim_v==3 && u->dim_a==4 && u->stride==16);
            if(!strncmp(u->name,"colorKey",8))assert(u->dim_v==1 && u->dim_a==4 && u->stride==16);
        }
        memset(s.compare_mode,1,sizeof(s.compare_mode));memset(s.rgb_inputs,0x5a,sizeof(s.rgb_inputs));memset(s.alpha_inputs,0xa5,sizeof(s.alpha_inputs));s.surface_zeta_format=0xf00d;
        assert(pgraph_spv_psh(&s,opts,&b) && allocations==4);assert(a.num_words==b.num_words && !memcmp(a.words,b.words,a.num_words*4));
        memset(&s,0xa5,sizeof(s));char path[4096];snprintf(path,sizeof(path),"%s/texture-ops-%u.spv",argv[1],i);FILE *f=fopen(path,"wb");assert(f);assert(fwrite(a.words,4,a.num_words,f)==a.num_words);fclose(f);
        glslc_result_free_words(&a);free(a.uniforms.members);glslc_result_free_words(&b);free(b.uniforms.members);
    }
    for(unsigned i=0;i<4096;i++){
        PshState s={.final_inputs_0=0x20080000,.final_inputs_1=0x1800};s.combiner_dynamic=!!(i&1024);s.alpha_dynamic=!!(i&512);s.point_sprite=!!(i&2048);
        unsigned dimensions=0,projected=0,rects=0,borders=0,keys=0;bool remap=false,project_cube=false;
        for(unsigned j=0;j<4;j++){
            unsigned family=(i&128)?(i/4)%6:(i/(j+1)+j*7)%6,mode=family==0 || family==3?PS_TEXTUREMODES_PROJECT2D:family<3?PS_TEXTUREMODES_PROJECT3D:PS_TEXTUREMODES_CUBEMAP;
            s.shader_stage_program|=mode<<(j*5);s.dim_tex[j]=family<2?3:2;s.tex_cubemap[j]=family==3 || family==4;
            s.rect_tex[j]=family>=2 && !!(i&(1u<<(j+4))) && !(j==3 && s.point_sprite);s.border_adjust[j]=!!(i&(1u<<j));s.colorkey_mode[j]=(i>>(j+3))&3;
            unsigned kind=family<2?1:family==3 || family==4?2:0;dimensions|=1u<<(kind==0?SpvDim2D:kind==1?SpvDim3D:SpvDimCube);
            remap|=family==5;project_cube|=family==3;projected+=family<3;rects+=s.rect_tex[j] && mode!=PS_TEXTUREMODES_CUBEMAP;borders+=s.border_adjust[j] && mode!=PS_TEXTUREMODES_CUBEMAP;keys+=s.colorkey_mode[j]!=0;
        }
        SpvPshOptions opts={.spirv_1_6=!!(i&256),.ubo_binding=1,.tex_binding=2};GlslcResult a,b;allocations=0;
        assert(pgraph_spv_psh(&s,opts,&a) && allocations==2);unsigned found=0,samples=0,projections=0,queries=0,functions=0;
        for(size_t k=5;k<a.num_words;k+=a.words[k]>>16){
            unsigned op=a.words[k]&65535;functions+=op==SpvOpFunction;if(op==SpvOpTypeImage)found|=1u<<a.words[k+3];samples+=op==SpvOpImageSampleImplicitLod;projections+=op==SpvOpImageSampleProjImplicitLod;queries+=op==SpvOpImageQuerySizeLod;
        }
        assert(found==dimensions && samples+projections==4 && projections==projected && queries==rects);assert(functions==1u+s.combiner_dynamic+remap+project_cube);
        unsigned expected=11+!!rects+s.alpha_dynamic+(s.combiner_dynamic?7:0)+(borders?2:0)+(keys?2:0);assert(a.uniforms.count==expected);
        for(size_t j=0;j<a.uniforms.count;j++){const GlslcUniform *u=a.uniforms.members+j;assert(u->offset+u->stride*(u->dim_a-1)+u->dim_v*4<=a.uniforms.size);}
        memset(s.compare_mode,1,sizeof(s.compare_mode));memset(s.rgb_inputs,0x5a,sizeof(s.rgb_inputs));memset(s.alpha_inputs,0xa5,sizeof(s.alpha_inputs));s.surface_zeta_format=0xf00d;
        assert(pgraph_spv_psh(&s,opts,&b) && allocations==4);assert(a.num_words==b.num_words && !memcmp(a.words,b.words,a.num_words*4));
        memset(&s,0xa5,sizeof(s));char path[4096];snprintf(path,sizeof(path),"%s/dimensional-%u.spv",argv[1],i);FILE *f=fopen(path,"wb");assert(f);assert(fwrite(a.words,4,a.num_words,f)==a.num_words);fclose(f);
        glslc_result_free_words(&a);free(a.uniforms.members);glslc_result_free_words(&b);free(b.uniforms.members);
    }
    /* Dependent reads borrow only the immutable key during count/write. */
    for(unsigned i=0;i<4096;i++){
        unsigned unit=1+i%3,choice=(i/3)%4,input=(i/12)%unit;
        const unsigned modes[]={PS_TEXTUREMODES_BUMPENVMAP,PS_TEXTUREMODES_BUMPENVMAP_LUM,PS_TEXTUREMODES_DPNDNT_AR,PS_TEXTUREMODES_DPNDNT_GB};
        bool bump=choice<2,luma=choice==1,rect=bump && !!(i&64),volume=bump && !rect && !!(i&128);
        PshState s={.final_inputs_0=0x20080000,.final_inputs_1=0x1800};
        s.combiner_dynamic=!!(i&256);s.alpha_dynamic=!!(i&512);s.point_sprite=!!(i&1024) && !(unit==3 && rect);
        for(unsigned j=0;j<unit;j++)s.shader_stage_program|=PS_TEXTUREMODES_PASSTHRU<<(j*5);
        s.shader_stage_program|=modes[choice]<<(unit*5);s.dim_tex[unit]=volume?3:2;s.rect_tex[unit]=rect;
        s.border_adjust[unit]=bump && !!(i&2048);s.colorkey_mode[unit]=(i/32)%4;s.alphakill[unit]=!!(i&16);
        if(unit>=2)s.other_stage_input=input<<(16+(unit-2)*4);
        SpvPshOptions opts={.spirv_1_6=!!(i&8),.ubo_binding=1,.tex_binding=2};GlslcResult a,b;allocations=0;
        assert(pgraph_spv_psh(&s,opts,&a) && allocations==2);
        unsigned matrices=0,functions=0,queries=0;
        for(size_t k=5;k<a.num_words;k+=a.words[k]>>16){unsigned op=a.words[k]&65535;matrices+=op==SpvOpMatrixTimesVector;functions+=op==SpvOpFunction;queries+=op==SpvOpImageQuerySizeLod;}
        assert(matrices==bump && functions==1u+s.combiner_dynamic+bump && queries==rect);
        unsigned keys=s.colorkey_mode[unit]!=0;
        assert(a.uniforms.count==11+rect+s.alpha_dynamic+7*s.combiner_dynamic+2*keys+bump+2*luma);
        assert(a.uniforms.size==(rect?(s.alpha_dynamic?576:560):s.alpha_dynamic?512:496)+528*s.combiner_dynamic+128*keys+128*bump+128*luma);
        unsigned fields=0;
        for(size_t j=0;j<a.uniforms.count;j++){
            const GlslcUniform *u=a.uniforms.members+j;
            assert(u->offset+u->stride*(u->dim_a-1)+u->dim_v*4<=a.uniforms.size);
            assert((const char *)u->name>=(const char *)(a.uniforms.members+a.uniforms.count));
            if(!strcmp(u->name,"bumpMat")){assert(u->dim_v==2 && u->dim_a==8 && u->stride==16);fields++;}
            if(!strcmp(u->name,"bumpScale") || !strcmp(u->name,"bumpOffset")){assert(u->dim_v==1 && u->dim_a==4 && u->stride==16);fields++;}
        }
        assert(fields==(unsigned)bump+2u*luma);
        memset(s.compare_mode,1,sizeof(s.compare_mode));s.surface_zeta_format=0xbeef;
        assert(pgraph_spv_psh(&s,opts,&b) && allocations==4);assert(a.num_words==b.num_words && !memcmp(a.words,b.words,a.num_words*4));
        memset(&s,0xa5,sizeof(s));char path[4096];snprintf(path,sizeof(path),"%s/dependent-%u.spv",argv[1],i);FILE *f=fopen(path,"wb");assert(f);assert(fwrite(a.words,4,a.num_words,f)==a.num_words);fclose(f);
        glslc_result_free_words(&a);free(a.uniforms.members);glslc_result_free_words(&b);free(b.uniforms.members);
    }
    for(unsigned i=0;i<8;i++){
        PshState s={.final_inputs_0=0x20080000,.final_inputs_1=0x1800,.shader_stage_program=PS_TEXTUREMODES_DPNDNT_AR<<10};s.dim_tex[2]=2;
        if(i==0){s.shader_stage_program=PS_TEXTUREMODES_DPNDNT_AR;s.dim_tex[0]=2;}
        if(i==1)s.other_stage_input=2<<16;
        if(i==2)s.other_stage_input=3<<16;
        if(i==3)s.other_stage_input=15<<16;
        if(i==4)s.rect_tex[2]=true;
        if(i==5)s.border_adjust[2]=true;
        if(i==6)s.dim_tex[2]=3;
        if(i==7){s.shader_stage_program=PS_TEXTUREMODES_BUMPENVMAP<<10;s.dim_tex[2]=3;s.rect_tex[2]=true;}
        allocations=0;GlslcResult a;assert(!pgraph_spv_psh(&s,(SpvPshOptions){.ubo_binding=1,.tex_binding=2},&a));assert(!allocations && !a.words && !a.uniforms.members);
    }
    for(unsigned i=0;i<9;i++){
        PshState s={.final_inputs_0=0x20080000,.final_inputs_1=0x1800,.shader_stage_program=PS_TEXTUREMODES_PROJECT2D};s.dim_tex[0]=2;
        SpvPshOptions opts={.ubo_binding=1,.tex_binding=2};GlslcResult a;
        if(i==0)s.colorkey_mode[0]=-1;
        if(i==1)s.conv_tex[0]=3;
        if(i==2)s.colorkey_mode[0]=4;
        if(i==3){s.dim_tex[0]=3;s.rect_tex[0]=true;}
        if(i==4){s.tex_cubemap[0]=true;s.conv_tex[0]=CONVOLUTION_FILTER_GAUSSIAN;}
        if(i==5)opts.tex_binding=29;
        if(i==6)opts.ubo_binding=3;
        if(i==7){s.shader_stage_program=PS_TEXTUREMODES_CUBEMAP;s.dim_tex[0]=3;}
        if(i==8){s.shader_stage_program=PS_TEXTUREMODES_PASSTHRU;s.border_adjust[0]=true;}
        allocations=0;assert(!pgraph_spv_psh(&s,opts,&a));assert(!allocations && !a.words && !a.uniforms.members);
    }
    for(unsigned i=0;i<16;i++){
        PshState s={.final_inputs_0=0x04200000,.final_inputs_1=0x1400};SpvPshOptions opts={.ubo_binding=1};
        if(i==0)opts.ubo_binding=32;
        if(i==1){s.combiner_dynamic=true;s.shader_stage_program=PS_TEXTUREMODES_BRDF;}
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
    puts("Direct PSH: 24576 deterministic/final-allocation/ignored-key/owned-layout modules and thirty-three allocation-free unsupported cases PASS");return 0;
}
