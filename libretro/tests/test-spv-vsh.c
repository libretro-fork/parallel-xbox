/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spirv/unified1/spirv.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "hw/xbox/nv2a/pgraph/spv/vsh.h"

typedef struct { uint32_t vk_api_version; VkPhysicalDevice physical_device; } PGRAPHVkState;
static bool pgraph_vk_preserve_float32,cap_support;
static unsigned cap_queries;
static void vkGetPhysicalDeviceProperties2(VkPhysicalDevice gpu,VkPhysicalDeviceProperties2 *props)
{
    assert(gpu==(VkPhysicalDevice)(uintptr_t)0x111);
    assert(props->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 && props->pNext);
    VkPhysicalDeviceFloatControlsProperties *controls=props->pNext;
    assert(controls->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES && !controls->pNext);
    controls->shaderSignedZeroInfNanPreserveFloat32=cap_support;cap_queries++;
}
/* DEVICE CAPS */
static void check_caps(void)
{
    for(unsigned generation=0;generation<64;generation++)for(unsigned minor=0;minor<4;minor++)for(unsigned supported=0;supported<2;supported++){
        PGRAPHVkState r={.vk_api_version=VK_MAKE_VERSION(1,minor,0),.physical_device=(VkPhysicalDevice)(uintptr_t)0x111};
        cap_support=supported;pgraph_vk_preserve_float32=!supported;
        unsigned before=cap_queries;init_float_controls(&r);
        assert(pgraph_vk_preserve_float32==(minor>=2 && supported));
        assert(cap_queries-before==(unsigned)(minor>=2));
    }
}

static unsigned allocations;
void *vsh_test_allocate(size_t n) { allocations++; return malloc(n); }

static void token(uint32_t t[4], unsigned m, unsigned l, unsigned input,
                  unsigned output, bool scalar_output, bool final)
{
    t[0] = 0;
    t[1] = (l << 25) | (m << 21) | (32u << 13) | (input << 9) | 0x1b;
    t[2] = (PARAM_V << 26) | (PARAM_C << 11) | (0x1bu << 17) | (0x1bu << 2);
    t[3] = (PARAM_V << 28) | (15u << 24) | (2u << 20) | (15u << 16) |
           (15u << 12) | (OUTPUT_O << 11) | (output << 3) |
           ((unsigned)scalar_output << 2) | final;
    if (m == MAC_ARL) { t[3] &= ~(15u << 12); }
}

static void make_case(VshState *s, unsigned variant)
{
    memset(s, 0, sizeof(*s));
    s->smooth_shading = !!(variant & 1);
    s->fog_dynamic = !!(variant & 2);
    s->fog_mode = (enum VshFogMode)-1; /* Unused while disabled/dynamic. */
    s->specular_enable = !!(variant & 4);
    s->ignore_specular_alpha = !!(variant & 8);
    s->point_params_enable = false;
    unsigned style = (variant >> 4) % 3;
    s->uniform_attrs = style == 1 ? 1 : style == 2 ? 0xffff : 0;
    if (!s->uniform_attrs && (variant & 64)) { s->compressed_attrs = 2; }
    if (!s->uniform_attrs && (variant & 128)) { s->swizzle_attrs = 4; }
    s->programmable.program_length = 2;
    token(s->programmable.program_data[0], MAC_MOV, ILU_NOP, 0, 0, false, false);
    unsigned m = (variant / 2) % 14, l = (variant / 28) % 8;
    token(s->programmable.program_data[1], m, l, m == MAC_ARL ? 15 : 1,
          variant & 8 ? 5 : 3, !!l && !!(variant & 1), true);
}

int main(int argc, char **argv)
{
    assert(argc == 2);check_caps();
    unsigned total = 0;
    for (unsigned i = 0; i < 30864; i++) {
        VshState state;
        unsigned base=i%1120;
        make_case(&state, base);
        if(base>=448){
            static const unsigned modes[]={FOG_MODE_LINEAR,FOG_MODE_EXP,FOG_MODE_EXP2,FOG_MODE_LINEAR_ABS,FOG_MODE_EXP_ABS,FOG_MODE_EXP2_ABS};
            state.fog_dynamic=false;state.fog_enable=true;state.fog_mode=modes[(base-448)/112];
        }
        if(i>=1120){
            state.point_params_enable=true;
            uint32_t *t=state.programmable.program_data[1];
            t[3]=(t[3]&~(15u<<3))|(6u<<3);
        }
        if(i>=2240){
            state.is_fixed_function=true;state.point_params_enable=false;
            state.fog_dynamic=false;state.fog_enable=false;
            state.programmable.program_length=0x7fffffff;
            for(unsigned t=0;t<4;t++)state.fixed_function.texture_matrix_enable[t]=!!(i&(1u<<t));
            state.fixed_function.normalization=!!(i&16);state.fixed_function.two_sided=!!(i&32);
        }
        if(i>=2752){
            static const unsigned modes[]={FOG_MODE_LINEAR,FOG_MODE_EXP,FOG_MODE_EXP2,FOG_MODE_LINEAR_ABS,FOG_MODE_EXP_ABS,FOG_MODE_EXP2_ABS};
            unsigned f=(i-2752)%24;state.fog_enable=true;
            if(f<12){state.fog_mode=modes[f%6];state.fixed_function.foggen=f<6?FOGGEN_SPEC_ALPHA:FOGGEN_FOG_X;}
            else {static const unsigned linear_exp[]={FOG_MODE_LINEAR,FOG_MODE_EXP,FOG_MODE_LINEAR_ABS,FOG_MODE_EXP_ABS};state.fog_mode=linear_exp[(f-12)%4];state.fixed_function.foggen=FOGGEN_RADIAL+(f-12)/4;}
        }
        if(i>=3712){
            unsigned f=i-3712;state.fog_enable=!!((f/16)&1);
            for(unsigned t=0;t<4;t++){
                state.fixed_function.texture_matrix_enable[t]=!!(f&(1u<<t));
                unsigned pattern=((f/16)+t*17)%81;
                for(unsigned a=0;a<4;a++){state.fixed_function.texgen[t][a]=pattern%3;pattern/=3;}
            }
        }
        if(i>=5008){
            unsigned f=i-5008;
            for(unsigned t=0;t<4;t++){
                state.fixed_function.texture_matrix_enable[t]=!!(f&(1u<<t));
                unsigned pattern=((f/16)+t*37)%192;
                for(unsigned a=0;a<3;a++){unsigned mode=pattern%4;pattern/=4;state.fixed_function.texgen[t][a]=mode==3?TEXGEN_NORMAL_MAP:mode;}
                state.fixed_function.texgen[t][3]=pattern;
            }
        }
  if(i>=8080){
   unsigned f=i-8080, pattern=f/16;
   for(unsigned t=0;t<4;t++){
    state.fixed_function.texture_matrix_enable[t]=!!(f&(1u<<t));
    unsigned n=(pattern+t*43)%128;
    state.fixed_function.texgen[t][0]=n%6;
    state.fixed_function.texgen[t][1]=(n/6+t)%6;
    static const enum VshTexgen rmode[]={TEXGEN_DISABLE,TEXGEN_EYE_LINEAR,TEXGEN_OBJECT_LINEAR,TEXGEN_NORMAL_MAP,TEXGEN_REFLECTION_MAP};
    state.fixed_function.texgen[t][2]=rmode[(n/11+t)%5];
    state.fixed_function.texgen[t][3]=(n/7+t)%3;
   }
  }
  if(i>=10128)state.point_params_enable=true;
  if(i>=11152){
   unsigned f=i-11152;
   state.fixed_function.skinning=1+f/256;
   state.point_params_enable=!!(f&32);
  }
        if(i>=12688){
            unsigned f=i-12688;state.fixed_function.texgen_dynamic=true;
            state.fixed_function.skinning=(f/16)%7;state.point_params_enable=!!(f&512);
   state.fixed_function.normalization=!!(f&256);
            memset(state.fixed_function.texgen,0xff,sizeof(state.fixed_function.texgen));
        }
  if(i>=13712){
   unsigned f=i-13712;state.fog_dynamic=true;
   state.fog_enable=!(f&1024);state.fog_mode=(enum VshFogMode)-1;
   state.fixed_function.foggen=(enum VshFoggen)-1;
   state.fixed_function.texgen_dynamic=!!(f&1);
   state.fixed_function.skinning=(f/16)%7;
   state.point_params_enable=!!(f&8);state.fixed_function.normalization=!!(f&2);
   for(unsigned t=0;t<4;t++){
    state.fixed_function.texture_matrix_enable[t]=!!(f&(1u<<t));
    state.fixed_function.texgen[t][0]=(f/16+t)%6;
    state.fixed_function.texgen[t][1]=(f/32+t)%6;
    state.fixed_function.texgen[t][2]=TEXGEN_NORMAL_MAP;
    state.fixed_function.texgen[t][3]=TEXGEN_OBJECT_LINEAR;
   }
  }
  if(i>=17808){
   unsigned f=i-17808, combo=f/128;
   state.fog_dynamic=false;state.fog_enable=true;
   state.fog_mode=combo<3?FOG_MODE_EXP2:FOG_MODE_EXP2_ABS;
   state.fixed_function.foggen=FOGGEN_RADIAL+combo%3;
   state.fixed_function.texgen_dynamic=!!(f&64);state.point_params_enable=!!(f&32);
  }
  if(i>=18576){
   unsigned f=i-18576, material=(f/7)%81;
   state.fixed_function.lighting=true;state.fixed_function.light_dynamic=false;
   state.fixed_function.two_sided=!!(f&256);state.fixed_function.local_eye=!!(f&512);
   state.specular_enable=!!(f&1024);state.separate_specular=!!(f&2048);
   state.fixed_function.skinning=(f/16)%7;
   state.fixed_function.ambient_src=material%3;material/=3;
   state.fixed_function.emission_src=material%3;material/=3;
   state.fixed_function.diffuse_src=material%3;material/=3;
   state.fixed_function.specular_src=material;
   for(unsigned l=0;l<8;l++)state.fixed_function.light[l]=f&(1u<<l)?LIGHT_INFINITE:LIGHT_OFF;
  }
  if(i>=22672){
   unsigned f=i-22672, pattern=f&255;
   state.fixed_function.two_sided=!!(f&256);state.fixed_function.local_eye=!!(f&512);
   state.specular_enable=!!(f&1024);state.separate_specular=!!(f&2048);
   state.fixed_function.skinning=(f/16)%7;
   for(unsigned l=0;l<8;l++){
    unsigned mode=(pattern>>((l%4)*2))&3;
    if(l>=4)mode=(mode+1)&3;
    if(pattern<16)mode=l==pattern%8?(pattern<8?LIGHT_LOCAL:LIGHT_SPOT):LIGHT_OFF;
    state.fixed_function.light[l]=mode;
   }
  }
  if(i>=26768){
   unsigned f=i-26768;state.fixed_function.light_dynamic=true;
   state.fixed_function.two_sided=!!(f&256);state.fixed_function.local_eye=!!(f&512);
   state.specular_enable=!!(f&1024);state.separate_specular=!!(f&2048);
   for(unsigned l=0;l<8;l++)state.fixed_function.light[l]=(enum VshLight)99;
  }
        SpvVshOptions opts = { .spirv_1_6 = !!(i & 1),
                              .push_constants = !!(i & 4), .ubo_binding = i % 3,
                              .preserve_float32 = i>=1120 || !!(i&8) };
        GlslcResult a, b;
        allocations = 0;
        assert(pgraph_spv_vsh_supported(&state, opts));
        assert(pgraph_spv_vsh(&state, opts, &a));
        assert(allocations == 2u + (opts.push_constants && state.uniform_attrs != 0));
        if(state.is_fixed_function){
            memset(state.programmable.program_data,0xa5,sizeof(state.programmable.program_data));
            state.programmable.program_length=-42;
            bool consumes_normal=state.fixed_function.texgen_dynamic || (state.fixed_function.lighting && state.fixed_function.light_dynamic);
            if(state.fixed_function.lighting)for(unsigned l=0;l<8;l++)consumes_normal|=state.fixed_function.light[l]!=LIGHT_OFF;
            for(unsigned t=0;t<4;t++)for(unsigned a=0;a<3;a++)consumes_normal|=state.fixed_function.texgen[t][a]>=TEXGEN_SPHERE_MAP;
            if(!consumes_normal)state.fixed_function.normalization=!state.fixed_function.normalization;
            if(state.fixed_function.lighting && state.fixed_function.light_dynamic)memset(state.fixed_function.light,0xa5,sizeof(state.fixed_function.light));
            if(!state.fixed_function.lighting)state.fixed_function.light_dynamic=!state.fixed_function.light_dynamic;
            if(!state.fixed_function.lighting)state.fixed_function.two_sided=!state.fixed_function.two_sided;
            if(!state.fog_enable)state.fixed_function.foggen=(enum VshFoggen)-1;
            if(state.fog_dynamic){state.fog_enable=!state.fog_enable;state.fog_mode=(enum VshFogMode)8;state.fixed_function.foggen=(enum VshFoggen)8;}
            if(state.fixed_function.texgen_dynamic){
                memset(state.fixed_function.texgen,0xa5,sizeof(state.fixed_function.texgen));
                for(unsigned t=0;t<4;t++)state.fixed_function.texture_matrix_enable[t]=!state.fixed_function.texture_matrix_enable[t];
            }
        }
        assert(pgraph_spv_vsh(&state, opts, &b));
        assert(a.num_words == b.num_words && !memcmp(a.words, b.words, a.num_words * 4));
        assert(a.allocation == NULL && a.allocation_words == a.num_words);
        bool dynamic=state.is_fixed_function && state.fixed_function.texgen_dynamic;
        bool viewer=dynamic;
        if(state.is_fixed_function)for(unsigned t=0;t<4;t++)for(unsigned k=0;k<3;k++){enum VshTexgen m=state.fixed_function.texgen[t][k];viewer|=m==TEXGEN_SPHERE_MAP || m==TEXGEN_REFLECTION_MAP;}
        bool lit=state.is_fixed_function && state.fixed_function.lighting, active=false;
        unsigned infinite_lights=0, local_lights=0, spot_lights=0;
        if(lit)for(unsigned l=0;l<8;l++){
            enum VshLight mode=state.fixed_function.light[l];active|=mode!=LIGHT_OFF;
            infinite_lights+=mode==LIGHT_INFINITE;local_lights+=mode==LIGHT_LOCAL||mode==LIGHT_SPOT;spot_lights+=mode==LIGHT_SPOT;
        }
        bool live_lights=lit && state.fixed_function.light_dynamic;
        if(live_lights){active=true;infinite_lights=local_lights=spot_lights=1;}
        unsigned lighting_members=lit?(1u+(state.fixed_function.diffuse_src==MATERIAL_COLOR_SRC_MATERIAL)+state.fixed_function.two_sided+active+(infinite_lights!=0)+(local_lights!=0)*3u+(active && state.specular_enable)*(1u+state.fixed_function.two_sided+(infinite_lights!=0 && !state.fixed_function.local_eye))):0;
        lighting_members+=live_lights*(1u+state.fixed_function.two_sided);
        assert(a.uniforms.count == lighting_members + 7u + (!opts.push_constants && state.uniform_attrs != 0) + viewer + (state.is_fixed_function && state.point_params_enable) + dynamic);
        if(viewer){
            const GlslcUniform *u=&a.uniforms.members[7u+(!opts.push_constants && state.uniform_attrs!=0)];
            assert(!strcmp(u->name,"texgenMode") && u->dim_v==1 && u->dim_a==4 && u->stride==16);
            assert(u->offset==3120u+(!opts.push_constants && state.uniform_attrs!=0)*256u);
            if(!lit)assert(a.uniforms.size==u->offset+64+(state.point_params_enable?128u:0)+dynamic*16u);
        }
        if(state.is_fixed_function && state.point_params_enable){
            const GlslcUniform *u=&a.uniforms.members[a.uniforms.count-1-dynamic-lighting_members];
            assert(!strcmp(u->name,"pointParams") && u->dim_v==1 && u->dim_a==8 && u->stride==16);
            assert(u->offset==3120u+(!opts.push_constants && state.uniform_attrs!=0)*256u+viewer*64u);
            if(!lit)assert(a.uniforms.size==u->offset+128+dynamic*16u);
        }
        if(dynamic){
            const GlslcUniform *u=&a.uniforms.members[a.uniforms.count-1-lighting_members];
            assert(!strcmp(u->name,"texMatEnable") && u->dim_v==1 && u->dim_a==1 && u->stride==4);
            assert(u->offset==3120u+(!opts.push_constants && state.uniform_attrs!=0)*256u+64u+state.point_params_enable*128u);
            if(!lit)assert(a.uniforms.size==u->offset+16);
        }
        assert(a.uniforms.members[0].dim_a == 192 && a.uniforms.members[0].stride == 16);
        assert(a.uniforms.members[6].offset == 3112 && a.uniforms.members[6].dim_v == 2);
        assert(a.push_constants.count == (size_t)(opts.push_constants && state.uniform_attrs));
        if (a.push_constants.count) {
            assert(a.push_constants.members[0].dim_a == (size_t)__builtin_popcount(state.uniform_attrs));
        }
        unsigned switches=0, unsigned_type=0, control_pointer=0, control_reads=0, preserve_modes=0, preserve_caps=0, matrices=0, transforms=0, normalizations=0, reflections=0, branches=0, contractions=0, loops=0;
        for(size_t w=5;w<a.num_words;w+=a.words[w]>>16){
            unsigned op=a.words[w]&65535;
            if(op==SpvOpSwitch)switches++;
            if(op==SpvOpLoopMerge)loops++;
            if(op==SpvOpDecorate && a.words[w+2]==SpvDecorationNoContraction)contractions++;
            if(op==SpvOpBranchConditional)branches++;
            if(op==SpvOpTypeMatrix)matrices++;
            if(op==SpvOpVectorTimesMatrix)transforms++;
            if(op==SpvOpExtInst && a.words[w+4]==69)normalizations++;
            if(op==SpvOpExtInst && a.words[w+4]==71)reflections++;
            if(op==SpvOpExecutionMode && a.words[w+2]==SpvExecutionModeSignedZeroInfNanPreserve){assert(a.words[w+3]==32);preserve_modes++;}
            if(op==SpvOpCapability && a.words[w+1]==SpvCapabilitySignedZeroInfNanPreserve)preserve_caps++;
            if(op==SpvOpTypeInt && a.words[w+2]==32 && !a.words[w+3])unsigned_type=a.words[w+1];
        }
        for(size_t w=5;w<a.num_words;w+=a.words[w]>>16)
            if((a.words[w]&65535)==SpvOpTypePointer && a.words[w+2]==SpvStorageClassUniform && a.words[w+3]==unsigned_type)control_pointer=a.words[w+1];
        for(size_t w=5;w<a.num_words;w+=a.words[w]>>16)
            if((a.words[w]&65535)==SpvOpAccessChain && a.words[w+1]==control_pointer)control_reads++;
        bool precise_fog=state.is_fixed_function && (state.fog_dynamic || (state.fog_enable && (state.fog_mode==FOG_MODE_EXP2 || state.fog_mode==FOG_MODE_EXP2_ABS) && state.fixed_function.foggen>=FOGGEN_RADIAL && state.fixed_function.foggen<=FOGGEN_ABS_PLANAR));
        assert(loops==(unsigned)live_lights);
        assert(contractions==(precise_fog?4u:0u)+(unsigned)(live_lights && state.fixed_function.local_eye));
        assert(preserve_modes==(unsigned)(state.point_params_enable || state.is_fixed_function) && preserve_caps==preserve_modes);
        assert(switches==(unsigned)state.fog_dynamic*(state.is_fixed_function?2u:1u)+dynamic*16u);
        assert(control_reads==(unsigned)state.fog_dynamic + (dynamic?5u:(unsigned)viewer) + live_lights*(1u+state.fixed_function.two_sided));
        unsigned expected_transforms=state.is_fixed_function;
        if(dynamic)expected_transforms+=4;
        else if(state.is_fixed_function)for(unsigned t=0;t<4;t++)expected_transforms+=state.fixed_function.texture_matrix_enable[t];
        bool needs_eye=state.is_fixed_function && state.fog_enable && state.fixed_function.foggen>=FOGGEN_RADIAL && state.fixed_function.foggen<=FOGGEN_ABS_PLANAR;
        if(state.is_fixed_function)for(unsigned t=0;t<4;t++)for(unsigned a=0;a<4;a++)needs_eye|=state.fixed_function.texgen[t][a]==TEXGEN_EYE_LINEAR;
        needs_eye|=(local_lights!=0) || (active && state.specular_enable && state.fixed_function.local_eye) || (state.is_fixed_function && state.fog_dynamic) || viewer || (state.is_fixed_function && state.point_params_enable);
        unsigned bones=0;
        if(state.is_fixed_function){static const unsigned counts[]={0,2,2,3,3,4,4};bones=counts[state.fixed_function.skinning];}
        expected_transforms+=bones?bones:needs_eye;
        if(bones && state.point_params_enable)expected_transforms++;
        bool needs_normal=dynamic || active;
        if(state.is_fixed_function)for(unsigned t=0;t<4;t++)for(unsigned a=0;a<3;a++)needs_normal|=state.fixed_function.texgen[t][a]>=TEXGEN_SPHERE_MAP;
        expected_transforms+=needs_normal*(bones?bones:1);
        /* One existing homogeneous-w guard, plus the lazy viewer choice.
         * Eager Normalize/Select regressed physical-GPU reflection rounding. */
        unsigned lights=0;
        if(lit)for(unsigned l=0;l<8;l++)lights+=state.fixed_function.light[l]!=LIGHT_OFF;
        if(state.is_fixed_function)assert(branches==1u+(unsigned)viewer+dynamic*4u+(live_lights?12u+state.fixed_function.two_sided+3u*state.specular_enable*(1u+state.fixed_function.two_sided):local_lights+spot_lights*6u+lights*3u*state.specular_enable*(1u+state.fixed_function.two_sided)));
        assert(reflections==(unsigned)viewer);
        assert(normalizations==(unsigned)(needs_normal && state.fixed_function.normalization)+(unsigned)viewer+(live_lights?2u:lights)+(active && state.specular_enable && state.fixed_function.local_eye)+state.specular_enable*(local_lights+infinite_lights*state.fixed_function.local_eye));
        assert(matrices==(unsigned)state.is_fixed_function && transforms==expected_transforms);
        memset(&state, 0xa5, sizeof(state));
        assert(!strcmp(a.uniforms.members[0].name, "c"));
        assert(!strcmp(a.uniforms.members[6].name, "surfaceSize"));
        char path[4096];
        snprintf(path, sizeof(path), "%s/vsh-%03u.spv", argv[1], i);
        FILE *f = fopen(path, "wb"); assert(f);
        assert(fwrite(a.words, 4, a.num_words, f) == a.num_words); fclose(f);
        glslc_result_free_words(&a); free(a.uniforms.members); free(a.push_constants.members);
        glslc_result_free_words(&b); free(b.uniforms.members); free(b.push_constants.members);
        total++;
    }
    for (unsigned i = 0; i < 39; i++) {
        VshState s; make_case(&s, 0); SpvVshOptions opts = {0};
        if (i == 0) { s.is_fixed_function = true; }
        if (i == 1) { s.fog_enable = true; s.fog_dynamic = false; s.fog_mode = FOG_MODE_ERROR2; }
        if (i == 2) { s.programmable.program_length = 0; }
        if (i == 3) { s.programmable.program_data[1][3] &= ~1u; }
        if (i == 4) { s.programmable.program_data[1][1] |= 15u << 21; }
        if (i == 5) { opts.ubo_binding = 32; }
        if (i == 6) { s.uniform_attrs = s.compressed_attrs = 1; }
        if (i == 7) { s.programmable.program_data[0][3] &= ~(1u << 11); }
        if (i == 8) { s.point_params_enable = true; }
        if (i == 9) { s.fog_enable = true; s.fog_mode = FOG_MODE_ERROR6; }
        if (i == 10) { s.fog_enable = true; s.fog_mode = (enum VshFogMode)8; }
        if (i == 11) { s.fog_enable = true; s.fog_mode = (enum VshFogMode)-1; }
        if(i>=12){
            s.is_fixed_function=true;s.fog_mode=FOG_MODE_LINEAR;opts.preserve_float32=true;
            if(i==12){s.fixed_function.lighting=true;s.fixed_function.light[0]=LIGHT_LOCAL;opts.preserve_float32=false;}
            if(i==13)s.fixed_function.csv0c_dynamic=true;
            if(i==14){s.fixed_function.texgen_dynamic=true;opts.ubo_binding=32;}
            if(i==15)s.fixed_function.skinning=(enum VshSkinning)-1;
            if(i==16){s.fog_dynamic=true;s.fixed_function.lighting=true;s.fixed_function.light[0]=LIGHT_SPOT;s.fixed_function.csv0c_dynamic=true;}
            if(i==17){s.fog_enable=true;s.fixed_function.foggen=(enum VshFoggen)5;}
            if(i==18){s.point_params_enable=true;opts.preserve_float32=false;}
            if(i==19)s.fixed_function.texgen[1][3]=TEXGEN_NORMAL_MAP;
            if(i==20)s.compressed_attrs=s.swizzle_attrs=2;
            if(i==21){s.fog_enable=true;s.fixed_function.foggen=(enum VshFoggen)-1;}
            if(i>=22 && i<28){opts.preserve_float32=false;s.fog_enable=true;s.fog_mode=i<25?FOG_MODE_EXP2:FOG_MODE_EXP2_ABS;s.fixed_function.foggen=FOGGEN_RADIAL+(i-22)%3;}
        }
        if(i>=28){s.is_fixed_function=true;opts.preserve_float32=true;s.fixed_function.texgen[0][i==28?0:i==29?2:3]=i==28?(enum VshTexgen)-1:i==29?TEXGEN_SPHERE_MAP:TEXGEN_REFLECTION_MAP;}
        if(i==31){s.fixed_function.texgen[0][3]=TEXGEN_DISABLE;s.fixed_function.skinning=(enum VshSkinning)7;}
        if(i>=32){
            s=(VshState){.is_fixed_function=true};opts.preserve_float32=true;s.fixed_function.lighting=true;
            if(i==32)s.fixed_function.ambient_src=(enum MaterialColorSource)3;
            if(i==33)s.fixed_function.emission_src=(enum MaterialColorSource)3;
            if(i==34)s.fixed_function.diffuse_src=(enum MaterialColorSource)3;
            if(i==35)s.fixed_function.specular_src=(enum MaterialColorSource)3;
            if(i==36){s.fixed_function.light_dynamic=true;s.fixed_function.csv0c_dynamic=true;}
            if(i==37)s.fixed_function.light[0]=(enum VshLight)-1;
            if(i==38)s.fixed_function.light[0]=(enum VshLight)4;
        }
        GlslcResult result; memset(&result, 0xa5, sizeof(result)); allocations = 0;
        assert(!pgraph_spv_vsh(&s, opts, &result));
        assert(!allocations && !result.words && !result.uniforms.members && !result.push_constants.members);
    }
    printf("Direct VSH: %u deterministic native-module/owned-layout/poisoned-key cases; thirty-nine allocation-free unsupported paths; 512 device-capability/generation checks PASS\n", total);
    return 0;
}
