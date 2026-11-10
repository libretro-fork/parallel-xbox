/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Internal dot-stage IR. All cached values are bounded SSA IDs, not payloads. */
static uint32_t dot_coord(Context *c,unsigned unit)
{
    if(unit==3 && c->state->point_sprite){
        uint32_t p=unary(c,SpvOpLoad,c->v2,c->point_coord);
        return v4(c,extract(c,p,0),extract(c,p,1),c->cf[F_ONE],c->cf[F_ONE]);
    }
    return ui_clamp_coord(c,unary(c,SpvOpLoad,c->v4,c->tex_inputs[unit]),unit);
}
static uint32_t dot_mapped(Context *c,uint32_t input,unsigned mapping)
{
    /* Hemisphere mappings still return RGB in the production reference. */
    return !mapping || mapping>=5?xyz(c,input):val(c->w,SpvOpFunctionCall,c->v3,2,c->dot_maps[mapping-1],input);
}
static uint32_t texture_dot(Context *c,uint32_t coord,unsigned unit)
{
    uint32_t mapped=dot_mapped(c,c->r[PS_REGISTER_T0+texture_input(c->state,unit)],dot_mapping(c->state,unit));
    return binary(c,SpvOpDot,c->f,xyz(c,coord),mapped);
}
static void texture_dot_depth(Context *c,unsigned unit)
{
    uint32_t nonzero=binary(c,SpvOpFUnordNotEqual,c->b,c->dots[unit],c->cf[F_ZERO]);
    uint32_t yes=id(c->w),no=id(c->w),merge=id(c->w);
    op(c->w,SpvOpSelectionMerge,2,merge,0u);op(c->w,SpvOpBranchConditional,3,nonzero,yes,no);
    op(c->w,SpvOpLabel,1,yes);uint32_t z=divf(c,c->dots[unit-1],c->dots[unit]);
    op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,no);op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,merge);
    c->z=val(c->w,SpvOpPhi,c->f,4,z,yes,c->cf[F_FLOAT_MAX],no);
    uint32_t low=extract(c,c->clip,2),high=extract(c,c->clip,3);
    if(c->state->depth_clipping)discard_if(c,binary(c,SpvOpLogicalOr,c->b,binary(c,SpvOpFOrdLessThan,c->b,c->z,low),binary(c,SpvOpFOrdLessThan,c->b,high,c->z)));
    else{c->precise=true;c->z=ext3(c,GLSLstd450FClamp,c->f,c->z,low,high);c->precise=false;}
}
static uint32_t dot_border(Context *c,uint32_t coord,unsigned unit)
{
    if(!c->state->border_adjust[unit])return coord;
    coord=binary(c,SpvOpFMul,c->v3,coord,unif(c,BORDER_LOGICAL,unit));
    coord=binary(c,SpvOpFAdd,c->v3,coord,splat(c,c->v3,c->cf[F_FOUR],3));
    return binary(c,SpvOpFMul,c->v3,coord,unif(c,BORDER_INV,unit));
}
static uint32_t texture_dot_sample(Context *c,uint32_t coord,uint32_t sample,unsigned kind,unsigned unit)
{
    unsigned mode=texture_mode(c->state,unit);c->dots[unit]=texture_dot(c,coord,unit);uint32_t lookup;
    if(mode==PS_TEXTUREMODES_DOT_ST){
        lookup=val(c->w,SpvOpCompositeConstruct,c->v2,2,c->dots[unit-1],c->dots[unit]);
    }else{
        uint32_t last=c->dots[unit];
        if(mode==PS_TEXTUREMODES_DOT_RFLCT_DIFF){
            uint32_t future=dot_coord(c,unit+1),mapped=dot_mapped(c,c->r[PS_REGISTER_T0+texture_input(c->state,unit+1)],dot_mapping(c->state,unit+1));
            last=binary(c,SpvOpDot,c->f,xyz(c,future),mapped);
            lookup=val(c->w,SpvOpCompositeConstruct,c->v3,3,c->dots[unit-1],c->dots[unit],last);
        }else lookup=val(c->w,SpvOpCompositeConstruct,c->v3,3,c->dots[unit-2],c->dots[unit-1],last);
        if(mode==PS_TEXTUREMODES_DOT_RFLCT_SPEC){
            uint32_t eye=val(c->w,SpvOpCompositeConstruct,c->v3,3,extract(c,dot_coord(c,unit-2),3),extract(c,dot_coord(c,unit-1),3),extract(c,coord,3));
            uint32_t numerator=binary(c,SpvOpDot,c->f,lookup,eye),denominator=binary(c,SpvOpDot,c->f,lookup,lookup);
            lookup=binary(c,SpvOpFMul,c->v3,splat(c,c->v3,c->cf[F_TWO],3),lookup);
            lookup=binary(c,SpvOpFMul,c->v3,lookup,splat(c,c->v3,numerator,3));
            lookup=binary(c,SpvOpFSub,c->v3,binary(c,SpvOpFDiv,c->v3,lookup,splat(c,c->v3,denominator,3)),eye);
        }
        lookup=dot_border(c,lookup,unit);
        if(cube_stage(c->state,unit)){if(kind==0)lookup=texture_cube_to_2d(c,lookup);}
        else if(kind==0)lookup=xy(c,lookup);
    }
    if(c->state->rect_tex[unit] && !cube_stage(c->state,unit))lookup=texture_rect_uv(c,sample,kind,lookup,unit);
    /* Keep zero bias explicit for integer-packed HILO lookup chains. The
     * operand-free form selects a different mip on the RTX regression corpus;
     * bias zero preserves implicit derivatives and sampler filtering. */
    if(dot_mapping_used(c->state,4))return val(c->w,SpvOpImageSampleImplicitLod,c->v4,4,sample,lookup,(uint32_t)SpvImageOperandsBiasMask,c->cf[F_ZERO]);
    return val(c->w,SpvOpImageSampleImplicitLod,c->v4,2,sample,lookup);
}
static void dot_allocate(Context *c)
{
    if(dot_mapping_used(c->state,3) && !c->sign_fn){
        c->sign_fn=id(c->w);c->sign_type=id(c->w);for(unsigned j=0;j<3;j++)c->sign_constants[j]=id(c->w);
    }
    for(unsigned j=0;j<2;j++)if(dot_mapping_used(c->state,j+1))c->dot_sign[j]=id(c->w);
    if(c->dot_sign[0] || c->dot_sign[1]){
        c->dot_sign_type=c->sign_type?c->sign_type:id(c->w);for(unsigned j=0;j<4;j++)c->dot_constants[j]=id(c->w);
    }
    for(unsigned j=0;j<4;j++)if(dot_mapping_used(c->state,j+1))c->dot_maps[j]=id(c->w);
    if(c->dot_maps[0] || c->dot_maps[1] || c->dot_maps[2] || c->dot_maps[3])c->dot_map_type=id(c->w);
}
static void dot_types(Context *c)
{
    if(c->dot_sign_type){
        if(c->dot_sign_type!=c->sign_type)op(c->w,SpvOpTypeFunction,3,c->dot_sign_type,c->f,c->f);
        const float values[]={127,128,127.5f,255.5f};
        for(unsigned j=0;j<4;j++){uint32_t bits;memcpy(&bits,values+j,4);op(c->w,SpvOpConstant,3,c->f,c->dot_constants[j],bits);}
    }
    if(c->dot_map_type)op(c->w,SpvOpTypeFunction,3,c->dot_map_type,c->v3,c->v4);
}
static void dot_helpers(Context *c)
{
    for(unsigned j=0;j<2;j++)if(c->dot_sign[j]){
        op(c->w,SpvOpFunction,4,c->f,c->dot_sign[j],0u,c->dot_sign_type);
        uint32_t input=id(c->w);op(c->w,SpvOpFunctionParameter,2,c->f,input);op(c->w,SpvOpLabel,1,id(c->w));
        uint32_t scaled=mul(c,input,c->cf[F_BYTE_MAX]),result;
        if(!j)result=divf(c,sub(c,scaled,c->dot_constants[1]),c->dot_constants[0]);
        else{
            uint32_t high=binary(c,SpvOpFOrdGreaterThanEqual,c->b,scaled,c->dot_constants[1]),yes=id(c->w),no=id(c->w),merge=id(c->w);
            op(c->w,SpvOpSelectionMerge,2,merge,0u);op(c->w,SpvOpBranchConditional,3,high,yes,no);
            op(c->w,SpvOpLabel,1,yes);uint32_t negative=divf(c,sub(c,scaled,c->dot_constants[3]),c->dot_constants[2]);
            op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,no);uint32_t positive=divf(c,add(c,scaled,c->cf[F_HALF]),c->dot_constants[2]);
            op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,merge);result=val(c->w,SpvOpPhi,c->f,4,negative,yes,positive,no);
        }
        op(c->w,SpvOpReturnValue,1,result);op(c->w,SpvOpFunctionEnd,0);
    }
    for(unsigned j=0;j<4;j++)if(c->dot_maps[j]){
        op(c->w,SpvOpFunction,4,c->v3,c->dot_maps[j],0u,c->dot_map_type);
        uint32_t input=id(c->w);op(c->w,SpvOpFunctionParameter,2,c->v4,input);op(c->w,SpvOpLabel,1,id(c->w));uint32_t components[3];
        if(j<3)for(unsigned k=0;k<3;k++)components[k]=val(c->w,SpvOpFunctionCall,c->f,2,j<2?c->dot_sign[j]:c->sign_fn,extract(c,input,k));
        else{
            const unsigned channels[2][2]={{3,0},{1,2}};
            for(unsigned k=0;k<2;k++){
                uint32_t hi=unary(c,SpvOpConvertFToU,c->u,mul(c,extract(c,input,channels[k][0]),c->cf[F_BYTE_MAX]));
                uint32_t lo=unary(c,SpvOpConvertFToU,c->u,mul(c,extract(c,input,channels[k][1]),c->cf[F_BYTE_MAX]));
                uint32_t packed=binary(c,SpvOpBitwiseOr,c->u,binary(c,SpvOpShiftLeftLogical,c->u,hi,c->ui32[8]),lo);
                components[k]=divf(c,unary(c,SpvOpConvertUToF,c->f,packed),c->cf[F_D16_MAX]);
            }
            components[2]=c->cf[F_ONE];
        }
        uint32_t mapped=val(c->w,SpvOpCompositeConstruct,c->v3,3,components[0],components[1],components[2]);
        op(c->w,SpvOpReturnValue,1,mapped);op(c->w,SpvOpFunctionEnd,0);
    }
}
