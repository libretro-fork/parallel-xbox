/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Decode before filtering. Only bounded SSA IDs are lent between builders. */
static uint32_t f16_raw(Context *c,uint32_t sample,uint32_t uv,uint32_t size,bool linear,uint32_t *fraction)
{
    uint32_t half=splat(c,c->v2,c->cf[F_HALF],2);
    uint32_t pos=binary(c,SpvOpFMul,c->v2,uv,size),lookup;
    if(linear){
        pos=binary(c,SpvOpFSub,c->v2,pos,half);
        *fraction=ext1(c,GLSLstd450Fract,c->v2,pos);
        lookup=binary(c,SpvOpFDiv,c->v2,binary(c,SpvOpFAdd,c->v2,ext1(c,GLSLstd450Floor,c->v2,pos),splat(c,c->v2,c->cf[F_ONE],2)),size);
        uint32_t raw=val(c->w,SpvOpImageGather,c->v4,3,sample,lookup,c->si[0]);
        return val(c->w,SpvOpVectorShuffle,c->v4,6,raw,raw,3u,2u,0u,1u);
    }
    *fraction=splat(c,c->v2,c->cf[F_ZERO],2);
    lookup=binary(c,SpvOpFDiv,c->v2,binary(c,SpvOpFAdd,c->v2,ext1(c,GLSLstd450Floor,c->v2,pos),half),size);
    uint32_t raw=val(c->w,SpvOpImageSampleExplicitLod,c->v4,4,sample,lookup,(uint32_t)SpvImageOperandsLodMask,c->cf[F_ZERO]);
    return splat(c,c->v4,extract(c,raw,0),4);
}
static uint32_t texture_f16_depth(Context *c,uint32_t coord,uint32_t sample,unsigned unit)
{
    uint32_t image=unary(c,SpvOpImage,c->image_t,sample);
    uint32_t size=unary(c,SpvOpConvertSToF,c->v2,val(c->w,SpvOpImageQuerySizeLod,c->iv2,2,image,c->si[0]));
    uint32_t uv=xy(c,coord),scale=unif(c,TEX_SCALE,unit);
    if(c->state->rect_tex[unit]){
        uint32_t divisor=binary(c,SpvOpFDiv,c->v2,size,splat(c,c->v2,ext1(c,GLSLstd450FAbs,c->f,scale),2));
        uv=binary(c,SpvOpFDiv,c->v2,uv,divisor);
    }
    uv=binary(c,SpvOpFDiv,c->v2,uv,splat(c,c->v2,extract(c,coord,3),2));
    unsigned flags=c->state->shadow_map[unit];bool min_linear=(flags&4)!=0,mag_linear=(flags&8)!=0;
    uint32_t raw,fraction;
    if(min_linear==mag_linear)raw=f16_raw(c,sample,uv,size,min_linear,&fraction);
    else{
        uint32_t lod=extract(c,val(c->w,SpvOpImageQueryLod,c->v2,2,sample,uv),1);
        uint32_t linear=binary(c,min_linear?SpvOpFOrdGreaterThan:SpvOpFOrdLessThanEqual,c->b,lod,c->cf[F_ZERO]);
        uint32_t yes=id(c->w),no=id(c->w),merge=id(c->w),fracs[2],values[2];
        op(c->w,SpvOpSelectionMerge,2,merge,0u);op(c->w,SpvOpBranchConditional,3,linear,yes,no);
        op(c->w,SpvOpLabel,1,yes);values[0]=f16_raw(c,sample,uv,size,true,&fracs[0]);op(c->w,SpvOpBranch,1,merge);
        op(c->w,SpvOpLabel,1,no);values[1]=f16_raw(c,sample,uv,size,false,&fracs[1]);op(c->w,SpvOpBranch,1,merge);
        op(c->w,SpvOpLabel,1,merge);raw=val(c->w,SpvOpPhi,c->v4,4,values[0],yes,values[1],no);fraction=val(c->w,SpvOpPhi,c->v2,4,fracs[0],yes,fracs[1],no);
    }
    uint32_t encoded=binary(c,SpvOpFOrdLessThan,c->b,scale,c->cf[F_ZERO]);
    uint32_t yes=id(c->w),no=id(c->w),merge=id(c->w);
    op(c->w,SpvOpSelectionMerge,2,merge,0u);op(c->w,SpvOpBranchConditional,3,encoded,yes,no);op(c->w,SpvOpLabel,1,yes);
    uint32_t bits=unary(c,SpvOpConvertFToU,c->uvec4_t,ext1(c,GLSLstd450Round,c->v4,binary(c,SpvOpFMul,c->v4,raw,splat(c,c->v4,c->cf[F_D16_MAX],4))));
    uint32_t shifted=binary(c,SpvOpShiftLeftLogical,c->uvec4_t,bits,splat(c,c->uvec4_t,c->ui32[11],4));
    uint32_t decoded=unary(c,SpvOpBitcast,c->v4,binary(c,SpvOpIAdd,c->uvec4_t,shifted,splat(c,c->uvec4_t,c->f16_base,4)));
    uint32_t zero=binary(c,SpvOpIEqual,c->f16_bvec4,bits,splat(c,c->uvec4_t,c->ui32[0],4));
    decoded=choose(c,c->v4,zero,splat(c,c->v4,c->cf[F_ZERO],4),decoded);op(c->w,SpvOpBranch,1,merge);
    op(c->w,SpvOpLabel,1,no);uint32_t normalized=binary(c,SpvOpFMul,c->v4,raw,splat(c,c->v4,c->shadow_max_float,4));op(c->w,SpvOpBranch,1,merge);
    op(c->w,SpvOpLabel,1,merge);uint32_t value=val(c->w,SpvOpPhi,c->v4,4,decoded,yes,normalized,no);
    uint32_t x=extract(c,fraction,0),y=extract(c,fraction,1);
    uint32_t a=ext3(c,GLSLstd450FMix,c->f,extract(c,value,0),extract(c,value,1),x),b=ext3(c,GLSLstd450FMix,c->f,extract(c,value,2),extract(c,value,3),x);
    return ext3(c,GLSLstd450FMix,c->f,a,b,y);
}
