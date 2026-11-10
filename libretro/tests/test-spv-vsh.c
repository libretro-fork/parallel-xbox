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
    for (unsigned i = 0; i < 8080; i++) {
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
            bool consumes_normal=false;
            for(unsigned t=0;t<4;t++)for(unsigned a=0;a<3;a++)consumes_normal|=state.fixed_function.texgen[t][a]==TEXGEN_NORMAL_MAP;
            if(!consumes_normal)state.fixed_function.normalization=!state.fixed_function.normalization;
            state.fixed_function.light_dynamic=!state.fixed_function.light_dynamic;
            state.fixed_function.two_sided=!state.fixed_function.two_sided;
            if(!state.fog_enable)state.fixed_function.foggen=(enum VshFoggen)-1;
        }
        assert(pgraph_spv_vsh(&state, opts, &b));
        assert(a.num_words == b.num_words && !memcmp(a.words, b.words, a.num_words * 4));
        assert(a.allocation == NULL && a.allocation_words == a.num_words);
        assert(a.uniforms.count == 7u + (!opts.push_constants && state.uniform_attrs != 0));
        assert(a.uniforms.members[0].dim_a == 192 && a.uniforms.members[0].stride == 16);
        assert(a.uniforms.members[6].offset == 3112 && a.uniforms.members[6].dim_v == 2);
        assert(a.push_constants.count == (size_t)(opts.push_constants && state.uniform_attrs));
        if (a.push_constants.count) {
            assert(a.push_constants.members[0].dim_a == (size_t)__builtin_popcount(state.uniform_attrs));
        }
        unsigned switches=0, unsigned_type=0, control_pointer=0, control_reads=0, preserve_modes=0, preserve_caps=0, matrices=0, transforms=0, normalizations=0;
        for(size_t w=5;w<a.num_words;w+=a.words[w]>>16){
            unsigned op=a.words[w]&65535;
            if(op==SpvOpSwitch)switches++;
            if(op==SpvOpTypeMatrix)matrices++;
            if(op==SpvOpVectorTimesMatrix)transforms++;
            if(op==SpvOpExtInst && a.words[w+4]==69)normalizations++;
            if(op==SpvOpExecutionMode && a.words[w+2]==SpvExecutionModeSignedZeroInfNanPreserve){assert(a.words[w+3]==32);preserve_modes++;}
            if(op==SpvOpCapability && a.words[w+1]==SpvCapabilitySignedZeroInfNanPreserve)preserve_caps++;
            if(op==SpvOpTypeInt && a.words[w+2]==32 && !a.words[w+3])unsigned_type=a.words[w+1];
        }
        for(size_t w=5;w<a.num_words;w+=a.words[w]>>16)
            if((a.words[w]&65535)==SpvOpTypePointer && a.words[w+2]==SpvStorageClassUniform && a.words[w+3]==unsigned_type)control_pointer=a.words[w+1];
        for(size_t w=5;w<a.num_words;w+=a.words[w]>>16)
            if((a.words[w]&65535)==SpvOpAccessChain && a.words[w+1]==control_pointer)control_reads++;
        assert(preserve_modes==(unsigned)(state.point_params_enable || state.is_fixed_function) && preserve_caps==preserve_modes);
        assert(switches==(unsigned)state.fog_dynamic);
        assert(control_reads==(unsigned)state.fog_dynamic);
        unsigned expected_transforms=state.is_fixed_function;
        if(state.is_fixed_function)for(unsigned t=0;t<4;t++)expected_transforms+=state.fixed_function.texture_matrix_enable[t];
        bool needs_eye=state.is_fixed_function && state.fog_enable && state.fixed_function.foggen>=FOGGEN_RADIAL && state.fixed_function.foggen<=FOGGEN_ABS_PLANAR;
        if(state.is_fixed_function)for(unsigned t=0;t<4;t++)for(unsigned a=0;a<4;a++)needs_eye|=state.fixed_function.texgen[t][a]==TEXGEN_EYE_LINEAR;
        expected_transforms+=needs_eye;
        bool needs_normal=false;
        if(state.is_fixed_function)for(unsigned t=0;t<4;t++)for(unsigned a=0;a<3;a++)needs_normal|=state.fixed_function.texgen[t][a]==TEXGEN_NORMAL_MAP;
        expected_transforms+=needs_normal;
        assert(normalizations==(unsigned)(needs_normal && state.fixed_function.normalization));
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
    for (unsigned i = 0; i < 31; i++) {
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
            if(i==12)s.fixed_function.lighting=true;
            if(i==13)s.fixed_function.csv0c_dynamic=true;
            if(i==14)s.fixed_function.texgen_dynamic=true;
            if(i==15)s.fixed_function.skinning=SKINNING_1WEIGHTS;
            if(i==16)s.fog_dynamic=true;
            if(i==17){s.fog_enable=true;s.fixed_function.foggen=(enum VshFoggen)5;}
            if(i==18)s.point_params_enable=true;
            if(i==19)s.fixed_function.texgen[1][3]=TEXGEN_NORMAL_MAP;
            if(i==20)s.compressed_attrs=s.swizzle_attrs=2;
            if(i==21){s.fog_enable=true;s.fixed_function.foggen=(enum VshFoggen)-1;}
            if(i>=22 && i<28){s.fog_enable=true;s.fog_mode=i<25?FOG_MODE_EXP2:FOG_MODE_EXP2_ABS;s.fixed_function.foggen=FOGGEN_RADIAL+(i-22)%3;}
        }
        if(i>=28){s.is_fixed_function=true;opts.preserve_float32=true;s.fixed_function.texgen[0][0]=i==28?(enum VshTexgen)-1:i==29?TEXGEN_SPHERE_MAP:TEXGEN_REFLECTION_MAP;}
        GlslcResult result; memset(&result, 0xa5, sizeof(result)); allocations = 0;
        assert(!pgraph_spv_vsh(&s, opts, &result));
        assert(!allocations && !result.words && !result.uniforms.members && !result.push_constants.members);
    }
    printf("Direct VSH: %u deterministic native-module/owned-layout/poisoned-key cases; thirty-one allocation-free unsupported paths; 512 device-capability/generation checks PASS\n", total);
    return 0;
}
