/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NV2A_VK_SUN_SHADOW_MATERIAL_H
#define NV2A_VK_SUN_SHADOW_MATERIAL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct PgraphVkSunVariant {
    unsigned matrices; /* 1 rigid, 2..4 fixed-function palette matrices */
    bool remainder, cutout;
    uint32_t weight_stride, uv_stride;
} PgraphVkSunVariant;

static inline unsigned pgraph_vk_sun_weights(const PgraphVkSunVariant *v)
{
    if (!v || v->matrices < 1 || v->matrices > 4 ||
        (v->matrices == 1 && v->remainder)) return 0;
    return v->matrices == 1 ? 0 : v->matrices - (v->remainder ? 1 : 0);
}

/* Compile only on enhancement activation/cache miss. Constant indices keep
 * the palette layout identical to sixteen std140 vec4 rows (256 bytes).
 * Each bone matrix is already composed with the light camera. Geometry is
 * never skinned, repacked or copied on the CPU for the additional pass. */
static inline bool pgraph_vk_sun_vertex_source(
    const PgraphVkSunVariant *v, char *output, size_t capacity)
{
    if (!output || !capacity) return false;
    output[0] = 0;
    if (!v || v->matrices < 1 || v->matrices > 4 ||
        (v->matrices == 1 && v->remainder)) return false;
    char source[4096];
    int written = snprintf(source, sizeof(source),
        "#version 450\nlayout(location=0) in vec4 position;\n%s%s%s"
        "layout(push_constant) uniform Shadow { vec4 r0;vec4 r1;vec4 r2;vec4 r3;vec4 alpha; } light;\n"
        "void main() {\n%s",
        v->matrices > 1 ? "layout(location=1) in vec4 weight;\nlayout(set=0,binding=0,std140) uniform Palette { vec4 rows[16]; } bones;\n" : "",
        v->cutout ? "layout(location=2) in vec2 texcoord;\n" : "",
        v->cutout ? "layout(location=0) out vec2 uv;\n" : "",
        v->cutout ? "uv=texcoord;\n" : "");
    if (written < 0 || (size_t)written >= sizeof(source)) return false;
    size_t used = written;
    if (v->matrices == 1) {
        written = snprintf(source + used, sizeof(source) - used,
            "gl_Position=position.x*light.r0+position.y*light.r1+position.z*light.r2+position.w*light.r3;\n}\n");
        if (written < 0 || (size_t)written >= sizeof(source) - used) return false;
        used += written;
    } else {
        written = snprintf(source + used, sizeof(source) - used, "vec4 result=vec4(0.0); float remaining=1.0;\n");
        if (written < 0 || (size_t)written >= sizeof(source) - used) return false;
        used += written;
        for (unsigned bone = 0; bone < v->matrices; bone++) {
            bool last = v->remainder && bone == v->matrices - 1;
            char weight[32];
            if (last) snprintf(weight, sizeof(weight), "remaining");
            else snprintf(weight, sizeof(weight), "weight.%c", "xyzw"[bone]);
            written = snprintf(source + used, sizeof(source) - used,
                "result += (position.x*bones.rows[%u]+position.y*bones.rows[%u]+position.z*bones.rows[%u]+position.w*bones.rows[%u])*%s;\n%s",
                bone * 4, bone * 4 + 1, bone * 4 + 2, bone * 4 + 3, weight,
                v->remainder && !last ? "remaining -= " : "");
            if (written < 0 || (size_t)written >= sizeof(source) - used) return false;
            used += written;
            if (v->remainder && !last) {
                written = snprintf(source + used, sizeof(source) - used, "%s;\n", weight);
                if (written < 0 || (size_t)written >= sizeof(source) - used) return false;
                used += written;
            }
        }
        written = snprintf(source + used, sizeof(source) - used, "gl_Position=result;\n}\n");
        if (written < 0 || (size_t)written >= sizeof(source) - used) return false;
        used += written;
    }
    if (used >= capacity) return false;
    memcpy(output, source, used + 1);
    return true;
}

/* Op values match VkCompareOp: NEVER, LESS, EQUAL, LEQUAL, GREATER,
 * NOTEQUAL, GEQUAL, ALWAYS. Sample base-mip alpha only: material/vertex
 * alpha and averaged mip chains must not remove foliage from the map. */
static const char pgraph_vk_sun_cutout_glsl[] =
    "#version 450\nlayout(location=0) in vec2 uv;\n"
    "layout(set=0,binding=1) uniform sampler2D maskTexture;\n"
    "layout(push_constant) uniform Shadow { vec4 r0;vec4 r1;vec4 r2;vec4 r3;vec4 alpha; } light;\n"
    "void main(){ float a=textureLod(maskTexture,uv,0.0).a; float r=light.alpha.x; int f=int(light.alpha.y); bool keep=false;\n"
    "if(f==1)keep=a<r; else if(f==2)keep=a==r; else if(f==3)keep=a<=r;"
    "else if(f==4)keep=a>r; else if(f==5)keep=a!=r; else if(f==6)keep=a>=r; else if(f==7)keep=true;"
    "if(!keep)discard; }\n";

#endif
