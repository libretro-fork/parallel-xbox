/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vulkan/vulkan.h>
#include "hw/xbox/nv2a/pgraph/spv/vsh.h"
/* TOKEN_HELPERS */
/* KEY_HELPERS */

int main(int argc,char **argv)
{
    assert(argc==3);
    unsigned first=strtoul(argv[1],NULL,10),limit=strtoul(argv[2],NULL,10);
    VkPhysicalDeviceProperties props={0};props.limits.maxPushConstantsSize=256;
    VkPhysicalDeviceFloatControlsProperties fp={.shaderSignedZeroInfNanPreserveFloat32=true};
    VkVertexInputAttributeDescription attrs[16];
    unsigned compared=0;
    /* CORPUS */
        (void)attrs;(void)prepared_hash;
        for(unsigned mode=0;mode<4;mode++) {
            GenVshGlslOptions options={.vulkan=mode>=2,.locations=!!(mode&1),
                .prefix_outputs=!!(mode&1),.ubo_binding=mode>=2?mode-2:0,
                .use_push_constants_for_uniform_attrs=mode>=2 && opts.push_constants};
            MString *text=pgraph_glsl_gen_vsh(&state,options);
            assert(mstring_test_live(text)==1);
            assert(mstring_get_length(text)>0);
            mstring_unref(text);assert(mstring_test_live(NULL)==0);compared++;
#ifdef TEST_INTERPRETER_OWNERS
            if(!state.is_fixed_function && mode>=2) {
                options.vulkan=mode&1;
                options.use_push_constants_for_uniform_attrs=options.vulkan && opts.push_constants;
                text=pgraph_glsl_gen_vsh_interpreter(&state,options);
                assert(mstring_test_live(text)==1);
                assert(mstring_get_length(text)>0);
                mstring_unref(text);assert(mstring_test_live(NULL)==0);compared++;
#ifdef TEST_DYNAMIC_INTERPRETER_OWNERS
                text=pgraph_glsl_gen_vsh_interpreter_dynamic(&state,options);
                assert(mstring_test_live(text)==1);
                assert(mstring_get_length(text)>0);
                mstring_unref(text);assert(mstring_test_live(NULL)==0);compared++;
#endif
            }
#endif
        }
    }
    printf("VSH source owners [%u,%u): %u returned owners released; no retained temporary owners\n",first,limit,compared);
    return 0;
}
