/* SPDX-License-Identifier: MIT */
#include "../s3tc-decode-glsl.h"
static const char s3tc_volume_decode_glsl[] =
    "#version 430\n"
    "layout(local_size_x=8, local_size_y=8) in;\n"
    "layout(std430, binding=0) readonly buffer Blocks { uint words[]; };\n"
    "layout(rgba8, binding=0) uniform writeonly image3D output_image;\n"
    "layout(location=0) uniform uvec4 shape; // width, height, depth, BC1/2/3\n"
    S3TC_DECODE_GLSL
    "void main() {\n"
    "    uvec3 p = gl_GlobalInvocationID;\n"
    "    if (any(greaterThanEqual(p, shape.xyz))) return;\n"
    "    imageStore(output_image, ivec3(p), vec4(decode_s3tc(p,shape,0u))/255.0);\n"
    "}\n"
;
