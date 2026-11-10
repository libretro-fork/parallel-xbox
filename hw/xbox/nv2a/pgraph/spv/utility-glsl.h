/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Test/generation reference only; runtime uses immutable utility words. */
#ifndef PGRAPH_SPV_UTILITY_GLSL_H
#define PGRAPH_SPV_UTILITY_GLSL_H
static const char *pvideo_convert_glsl =
    "#version 450\n"
    "layout(local_size_x=64) in;\n"
    "layout(binding=0, std430) readonly buffer Input { uint yuy2[]; };\n"
    "layout(binding=1, rgba8) uniform writeonly image2D rgba;\n"
    "layout(push_constant, std430) uniform Args { uint src; uint stride; uint width; uint height; };\n"
    "void main() {\n"
    "    uint i = gl_GlobalInvocationID.x;\n"
    "    uint pairs = (width + 1u) / 2u;\n"
    "    if (i >= pairs * height) return;\n"
    "    uint x = (i % pairs) * 2u;\n"
    "    uint y = i / pairs;\n"
    "    uint row = stride == 0u ? pairs : stride;\n"
    "    uint pair = yuy2[src + y * row + i % pairs];\n"
    "    int d = int((pair >> 8u) & 255u) - 128;\n"
    "    int e = int(pair >> 24u) - 128;\n"
    "    int cr = 409 * e + 128;\n"
    "    int cg = -100 * d - 208 * e + 128;\n"
    "    int cb = 516 * d + 128;\n"
    "    int c = 298 * (int(pair & 255u) - 16);\n"
    "    int r = clamp((c + cr) >> 8, 0, 255);\n"
    "    int g = clamp((c + cg) >> 8, 0, 255);\n"
    "    int b = clamp((c + cb) >> 8, 0, 255);\n"
    "    imageStore(rgba, ivec2(x, y), vec4(r, g, b, 255) / 255.0);\n"
    "    if (x + 1u < width) {\n"
    "        c = 298 * (int((pair >> 16u) & 255u) - 16);\n"
    "        r = clamp((c + cr) >> 8, 0, 255);\n"
    "        g = clamp((c + cg) >> 8, 0, 255);\n"
    "        b = clamp((c + cb) >> 8, 0, 255);\n"
    "        imageStore(rgba, ivec2(x + 1u, y), vec4(r, g, b, 255) / 255.0);\n"
    "    }\n"
    "}\n";

static const char *vertex_shader_source =
    "#version 450\n"
    "layout(location = 0) out vec3 v_fragColor;\n"
    "\n"
    "vec2 positions[11] = vec2[](\n"
    "    vec2(-0.5, -0.75),\n"
    "    vec2(-0.25, -0.25),\n"
    "    vec2(-0.75, -0.25),\n"
    "    vec2(0.25, -0.25),\n"
    "    vec2(0.25, -0.75),\n"
    "    vec2(0.75, -0.25),\n"
    "    vec2(0.75, -0.75),\n"
    "    vec2(-0.75, 0.75),\n"
    "    vec2(-0.75, 0.25),\n"
    "    vec2(-0.25, 0.25),\n"
    "    vec2(-0.25, 0.75)\n"
    ");\n"
    "\n"
    "vec3 colors[11] = vec3[](\n"
    "    vec3(0.0, 0.0, 1.0),\n"
    "    vec3(0.0, 1.0, 0.0),\n"
    "    vec3(0.0, 1.0, 1.0),\n"
    "    vec3(0.0, 0.0, 1.0),\n"
    "    vec3(0.0, 1.0, 0.0),\n"
    "    vec3(0.0, 1.0, 1.0),\n"
    "    vec3(1.0, 0.0, 0.0),\n"
    "    vec3(0.0, 0.0, 1.0),\n"
    "    vec3(0.0, 1.0, 0.0),\n"
    "    vec3(0.0, 1.0, 1.0),\n"
    "    vec3(1.0, 0.0, 0.0)\n"
    ");\n"
    "\n"
    "void main() {\n"
    "    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);\n"
    "    v_fragColor = colors[gl_VertexIndex];\n"
    "}\n";

static const char *geometry_shader_source =
    "#version 450\n"
    "layout(triangles) in;\n"
    "layout(triangle_strip, max_vertices = 3) out;\n"
    "layout(location = 0) out vec3 fragColor;\n"
    "layout(location = 0) in vec3 v_fragColor[];\n"
    "\n"
    "void main() {\n"
    "    for (int i = 0; i < 3; i++) {\n"
             // This should be just:
             //   gl_Position = gl_in[i].gl_Position;
             //   fragColor = v_fragColor[0];
             // but we apply the same Nvidia bug work around from gl/gpuprops.c
             // to be on the safe side even if the compilers involved with
             // Vulkan are different.
    "        gl_Position = gl_in[i].gl_Position + vec4(1.0/16384.0, 1.0/16384.0, 0.0, 0.0);\n"
    "        precise vec3 color = v_fragColor[0]*(0.999 + gl_in[i].gl_Position.x/16384.0) + v_fragColor[1]*0.00005 + v_fragColor[2]*0.00005;\n"
    "        fragColor = color;\n"
    "        EmitVertex();\n"
    "    }\n"
    "    EndPrimitive();\n"
    "}\n";

static const char *fragment_shader_source =
    "#version 450\n"
    "layout(location = 0) out vec4 outColor;\n"
    "layout(location = 0) in vec3 fragColor;\n"
    "\n"
    "void main() {\n"
    "    outColor = vec4(fragColor, 1.0);\n"
    "}\n";

#include "hw/xbox/nv2a/pgraph/s3tc-decode-glsl.h"

#define S3TC_VK_PREFIX \
    "#version 450\n" \
    "layout(local_size_x=8,local_size_y=8) in;\n" \
    "layout(binding=0,std430) readonly buffer Blocks { uint words[]; };\n" \
    "layout(push_constant) uniform Args { uvec4 shape; uvec4 output_shape; uint src; };\n"
#define S3TC_VK_MAIN \
    S3TC_DECODE_GLSL \
    "void main() {\n" \
    " uvec3 p=gl_GlobalInvocationID;\n" \
    " if(p.x>=output_shape.x || p.y>=output_shape.y || p.z>=shape.z) return;\n" \
    " uvec3 input_pos=p+uvec3(output_shape.zw,0u);\n" \
    " imageStore(output_image,ivec3(p),vec4(decode_s3tc(input_pos,shape,src))/255.0);\n" \
    "}\n"
static const char s3tc_vk_array_glsl[] = S3TC_VK_PREFIX
    "layout(binding=1,rgba8) uniform writeonly image2DArray output_image;\n"
    S3TC_VK_MAIN;
static const char s3tc_vk_volume_glsl[] = S3TC_VK_PREFIX
    "layout(binding=1,rgba8) uniform writeonly image3D output_image;\n"
    S3TC_VK_MAIN;
#undef S3TC_VK_PREFIX
#undef S3TC_VK_MAIN
#endif
