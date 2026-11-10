/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Sample existing depth views. All temporaries are SSA IDs, not texel copies. */
static uint32_t texture_shadow_compare(Context *c,uint32_t depth,uint32_t reference)
{
    const SpvOp comparisons[]={SpvOpNop,SpvOpFOrdLessThan,SpvOpFOrdEqual,SpvOpFOrdLessThanEqual,SpvOpFOrdGreaterThan,SpvOpFUnordNotEqual,SpvOpFOrdGreaterThanEqual};
    uint32_t match=binary(c,comparisons[c->state->shadow_depth_func],c->b,depth,reference);
    return splat(c,c->v4,choose(c,c->f,match,c->cf[F_ONE],c->cf[F_ZERO]),4);
}
static uint32_t texture_shadow(Context *c,uint32_t coord,uint32_t sample,unsigned kind,unsigned unit)
{
    if(f16_shadow(c->state,unit)){
        uint32_t depth=texture_f16_depth(c,coord,sample,unit),reference=c->cf[F_ZERO];
        if(texture_mode(c->state,unit)==PS_TEXTUREMODES_PROJECT3D)reference=ext3(c,GLSLstd450FClamp,c->f,divf(c,extract(c,coord,2),extract(c,coord,3)),c->cf[F_ZERO],c->shadow_max_float);
        return texture_shadow_compare(c,depth,reference);
    }
    uint32_t uv=xy(c,coord);
    if(c->state->rect_tex[unit])uv=texture_rect_uv(c,sample,kind,uv,unit);
    uint32_t lookup=val(c->w,SpvOpCompositeConstruct,c->v3,3,extract(c,uv,0),extract(c,uv,1),extract(c,coord,3));
    uint32_t color=val(c->w,SpvOpImageSampleProjImplicitLod,kind==3?c->uvec4_t:c->v4,2,sample,lookup),depth,max_depth;
    if(kind==3){
        uint32_t raw=val(c->w,SpvOpCompositeExtract,c->u,2,color,0u);
        raw=binary(c,SpvOpShiftRightLogical,c->u,raw,c->ui32[8]);
        depth=divf(c,unary(c,SpvOpConvertUToF,c->f,raw),c->shadow_max24);
        max_depth=c->shadow_max24;
    }else{
        depth=extract(c,color,0);max_depth=0;
        if(texture_mode(c->state,unit)==PS_TEXTUREMODES_PROJECT3D){
            uint32_t wide=binary(c,SpvOpFOrdGreaterThan,c->b,extract(c,color,1),c->cf[F_ZERO]);
            uint32_t floating=binary(c,SpvOpFOrdGreaterThan,c->b,extract(c,color,2),c->cf[F_ZERO]);
            max_depth=choose(c,c->f,wide,c->shadow_max24,choose(c,c->f,floating,c->shadow_max_float,c->cf[F_D16_MAX]));
        }
    }
    uint32_t reference=c->cf[F_ZERO];
    if(texture_mode(c->state,unit)==PS_TEXTUREMODES_PROJECT3D){
        /* Keep the normalization/scale roundtrip used by the reference. */
        depth=mul(c,depth,max_depth);
        reference=ext3(c,GLSLstd450FClamp,c->f,divf(c,extract(c,coord,2),extract(c,coord,3)),c->cf[F_ZERO],max_depth);
    }
    return texture_shadow_compare(c,depth,reference);
}
