/*
 * S3TC Texture Decompression
 *
 * Copyright (c) 2020 Wilhelm Kovatch
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* SPDX-License-Identifier: MIT */
/* Integer BC decode matches s3tc.c, including Xbox four-deep volume groups. */
static const char s3tc_volume_decode_glsl[] =
    "#version 430\n"
    "layout(local_size_x=8, local_size_y=8) in;\n"
    "layout(std430, binding=0) readonly buffer Blocks { uint words[]; };\n"
    "layout(rgba8, binding=0) uniform writeonly image3D output_image;\n"
    "layout(location=0) uniform uvec4 shape; // width, height, depth, BC1/2/3\n"
    "void main() {\n"
    "    uvec3 p = gl_GlobalInvocationID;\n"
    "    if (any(greaterThanEqual(p, shape.xyz))) return;\n"
    "    uint bw = (shape.x+3u)/4u, bh = (shape.y+3u)/4u;\n"
    "    uint z0 = p.z & ~3u, nz = min(shape.z-z0, 4u);\n"
    "    uint block = z0*bw*bh + (p.y/4u*bw+p.x/4u)*nz + p.z-z0;\n"
    "    uint base = block * (shape.w == 0u ? 2u : 4u);\n"
    "    uint color = base + (shape.w == 0u ? 0u : 2u);\n"
    "    uint endpoints = words[color];\n"
    "    uint c0 = endpoints & 65535u, c1 = endpoints >> 16u;\n"
    "    uvec3 a = uvec3((c0>>11u)&31u, (c0>>5u)&63u, c0&31u);\n"
    "    uvec3 b = uvec3((c1>>11u)&31u, (c1>>5u)&63u, c1&31u);\n"
    "    uvec3 maximum = uvec3(31u,63u,31u);\n"
    "    uint pixel = (p.y&3u)*4u+(p.x&3u);\n"
    "    uint index = (words[color+1u] >> (2u*pixel)) & 3u;\n"
    "    bool transparent = shape.w == 0u && c0 <= c1;\n"
    "    uvec3 rgb;\n"
    "    if (index == 0u) rgb = (a*255u+maximum/2u)/maximum;\n"
    "    else if (index == 1u) rgb = (b*255u+maximum/2u)/maximum;\n"
    "    else if (transparent) rgb = index == 3u ? uvec3(0u) : ((a+b)*255u+maximum)/(2u*maximum);\n"
    "    else {\n"
    "        uvec3 denominator = 3u*maximum;\n"
    "        rgb = (((4u-index)*a+(index-1u)*b)*255u+denominator/2u)/denominator;\n"
    "    }\n"
    "    uint alpha = transparent && index == 3u ? 0u : 255u;\n"
    "    if (shape.w == 1u) {\n"
    "        alpha = ((words[base+pixel/8u] >> ((pixel&7u)*4u)) & 15u)*17u;\n"
    "    } else if (shape.w == 2u) {\n"
    "        uint lo = words[base], hi = words[base+1u];\n"
    "        uint a0 = lo&255u, a1 = (lo>>8u)&255u;\n"
    "        uint bit = 16u+3u*pixel;\n"
    "        uint selector = (bit >= 32u ? hi>>(bit-32u) : (lo>>bit)|(hi<<(32u-bit))) & 7u;\n"
    "        if (selector == 0u) alpha = a0;\n"
    "        else if (selector == 1u) alpha = a1;\n"
    "        else if (a0 > a1) alpha = ((8u-selector)*a0+(selector-1u)*a1+3u)/7u;\n"
    "        else if (selector < 6u) alpha = ((6u-selector)*a0+(selector-1u)*a1+2u)/5u;\n"
    "        else alpha = selector == 6u ? 0u : 255u;\n"
    "    }\n"
    "    imageStore(output_image, ivec3(p), vec4(uvec4(rgb,alpha))/255.0);\n"
    "}\n"
;
