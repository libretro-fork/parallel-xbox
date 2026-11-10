/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spirv/unified1/spirv.h>
#include "hw/xbox/nv2a/pgraph/spv/vsh.h"

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
    assert(argc == 2);
    unsigned total = 0;
    for (unsigned i = 0; i < 1120; i++) {
        VshState state;
        make_case(&state, i);
        if(i>=448){
            static const unsigned modes[]={FOG_MODE_LINEAR,FOG_MODE_EXP,FOG_MODE_EXP2,FOG_MODE_LINEAR_ABS,FOG_MODE_EXP_ABS,FOG_MODE_EXP2_ABS};
            state.fog_dynamic=false;state.fog_enable=true;state.fog_mode=modes[(i-448)/112];
        }
        SpvVshOptions opts = { .spirv_1_6 = !!(i & 1),
                              .push_constants = !!(i & 4), .ubo_binding = i % 3 };
        GlslcResult a, b;
        allocations = 0;
        assert(pgraph_spv_vsh_supported(&state, opts));
        assert(pgraph_spv_vsh(&state, opts, &a));
        assert(allocations == 2u + (opts.push_constants && state.uniform_attrs != 0));
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
        unsigned switches=0, unsigned_type=0, control_pointer=0, control_reads=0;
        for(size_t w=5;w<a.num_words;w+=a.words[w]>>16){
            unsigned op=a.words[w]&65535;
            if(op==SpvOpSwitch)switches++;
            if(op==SpvOpTypeInt && a.words[w+2]==32 && !a.words[w+3])unsigned_type=a.words[w+1];
        }
        for(size_t w=5;w<a.num_words;w+=a.words[w]>>16)
            if((a.words[w]&65535)==SpvOpTypePointer && a.words[w+2]==SpvStorageClassUniform && a.words[w+3]==unsigned_type)control_pointer=a.words[w+1];
        for(size_t w=5;w<a.num_words;w+=a.words[w]>>16)
            if((a.words[w]&65535)==SpvOpAccessChain && a.words[w+1]==control_pointer)control_reads++;
        assert(switches==(unsigned)state.fog_dynamic);
        assert(control_reads==(unsigned)state.fog_dynamic);
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
    for (unsigned i = 0; i < 12; i++) {
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
        GlslcResult result; memset(&result, 0xa5, sizeof(result)); allocations = 0;
        assert(!pgraph_spv_vsh(&s, opts, &result));
        assert(!allocations && !result.words && !result.uniforms.members && !result.push_constants.members);
    }
    printf("Direct VSH: %u deterministic native-module/owned-layout/poisoned-key cases; twelve allocation-free unsupported paths PASS\n", total);
    return 0;
}
