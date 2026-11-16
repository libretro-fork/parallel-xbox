/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#ifdef _WIN32
#include "ui/gl-dispatch.h"
#include "gloffscreen.h"
extern void libretro_gl_set_standalone_mode(void);
#else
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#include "hw/xbox/nv2a/pgraph/glsl/psh.h"
/* CONTEXT */
#define REQUIRE(x) do{if(!(x)){fprintf(stderr,"ready fragment check failed line=%u: %s\n",__LINE__,#x);exit(1);}}while(0)
static GLuint program(GLenum stage,MString *source)
{
    GLuint shader=glCreateShader(stage);const char *text=mstring_get_str(source);glShaderSource(shader,1,&text,NULL);glCompileShader(shader);
    GLint good;glGetShaderiv(shader,GL_COMPILE_STATUS,&good);if(!good){char log[8192];glGetShaderInfoLog(shader,sizeof(log),NULL,log);fprintf(stderr,"compile %s\n%s\n",log,text);exit(1);}
    GLuint p=glCreateProgram();glProgramParameteri(p,GL_PROGRAM_SEPARABLE,GL_TRUE);glAttachShader(p,shader);glLinkProgram(p);glGetProgramiv(p,GL_LINK_STATUS,&good);
    if(!good){char log[8192];glGetProgramInfoLog(p,sizeof(log),NULL,log);fprintf(stderr,"link %s\n",log);exit(1);}glDetachShader(p,shader);glDeleteShader(shader);return p;
}
static PshState state_for(unsigned test)
{
    unsigned unit=(test/12)%4,pattern=test%12;
    PshState s={0};s.smooth_shading=(test/48)&1;s.z_perspective=(test/4)&1;s.depth_clipping=(test/8)&1;s.window_clip_exclusive=(test/16)&1;
    s.depth_format=(test/24)%4;s.alpha_test=(test/32)&1;s.point_sprite=(test/64)&1;s.alpha_dynamic=(test/128)&1;
    for(unsigned i=0;i<4;i++){s.dim_tex[i]=2;s.alphakill[i]=(test>>(i+1))&1;for(unsigned a=0;a<4;a++)s.compare_mode[i][a]=(test>>(a+2))&1;}
    switch(pattern){
    case 0:break;
    case 1:s.shader_stage_program=1u<<(unit*5);break;
    case 2:s.shader_stage_program=1|(1<<5)|(1<<10)|(1<<15);break;
    case 3:s.shader_stage_program=2u<<(unit*5);s.dim_tex[unit]=3;break;
    case 4:s.shader_stage_program=2u<<(unit*5);s.rect_tex[unit]=unit!=3||!s.point_sprite;break;
    case 5:s.shader_stage_program=3u<<(unit*5);s.tex_cubemap[unit]=true;break;
    case 6:s.shader_stage_program=3u<<(unit*5);break;
    case 7:s.shader_stage_program=4|(4<<5)|(4<<10)|(4<<15);break;
    case 8:s.shader_stage_program=5u<<(unit*5);break;
    case 9:s.shader_stage_program=1|(1<<5)|(3<<10)|(4<<15);s.dim_tex[1]=3;s.tex_cubemap[2]=true;break;
    case 10:s.shader_stage_program=1u<<(unit*5);s.tex_cubemap[unit]=true;break;
    case 11:s.shader_stage_program=1u<<(unit*5);s.rect_tex[unit]=unit!=3||!s.point_sprite;break;
    }
    for(unsigned i=0;i<4;i++){
        unsigned mode=(s.shader_stage_program>>(i*5))&31;
        if(mode>=1&&mode<=3){s.colorkey_mode[i]=(test/96)%4;s.border_adjust[i]=(test/192)&1;}
    }
    if((test/256)&1 && pgraph_glsl_ui_clamp_stage(&(PshState){.shader_stage_program=s.shader_stage_program,.depth_format=DEPTH_FORMAT_UI_CLAMP}))s.depth_format|=DEPTH_FORMAT_UI_CLAMP;
    if(test>=768){
        unsigned k=test-768,family=k%4;unit=(k/4)%4;
        memset(&s,0,sizeof(s));s.smooth_shading=(k/16)&1;s.depth_format=(k/32)%4;
        for(unsigned i=0;i<4;i++)s.dim_tex[i]=2;
        unsigned mode=family==1?2:family==2?3:1;
        s.shader_stage_program=mode<<(unit*5);s.dim_tex[unit]=family==1?3:2;
        s.tex_cubemap[unit]=family>=2;s.alphakill[unit]=(k/32)&1;
        s.rect_tex[unit]=family==0&&((k/64)&1);
    }
    unsigned reg=test>=768?PS_REGISTER_T0+unit:test%6<4?PS_REGISTER_T0+test%4:test%6==4?PS_REGISTER_V0:PS_REGISTER_R0;
    if(test>=768){
        s.combiner_control=(test-768)/16;
        for(unsigned j=0;j<8;j++){
            unsigned input=j?PS_REGISTER_R0:reg;
            s.rgb_inputs[j]=(input<<24)|(PS_REGISTER_C0<<16);
            s.alpha_inputs[j]=((input|16u)<<24)|((PS_REGISTER_C0|16u)<<16);
            s.rgb_outputs[j]=12<<4;s.alpha_outputs[j]=12<<4;
        }
        if(s.combiner_control)reg=PS_REGISTER_R0;
    }
    s.final_inputs_0=0x20000000u|(reg<<16);s.final_inputs_1=(reg|16u)<<8;
    if(test>=912){
        unsigned k=test-912,shape=(k/8)%9;
        memset(&s,0,sizeof(s));s.depth_format=DEPTH_FORMAT_D16;s.smooth_shading=k&1;
        s.combiner_dynamic=!!(k&2);s.combiner_control=1+k%8;if(k&4)s.combiner_control|=0x100;
        s.shader_stage_program=4u|(4u<<5)|(4u<<10)|(4u<<15);
        for(unsigned i=0;i<4;i++)s.dim_tex[i]=2;
        for(unsigned j=0;j<8;j++){
            unsigned a=(PS_REGISTER_T0+(k+j)%4)|(((k/8+j)%8)<<5);
            unsigned b=(j&1?PS_REGISTER_V0:PS_REGISTER_C0)|(((k/2+j)%8)<<5);
            s.rgb_inputs[j]=(a<<24)|(b<<16)|(PS_REGISTER_V1<<8)|PS_REGISTER_C1;
            s.alpha_inputs[j]=((a|16u)<<24)|((b|16u)<<16)|((PS_REGISTER_V1|16u)<<8)|(PS_REGISTER_C1|16u);
            unsigned ro=12u<<4,ao=12u<<4;
            if(shape==1)ao=13u<<4;
            if(shape==2)ro=ao=12;
            if(shape==3)ro=ao=12u<<8;
            if(shape==4){ro=(12u<<4)|13u|(12u<<8);ao=(13u<<4)|12u|(13u<<8);}
            if(shape==5)s.alpha_inputs[j]^=16u<<24;
            if(shape==6)ro=ao=0;
            if(shape==7){ro|=0x80u<<12;ao=0;}
            if(shape==8){ro=12u|(0x40u<<12);ao=13u<<4;}
            unsigned rm=(k/3+j)%8,am=shape==0?rm:(k/7+j)%8;
            s.rgb_outputs[j]=ro|(((k/8+j)%4)<<12)|(rm<<15)|((k&8)?1u<<14:0);
            s.alpha_outputs[j]=ao|(am<<15)|((k&16)?1u<<14:0);
        }
        unsigned out=PS_REGISTER_R0+((k/2)&1);s.final_inputs_0=0x20000000u|(out<<16);s.final_inputs_1=(out|16u)<<8;
    }
    if(test>=1020){
        unsigned k=test-1020,kind=k%6;
        static const uint32_t words[6][4]={
            {0x08040000,0x00000c00,0x18140000,0x00000c00},
            {0x08040000,0x00000c00,0x00002014,0x00000c00},
            {0x00000804,0x00000c00,0x00001814,0x00000c00},
            {0x08040000,0x000000c0,0x00001814,0x0000000c},
            {0x08040020,0x00000c00,0x18140020,0x00000c00},
            {0x08042020,0x00000c00,0x18142020,0x00000c00},
        };
        memset(&s,0,sizeof(s));s.depth_format=DEPTH_FORMAT_D16;s.smooth_shading=(k/6)&1;
        s.combiner_control=1+(k/6)%8;s.combiner_dynamic=!!(k&2);
        s.shader_stage_program=1u|(1u<<5);
        for(unsigned i=0;i<4;i++)s.dim_tex[i]=2;
        for(unsigned j=0;j<8;j++){
            s.rgb_inputs[j]=words[kind][0];s.alpha_inputs[j]=words[kind][2];
            if((k/48)&1){
                if((s.rgb_inputs[j]&65535u)==0)s.rgb_inputs[j]|=0xc0c0u;
                if((s.alpha_inputs[j]&65535u)==0)s.alpha_inputs[j]|=0xc0c0u;
            }
            unsigned rm=(k/24+j)%8,am=(k/18+j)%8;
            s.rgb_outputs[j]=words[kind][1]|(rm<<15);s.alpha_outputs[j]=words[kind][3]|(am<<15);
        }
        s.final_inputs_0=0x200c0000;s.final_inputs_1=0x1c00;
    }
    if(test>=1164){
        unsigned k=test-1164; s=state_for(1020+k%144);
        static const uint32_t finals[]={0x0000000c,0x130c0300,0x130e0300,
                                       0x13050300,0x130f0300,0x0000000e};
        s.final_inputs_0=finals[(k/24)%6];
        s.final_inputs_1=0x1c00|((k&1)?0x80:0);
        /* Complement flags and nonzero E/F must stay on the general path. */
        if(k&4)s.final_inputs_1|=0x60;
        if(k&8)s.final_inputs_1|=0x04050000;
    }
    s.depth_format |= DEPTH_FORMAT_HELPER_LIVENESS;
    return s;
}
static MString *vertex(bool smooth)
{
    MString *s=mstring_from_str("#version 410\nout gl_PerVertex{vec4 gl_Position;float gl_PointSize;};\n");pgraph_glsl_get_vtx_header(s,true,smooth,false,false,false);
    mstring_append(s,"uniform int fixture;uniform int ui;\nvoid main(){\n"
        "vec2 pos=gl_VertexID==0?vec2(-1,-1):gl_VertexID==1?vec2(3,-1):vec2(-1,3);\n"
        "gl_Position=vec4(pos,0,1);gl_PointSize=5.0;\n"
        "vtxD0=vec4(float(gl_VertexID)*0.25,0.5,0.75,0.5);vtxD1=vec4(0.125);vtxB0=vec4(0.75,0.25,0.5,1);vtxB1=vec4(0.25);vtxFog=0.5;\n"
        "float q=fixture%7==0?0.0:fixture%7==1?-1.0:2.0;\n"
        "vtxT0=vec4((pos*0.5+0.5)*float(1<<(fixture%4)),0.25,q);vtxT1=vtxT0*vec4(-1,2,1,1);vtxT2=vtxT0*vec4(2,-1,1,1);vtxT3=vtxT0*vec4(-2,-2,1,1);\n"
        "if(fixture==1000)vtxT0.x-=0.5;\n"
        "vtxPos0=vec4(0,0,128.5,1);vtxPos1=vec4(8,0,160.5,2);vtxPos2=vec4(0,8,192.5,3);triMZ=0.125;\n"
        "if(ui!=0){vtxPos1.xy=vec2(0.125,0.25);vtxPos2.xy=vec2(0.375,0.75);triMZ=uintBitsToFloat(0x80000000u);}\n"
        "}\n");return s;
}
static void uniforms(GLuint p,const PshState *s,unsigned test,bool ready)
{
    float max=(s->depth_format&3)==0?16777215.0f:(s->depth_format&3)==1?65535.0f:512.0f;
    float range[]={0,max,0,(test&1)?200.0f:4.0f};glProgramUniform4fv(p,glGetUniformLocation(p,"clipRange"),1,range);
    GLint regions[32]={0};regions[0]=regions[1]=2;regions[2]=regions[3]=6;glProgramUniform4iv(p,glGetUniformLocation(p,"clipRegion"),8,regions);
    glProgramUniform2i(p,glGetUniformLocation(p,"surfaceScale"),1,1);
    glProgramUniform1f(p,glGetUniformLocation(p,"depthFactor"),.25f);glProgramUniform1f(p,glGetUniformLocation(p,"depthOffset"),.125f);
    glProgramUniform1ui(p,glGetUniformLocation(p,"twoSideSel"),(test/3)&1);glProgramUniform4f(p,glGetUniformLocation(p,"fogColor"),.2f,.3f,.4f,.5f);
    glProgramUniform1i(p,glGetUniformLocation(p,"alphaTestEnable"),s->alpha_test);glProgramUniform1i(p,glGetUniformLocation(p,"alphaFunc"),(test/7)%8);glProgramUniform1i(p,glGetUniformLocation(p,"alphaRef"),(test/9)%256);
    uint32_t kill=0;for(unsigned i=0;i<4;i++)if(s->alphakill[i])kill|=1u<<i;glProgramUniform1ui(p,glGetUniformLocation(p,"alphaKillMask"),kill);
    glProgramUniform1ui(p,glGetUniformLocation(p,"combCtl"),s->combiner_control);
    glProgramUniform1uiv(p,glGetUniformLocation(p,"combRgbIn"),8,s->rgb_inputs);
    glProgramUniform1uiv(p,glGetUniformLocation(p,"combRgbOut"),8,s->rgb_outputs);
    glProgramUniform1uiv(p,glGetUniformLocation(p,"combAlphaIn"),8,s->alpha_inputs);
    glProgramUniform1uiv(p,glGetUniformLocation(p,"combAlphaOut"),8,s->alpha_outputs);glProgramUniform1ui(p,glGetUniformLocation(p,"combFinal0"),s->final_inputs_0);glProgramUniform1ui(p,glGetUniformLocation(p,"combFinal1"),s->final_inputs_1);
    float constants[72];for(unsigned i=0;i<72;i++)constants[i]=.5f;
    glProgramUniform4fv(p,glGetUniformLocation(p,"consts"),18,constants);
    float sizes[12],inverse[12],scales[4];GLuint keys[4]={0},masks[4]={0};
    for(unsigned i=0;i<12;i++){sizes[i]=4;inverse[i]=1.0f/12.0f;}for(unsigned i=0;i<4;i++)scales[i]=(test&2)?2.0f:1.0f;
    glProgramUniform3fv(p,glGetUniformLocation(p,"borderLogicalSize"),4,sizes);glProgramUniform3fv(p,glGetUniformLocation(p,"borderInvRealSize"),4,inverse);
    glProgramUniform1fv(p,glGetUniformLocation(p,"texScale"),4,scales);glProgramUniform1uiv(p,glGetUniformLocation(p,"colorKey"),4,keys);glProgramUniform1uiv(p,glGetUniformLocation(p,"colorKeyMask"),4,masks);
    for(unsigned i=0;i<4;i++){
        char n[32];if(ready){
            snprintf(n,sizeof(n),"fsTex2D%u",i);glProgramUniform1i(p,glGetUniformLocation(p,n),i);
            snprintf(n,sizeof(n),"fsTex3D%u",i);glProgramUniform1i(p,glGetUniformLocation(p,n),4+i);
            snprintf(n,sizeof(n),"fsTexCube%u",i);glProgramUniform1i(p,glGetUniformLocation(p,n),8+i);
        }else{snprintf(n,sizeof(n),"texSamp%u",i);glProgramUniform1i(p,glGetUniformLocation(p,n),s->dim_tex[i]==3?4+i:s->tex_cubemap[i]?8+i:i);}
    }
    if(ready){uint32_t controls[8];REQUIRE(pgraph_glsl_psh_ready_controls(s,controls));controls[0]|=0xa0000000u;controls[1]|=0xfff00000u;controls[2]=controls[3]=0xffffffffu;for(unsigned k=4;k<8;k++)controls[k]|=0xffff0000u;/* MUTATE */glProgramUniform4uiv(p,glGetUniformLocation(p,"fsControl[0]"),2,controls);}
}
int main(int argc,char **argv)
{
    bool partition=argc==4&&!strcmp(argv[1],"--range");
    unsigned first=0,limit=1308;
    if(partition){char *end;first=strtoul(argv[2],&end,10);REQUIRE(!*end);limit=strtoul(argv[3],&end,10);REQUIRE(!*end);REQUIRE(first<limit&&limit<=1308);}
    REQUIRE(argc==1||partition);
    signal(SIGABRT,quiet_abort);setvbuf(stdout,NULL,_IONBF,0);init_gl();
    GLint units;glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&units);REQUIRE(units>=12);
    GLuint fb,rb[2],vao,pipeline,tex[12];glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);glGenRenderbuffers(2,rb);
    glBindRenderbuffer(GL_RENDERBUFFER,rb[0]);glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA32F,8,8);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,rb[0]);
    glBindRenderbuffer(GL_RENDERBUFFER,rb[1]);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT32F,8,8);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,rb[1]);REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenProgramPipelines(1,&pipeline);glBindProgramPipeline(pipeline);glViewport(0,0,8,8);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_ALWAYS);glEnable(GL_PROGRAM_POINT_SIZE);
    glGenTextures(12,tex);
    for(unsigned i=0;i<12;i++){
        unsigned kind=i/4;GLenum target=kind==0?GL_TEXTURE_2D:kind==1?GL_TEXTURE_3D:GL_TEXTURE_CUBE_MAP;
        glActiveTexture(GL_TEXTURE0+i);glBindTexture(target,tex[i]);glTexParameteri(target,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(target,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(target,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(target,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexParameteri(target,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE);
        glTexParameteri(target,GL_TEXTURE_MAX_LEVEL,2);
        for(unsigned level=0;level<3;level++){
            unsigned side=4>>level;float data[4*4*4*4];
            for(unsigned j=0;j<side*side*side;j++){
                data[j*4]=(j%side)/(float)side;data[j*4+1]=((j/side)%side)/(float)side;
                data[j*4+2]=(i%4)*.125f+(j/(side*side))*.0625f+level*.125f;data[j*4+3]=(j%3)?0.5f:0.0f;
            }
            if(kind==1)glTexImage3D(target,level,GL_RGBA32F,side,side,side,0,GL_RGBA,GL_FLOAT,data);
            else if(kind==2)for(unsigned f=0;f<6;f++){
                for(unsigned j=0;j<side*side;j++)data[j*4+2]=(f+1)*.0625f+level*.125f;
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+f,level,GL_RGBA32F,side,side,0,GL_RGBA,GL_FLOAT,data);
            }
            else glTexImage2D(target,level,GL_RGBA32F,side,side,0,GL_RGBA,GL_FLOAT,data);
        }
    }
    /* Reject uncovered domains atomically; never publish partial controls. */
    for(unsigned bad=0;bad<11;bad++){
        PshState reject={.final_inputs_0=0x20090000,.depth_format=DEPTH_FORMAT_HELPER_LIVENESS,.shader_stage_program=1,.dim_tex={2,2,2,2}};
        if(bad==0)reject.shadow_map[0]=1;
        if(bad==1)reject.conv_tex[0]=CONVOLUTION_FILTER_GAUSSIAN;
        if(bad==2)reject.g8b8_signed[0]=1;
        if(bad==3)reject.shader_stage_program=PS_TEXTUREMODES_DOT_ST;
        if(bad==4)reject.dim_tex[0]=0;
        if(bad==5){reject.shader_stage_program=0;reject.depth_format|=DEPTH_FORMAT_UI_CLAMP;}
        if(bad==6)reject.combiner_control=9;
        if(bad==7){reject.shader_stage_program=PS_TEXTUREMODES_PASSTHRU;reject.border_adjust[0]=true;}
        if(bad==8)reject.other_stage_input=8;
        if(bad==9)reject.depth_format=0;
        if(bad==10)reject.final_inputs_0=0;
        uint32_t untouched[8],before[8];memset(untouched,0xa5,sizeof(untouched));memcpy(before,untouched,sizeof(before));
        REQUIRE(!pgraph_glsl_psh_ready_controls(&reject,untouched));REQUIRE(!memcmp(before,untouched,sizeof(before)));
        REQUIRE(!pgraph_glsl_gen_psh_ready(&reject,(GenPshGlslOptions){.locations=true}));
    }
    REQUIRE(!pgraph_glsl_gen_psh_ready(&(PshState){0},(GenPshGlslOptions){.vulkan=true}));
    REQUIRE(!pgraph_glsl_gen_psh_ready(&(PshState){0},(GenPshGlslOptions){0}));
    GLuint vs[2],ready[2];MString *ready_source[2];
    for(unsigned i=0;i<2;i++){MString *v=vertex(i);vs[i]=program(GL_VERTEX_SHADER,v);mstring_unref(v);PshState s={.depth_format=DEPTH_FORMAT_HELPER_LIVENESS,.combiner_dynamic=true,.smooth_shading=i};ready_source[i]=pgraph_glsl_gen_psh_ready(&s,(GenPshGlslOptions){.locations=true});REQUIRE(ready_source[i]);ready[i]=program(GL_FRAGMENT_SHADER,ready_source[i]);}
    uint64_t checked=0;unsigned written=0,discarded=0;
    for(unsigned test=first;test<limit;test++){
        static const GLint filters[]={GL_NEAREST,GL_LINEAR,GL_NEAREST_MIPMAP_NEAREST,GL_LINEAR_MIPMAP_NEAREST,GL_NEAREST_MIPMAP_LINEAR,GL_LINEAR_MIPMAP_LINEAR};
        static const GLint wraps[]={GL_REPEAT,GL_CLAMP_TO_EDGE,GL_CLAMP_TO_BORDER,GL_MIRRORED_REPEAT};
        for(unsigned i=0;i<12;i++){
            GLenum target=i<4?GL_TEXTURE_2D:i<8?GL_TEXTURE_3D:GL_TEXTURE_CUBE_MAP;
            glActiveTexture(GL_TEXTURE0+i);glBindTexture(target,tex[i]);
            glTexParameteri(target,GL_TEXTURE_MIN_FILTER,filters[(test/12+i)%6]);glTexParameteri(target,GL_TEXTURE_MAG_FILTER,(test/6)&1?GL_LINEAR:GL_NEAREST);
            glTexParameterf(target,GL_TEXTURE_LOD_BIAS,(test/7)&1?.25f:-.25f);
            glTexParameteri(target,GL_TEXTURE_WRAP_S,wraps[(test/24+i)%4]);glTexParameteri(target,GL_TEXTURE_WRAP_T,wraps[(test/48+i)%4]);glTexParameteri(target,GL_TEXTURE_WRAP_R,wraps[(test/96+i)%4]);
        }
        PshState s=state_for(test);uint32_t ctl[8];REQUIRE(pgraph_glsl_psh_ready_controls(&s,ctl));
        MString *invariant=pgraph_glsl_gen_psh_ready(&s,(GenPshGlslOptions){.locations=true});REQUIRE(!strcmp(mstring_get_str(invariant),mstring_get_str(ready_source[s.smooth_shading])));mstring_unref(invariant);
        MString *text=pgraph_glsl_gen_psh(&s,(GenPshGlslOptions){.locations=true});GLuint exact=program(GL_FRAGMENT_SHADER,text);mstring_unref(text);
        float values[2][320];
        for(unsigned path=0;path<2;path++){
            GLuint fs=path?ready[s.smooth_shading]:exact;uniforms(fs,&s,test,path);glUseProgramStages(pipeline,GL_VERTEX_SHADER_BIT,vs[s.smooth_shading]);glUseProgramStages(pipeline,GL_FRAGMENT_SHADER_BIT,fs);
            glProgramUniform1i(vs[s.smooth_shading],glGetUniformLocation(vs[s.smooth_shading],"fixture"),test);glProgramUniform1i(vs[s.smooth_shading],glGetUniformLocation(vs[s.smooth_shading],"ui"),!!(s.depth_format&DEPTH_FORMAT_UI_CLAMP));
            glClearColor(.9375f,.8125f,.6875f,.5625f);glClearDepth(.625);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glDrawArrays(s.point_sprite?GL_POINTS:GL_TRIANGLES,0,3);
            glReadPixels(0,0,8,8,GL_RGBA,GL_FLOAT,values[path]);glReadPixels(0,0,8,8,GL_DEPTH_COMPONENT,GL_FLOAT,values[path]+256);REQUIRE(glGetError()==GL_NO_ERROR);
        }
        for(unsigned i=0;i<320;i++){uint32_t a,b;memcpy(&a,&values[0][i],4);memcpy(&b,&values[1][i],4);if(a!=b&&!(isnan(values[0][i])&&isnan(values[1][i]))){fprintf(stderr,"fragment difference case=%u scalar=%u old=%08x new=%08x\n",test,i,a,b);return 1;}checked++;}
        for(unsigned i=0;i<64;i++){if(values[0][i*4]==.9375f)discarded++;else written++;}glDeleteProgram(exact);
    }
    if(!partition||first==0){
    /* Independent analytic oracle: a half-plane kill cuts a 4x4 window to
     * eight pixels. A one-texel volume mip is constant regardless of UV/LOD;
     * verify both shader paths publish precisely those pixels and their depth,
     * rather than merely agreeing with each other's discard implementation. */
    PshState oracle={.depth_format=DEPTH_FORMAT_D16|DEPTH_FORMAT_HELPER_LIVENESS,
        .shader_stage_program=PS_TEXTUREMODES_CLIPPLANE|(PS_TEXTUREMODES_PROJECT3D<<5),
        .dim_tex={2,3,2,2},.final_inputs_0=0x20090000,.final_inputs_1=0x1900};
    MString *oracle_source=pgraph_glsl_gen_psh(&oracle,(GenPshGlslOptions){.locations=true});
    GLuint oracle_exact=program(GL_FRAGMENT_SHADER,oracle_source);mstring_unref(oracle_source);
    glActiveTexture(GL_TEXTURE5);glBindTexture(GL_TEXTURE_3D,tex[5]);
    glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_BASE_LEVEL,2);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MAX_LEVEL,2);
    glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_T,GL_REPEAT);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_R,GL_REPEAT);
    for(unsigned path=0;path<2;path++){
        GLuint fs=path?ready[0]:oracle_exact;uniforms(fs,&oracle,0,path);
        glUseProgramStages(pipeline,GL_VERTEX_SHADER_BIT,vs[0]);glUseProgramStages(pipeline,GL_FRAGMENT_SHADER_BIT,fs);
        glProgramUniform1i(vs[0],glGetUniformLocation(vs[0],"fixture"),1000);glProgramUniform1i(vs[0],glGetUniformLocation(vs[0],"ui"),0);
        glClearColor(.9375f,.8125f,.6875f,.5625f);glClearDepth(.625);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glDrawArrays(GL_TRIANGLES,0,3);
        float values[320];glReadPixels(0,0,8,8,GL_RGBA,GL_FLOAT,values);glReadPixels(0,0,8,8,GL_DEPTH_COMPONENT,GL_FLOAT,values+256);REQUIRE(glGetError()==GL_NO_ERROR);
        for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++){
            bool live=x>=4&&x<6&&y>=2&&y<6;unsigned i=y*8+x;
            const float expected[4]={live?0:.9375f,live?0:.8125f,live?.375f:.6875f,live?0:.5625f};
            for(unsigned j=0;j<4;j++)REQUIRE(values[i*4+j]==expected[j]);
            REQUIRE(values[256+i]==(live?4.0f/65535.0f:.625f));
        }
    }
    glDeleteProgram(oracle_exact);
    puts("PASS independent half-plane/window/volume oracle: eight live pixels and exact color/depth on each path");
    }
    REQUIRE(checked==320ull*(limit-first));
    if(!partition)REQUIRE(written>0&&discarded>0);
    printf("PASS ready fragment: range=%u:%u cases=%u checked=%llu written=%u discarded=%u, two invariant native fragments\n",first,limit,limit-first,(unsigned long long)checked,written,discarded);
    for(unsigned i=0;i<2;i++){glDeleteProgram(ready[i]);glDeleteProgram(vs[i]);mstring_unref(ready_source[i]);}glDeleteTextures(12,tex);glDeleteProgramPipelines(1,&pipeline);glDeleteVertexArrays(1,&vao);glDeleteRenderbuffers(2,rb);glDeleteFramebuffers(1,&fb);return 0;
}
