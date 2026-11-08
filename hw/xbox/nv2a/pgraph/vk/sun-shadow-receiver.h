/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NV2A_VK_SUN_SHADOW_RECEIVER_H
#define NV2A_VK_SUN_SHADOW_RECEIVER_H

/* Append only to an enhanced receiver shader. No declarations or samplers
 * enter ordinary shaders. The map descriptor and its sampled content version
 * belong to the recording owner and must survive the receiver submission.
 * Declare a global sampler2DShadow shadowMap before this helper.
 * params: depth bias, game-shadow opacity, filter step U, filter step V.
 * The caller supplies normal/occluder receiver offsets in the light position.
 * Apply to the lit color BEFORE texture processing/fog. Preserve alpha. */
static const char pgraph_vk_sun_receiver_glsl[] =
    "vec4 sunShade(vec4 color, vec4 lightPosition, vec4 params) {\n"
    " if(lightPosition.w<=0.0)return color;\n"
    " vec3 p=lightPosition.xyz/lightPosition.w; vec2 uv=p.xy*0.5+vec2(0.5);\n"
    " if(uv.x<=0.0 || uv.x>=1.0 || uv.y<=0.0 || uv.y>=1.0 || p.z<=0.0 || p.z>=1.0)return color;\n"
    " float visibility=0.0;\n"
    " for(int y=-2;y<=2;y++){for(int x=-2;x<=2;x++){\n"
    "  visibility+=textureLod(shadowMap,vec3(uv+vec2(float(x)*params.z,float(y)*params.w),p.z-params.x),0.0);\n"
    " }}\n"
    " float edge=min(min(uv.x,1.0-uv.x),min(uv.y,1.0-uv.y));\n"
    " float coverage=clamp(edge*10.0,0.0,1.0);\n"
    " float shade=1.0-clamp(params.y,0.0,1.0)*coverage*(1.0-visibility/25.0);\n"
    " return vec4(color.rgb*shade,color.a);\n"
    "}\n";

#endif
