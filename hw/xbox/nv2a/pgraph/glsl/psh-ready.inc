/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Experimental regular-texture and fixed-depth shadow coverage; helper-liveness keys only.
 * Controls are 32 bytes of metadata, not a guest or texture payload snapshot.
 * Reject uncovered states before writing any output controls. Native owners
 * and typed texture alias bindings must be ready before this can be selected. */
bool pgraph_glsl_psh_ready_controls(const PshState *s, uint32_t controls[8])
{
    unsigned ui = pgraph_glsl_ui_clamp_stage(s);
    if (!(s->depth_format & DEPTH_FORMAT_HELPER_LIVENESS) ||
        (s->combiner_control & 255) > 8 ||
        (!s->combiner_dynamic && !(s->final_inputs_0 | s->final_inputs_1)) ||
        (s->point_sprite && s->rect_tex[3]) ||
        ((s->depth_format & DEPTH_FORMAT_UI_CLAMP) && !ui) ||
        (s->other_stage_input & 0x888)) return false;
    for (unsigned i = 0; i < 4; i++) {
        unsigned mode = (s->shader_stage_program >> (i * 5)) & 31;
        if (mode > PS_TEXTUREMODES_CLIPPLANE || s->g8b8_signed[i] ||
            (mode == PS_TEXTUREMODES_PASSTHRU && s->border_adjust[i])) return false;
        if (mode < PS_TEXTUREMODES_PROJECT2D || mode > PS_TEXTUREMODES_CUBEMAP) continue;
        if (s->dim_tex[i] != 2 && s->dim_tex[i] != 3) return false;
        if (s->colorkey_mode[i] < 0 || s->colorkey_mode[i] > 3) return false;
        if (s->shadow_map[i] &&
            (s->shadow_map[i] != 1 ||
             (mode != PS_TEXTUREMODES_PROJECT2D && mode != PS_TEXTUREMODES_PROJECT3D) ||
             s->dim_tex[i] != 2 || s->tex_cubemap[i] ||
             s->shadow_depth_func < SHADOW_DEPTH_FUNC_NEVER ||
             s->shadow_depth_func > SHADOW_DEPTH_FUNC_ALWAYS)) return false;
        if (mode == PS_TEXTUREMODES_PROJECT2D && s->conv_tex[i]) return false;
        if ((s->dim_tex[i] == 3 && (s->rect_tex[i] || s->tex_cubemap[i])) ||
            (mode == PS_TEXTUREMODES_CUBEMAP && s->dim_tex[i] != 2) ||
            (mode == PS_TEXTUREMODES_PROJECT3D && s->tex_cubemap[i])) return false;
    }
    controls[0] = (s->depth_format & DEPTH_FORMAT_MASK) |
                  (s->depth_clipping ? 4 : 0) | (s->z_perspective ? 8 : 0) |
                  (s->point_sprite ? 16 : 0) | (s->window_clip_exclusive ? 32 : 0) |
                  (ui << 8);
    controls[1] = s->shader_stage_program & 0xfffff;
    controls[2] = controls[3] = 0;
    for (unsigned i = 0; i < 4; i++) {
        unsigned mode = (controls[1] >> (i * 5)) & 31;
        uint32_t flags = 0;
        if (mode >= 1 && mode <= 3) {
            flags = (mode != 3 && s->rect_tex[i] ? 1 : 0) | (s->tex_cubemap[i] ? 2 : 0) |
                    (s->dim_tex[i] == 3 ? 4 : 0) | (mode != 3 && s->border_adjust[i] ? 8 : 0) |
                    ((unsigned)s->colorkey_mode[i] << 4);
        } else if (mode == PS_TEXTUREMODES_CLIPPLANE) {
            for (unsigned a = 0; a < 4; a++) flags |= (uint32_t)s->compare_mode[i][a] << (8 + a);
        }
        /* Shadow comparison uses the existing RGBA depth alias, not a new
         * sampler owner. Ordinary shadow sampling ignores border adjustment. */
        if (s->shadow_map[i] == 1 && (mode == 1 || mode == 2)) {
            flags = (flags & ~8u) | 64u | ((unsigned)s->shadow_depth_func << 7);
        }
        controls[4 + i] = flags;
    }
    return true;
}

static void psh_ready_window_clip(MString *out)
{
    mstring_append(out,
        "bool clipContained=false;\n"
        "vec2 coord=gl_FragCoord.xy-0.5;\n"
        "for(int i=0;i<8;i++){\n"
        " bool outside=any(bvec4(lessThan(coord,vec2(clipRegion[i].xy)),"
        "greaterThanEqual(coord,vec2(clipRegion[i].zw))));\n"
        " if(!outside){if((fsControl[0].x&32u)!=0u)fsKill=true;clipContained=true;break;}\n"
        "}\nif((fsControl[0].x&32u)==0u&&!clipContained)fsKill=true;\n");
}

static void psh_ready_depth_value(MString *out)
{
    mstring_append(out,
        "precise float zvalue;\nif((fsControl[0].x&8u)!=0u){\n"
        " zvalue=vtxPos0.w+(bc1*(vtxPos1.w-vtxPos0.w)+bc2*(vtxPos2.w-vtxPos0.w));\n"
        " if(zvalue>0.0){float zslopeofs=depthFactor*triMZ*zvalue*zvalue;\n"
        " zvalue+=depthOffset;zvalue+=zslopeofs;}\n"
        " else{zvalue=uintBitsToFloat(0x7F7FFFFFu);}\n"
        " if(isnan(zvalue))zvalue=uintBitsToFloat(0x7F7FFFFFu);\n"
        "}else{\n"
        " zvalue=vtxPos0.z+(bc1*(vtxPos1.z-vtxPos0.z)+bc2*(vtxPos2.z-vtxPos0.z));\n"
        " zvalue+=depthOffset;zvalue+=depthFactor*triMZ;\n}\n");
}

/* Keep ready-path invocations alive through all implicit-derivative texture
 * work. Killing lanes earlier changes filtered volume sampling on llvmpipe;
 * only the final framebuffer/depth publication is suppressed. No shader image
 * stores, atomics or other side effects occur in this covered source domain. */
static void psh_ready_textures(struct PixelShader *ps, MString *preflight, MString *vars)
{
    unsigned helpers = 0;
    psh_append_common_helper(preflight, PSH_CUBE_TO_2D, &helpers);
    psh_append_common_helper(preflight, PSH_2D_TO_CUBE, &helpers);
    define_colorkey_comparator(preflight);
    /* Opaque sampler parameters have fixed typed call-site owners: no dynamic
     * indexing of sampler arrays or aliasing different types on one unit. */
    mstring_append(preflight,
        "vec4 fsTexture(int stage,uint mode,sampler2D s2,sampler3D s3,samplerCube sc,vec4 p){\n"
        " uint f=fsControl[1][stage];\n"
        " if(mode==0u)return vec4(0.0,0.0,0.0,1.0);\n"
        " if(mode==4u)return p;\n"
        " if(mode==5u){for(int a=0;a<4;a++){\n"
        " if(((f&(1u<<uint(a+8)))!=0u)?p[a]>=0.0:p[a]<0.0)fsKill=true;}return vec4(0.0);}\n"
        " if((f&64u)!=0u){\n"
        "  uint cmp=(f>>7u)&7u;if(cmp==0u)return vec4(0.0);if(cmp==7u)return vec4(1.0);\n"
        "  vec3 coord=p.xyw;if((f&1u)!=0u)coord=vec3(p.xy/(textureSize(s2,0)/texScale[stage]),p.w);\n"
        "  vec4 depth=textureProj(s2,coord);float ref=0.0;\n"
        "  if(mode==2u){float maximum=depth.y>0.0?float(0xFFFFFF):(depth.z>0.0?511.9375:float(0xFFFF));\n"
        "   depth.x*=maximum;ref=clamp(p.z/p.w,0.0,maximum);}\n"
        "  bool pass=cmp==1u?depth.x<ref:cmp==2u?depth.x==ref:cmp==3u?depth.x<=ref:\n"
        "            cmp==4u?depth.x>ref:cmp==5u?depth.x!=ref:depth.x>=ref;return vec4(pass?1.0:0.0);\n"
        " }\n"
        " if(mode!=3u&&(f&8u)!=0u)p.xyz=(p.xyz*borderLogicalSize[stage]+vec3(4,4,4))*borderInvRealSize[stage];\n"
        " if(mode==1u){\n"
        "  if((f&4u)!=0u)return textureProj(s3,vec4(p.xy,0.0,p.w));\n"
        "  vec3 q=p.xyw;\n"
        "  if((f&1u)!=0u){vec2 sz=(f&2u)!=0u?vec2(textureSize(sc,0)):vec2(textureSize(s2,0));q.xy/=sz/texScale[stage];}\n"
        "  if((f&2u)!=0u)return texture(sc,remap2DToCube(q));\n"
        "  return texture(s2,q.xy/max(q.z,1e-15));\n"
        " }\n"
        " if(mode==2u){\n"
        "  if((f&4u)!=0u)return textureProj(s3,p);\n"
        "  if((f&1u)!=0u)p=vec4(p.xy/(textureSize(s2,0)/texScale[stage]),0.0,p.w);\n"
        "  return textureProj(s2,p);\n"
        " }\n"
        " if((f&2u)!=0u)return texture(sc,p.xyz);\n"
        " return texture(s2,remapCubeTo2D(p.xyz));\n"
        "}\n");
    for (unsigned i = 0; i < 4; i++) {
        mstring_append_fmt(preflight,
            "uniform sampler2D fsTex2D%u;\nuniform sampler3D fsTex3D%u;\nuniform samplerCube fsTexCube%u;\n",i,i,i);
        mstring_append_fmt(vars,
            "uint fsMode%u=(fsControl[0].y>>%uu)&31u;\n"
            "vec4 t%u=fsTexture(%u,fsMode%u,fsTex2D%u,fsTex3D%u,fsTexCube%u,pT%u);\n",
            i,i*5,i,i,i,i,i,i,i);
        mstring_append_fmt(vars,
            "if(fsMode%u>=1u&&fsMode%u<=3u){\n"
            " if((alphaKillMask&%uu)!=0u&&t%u.a==0.0)fsKill=true;\n"
            " uint ck=(fsControl[1][%u]>>4u)&3u;\n"
            " if(ck!=0u&&check_color_key(t%u,colorKey[%u],colorKeyMask[%u])){\n"
            "  if(ck==3u)fsKill=true;else if(ck==1u)t%u.a=0.0;else t%u=vec4(0.0);\n"
            " }\n}\n",i,i,1u<<i,i,i,i,i,i,i,i);
    }
    (void)ps;
}
