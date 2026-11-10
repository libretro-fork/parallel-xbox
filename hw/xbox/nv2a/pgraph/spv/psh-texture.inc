/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Internal texture IR helpers. All arrays below contain SSA IDs; final shader
 * words/layouts remain the only host allocations and borrowed keys do not escape. */
static uint32_t texture_image_type(Context *c,unsigned kind)
{ return kind?c->image_other[kind-1]:c->image_t; }
static uint32_t texture_sampled_type(Context *c,unsigned kind)
{ return kind?c->sampled_other[kind-1]:c->sampled_t; }
static uint32_t texture_sampler_pointer(Context *c,unsigned kind)
{ return kind?c->sampler_other_ptr[kind-1]:c->sampler_ptr; }
/* Keep division in the selected major-axis branch, including reference ties. */
static uint32_t texture_cube_axis(Context *c,uint32_t coord,uint32_t abs_coord,unsigned axis)
{
    uint32_t x=extract(c,coord,0),y=extract(c,coord,1),z=extract(c,coord,2),a,b;
    uint32_t positive=binary(c,SpvOpFOrdGreaterThan,c->b,extract(c,coord,axis),c->cf[F_ZERO]);
    if(axis==0){a=choose(c,c->f,positive,unary(c,SpvOpFNegate,c->f,z),z);b=y;}
    else if(axis==1){a=x;b=choose(c,c->f,positive,unary(c,SpvOpFNegate,c->f,z),z);}
    else{a=choose(c,c->f,positive,x,unary(c,SpvOpFNegate,c->f,x));b=y;}
    uint32_t uv=val(c->w,SpvOpCompositeConstruct,c->v2,2,a,b);
    return binary(c,SpvOpFDiv,c->v2,uv,splat(c,c->v2,extract(c,abs_coord,axis),2));
}
static uint32_t texture_cube_to_2d(Context *c,uint32_t coord)
{
    if(!c->cube_body)return val(c->w,SpvOpFunctionCall,c->v2,2,c->cube_fn,coord);
    uint32_t absolute=ext1(c,GLSLstd450FAbs,c->v3,coord);
    uint32_t x=extract(c,absolute,0),y=extract(c,absolute,1),z=extract(c,absolute,2);
    uint32_t x_major=binary(c,SpvOpLogicalAnd,c->b,binary(c,SpvOpFOrdGreaterThan,c->b,x,y),binary(c,SpvOpFOrdGreaterThan,c->b,x,z));
    uint32_t yes=id(c->w),other=id(c->w),merge=id(c->w);
    op(c->w,SpvOpSelectionMerge,2,merge,0u);op(c->w,SpvOpBranchConditional,3,x_major,yes,other);
    op(c->w,SpvOpLabel,1,yes);uint32_t uv_x=texture_cube_axis(c,coord,absolute,0);
    op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,other);
    uint32_t y_major=binary(c,SpvOpLogicalAnd,c->b,binary(c,SpvOpFOrdGreaterThan,c->b,y,x),binary(c,SpvOpFOrdGreaterThan,c->b,y,z));
    uint32_t yy=id(c->w),zz=id(c->w),inner=id(c->w);
    op(c->w,SpvOpSelectionMerge,2,inner,0u);op(c->w,SpvOpBranchConditional,3,y_major,yy,zz);
    op(c->w,SpvOpLabel,1,yy);uint32_t uv_y=texture_cube_axis(c,coord,absolute,1);
    op(c->w,SpvOpBranch,1,inner);op(c->w,SpvOpLabel,1,zz);uint32_t uv_z=texture_cube_axis(c,coord,absolute,2);
    op(c->w,SpvOpBranch,1,inner);op(c->w,SpvOpLabel,1,inner);
    uint32_t uv_other=val(c->w,SpvOpPhi,c->v2,4,uv_y,yy,uv_z,zz);
    op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,merge);
    return val(c->w,SpvOpPhi,c->v2,4,uv_x,yes,uv_other,inner);
}
static uint32_t texture_2d_to_cube(Context *c,uint32_t coord)
{
    if(!c->project_cube_body)return val(c->w,SpvOpFunctionCall,c->v3,2,c->project_cube_fn,coord);
    uint32_t st=binary(c,SpvOpFDiv,c->v2,xy(c,coord),splat(c,c->v2,extract(c,coord,2),2));
    uint32_t direction=val(c->w,SpvOpCompositeConstruct,c->v3,3,c->cf[F_ONE],extract(c,st,1),unary(c,SpvOpFNegate,c->f,extract(c,st,0)));
    return ext1(c,GLSLstd450Normalize,c->v3,direction);
}
static uint32_t texture_rect_uv(Context *c,uint32_t sample,unsigned kind,uint32_t uv,unsigned unit)
{
    uint32_t image=unary(c,SpvOpImage,texture_image_type(c,kind),sample);
    uint32_t size=val(c->w,SpvOpImageQuerySizeLod,c->iv2,2,image,c->si[0]);
    uint32_t divisor=binary(c,SpvOpFDiv,c->v2,unary(c,SpvOpConvertSToF,c->v2,size),splat(c,c->v2,unif(c,TEX_SCALE,unit),2));
    return binary(c,SpvOpFDiv,c->v2,uv,divisor);
}
static uint32_t texture_dependent(Context *c,uint32_t coord,uint32_t sample,unsigned kind,unsigned unit)
{
    uint32_t source=c->r[PS_REGISTER_T0+texture_input(c->state,unit)];
    unsigned mode=texture_mode(c->state,unit);uint32_t lookup;
    if(!bump_stage(c->state,unit)){
        lookup=mode==PS_TEXTUREMODES_DPNDNT_AR?val(c->w,SpvOpVectorShuffle,c->v2,4,source,source,3u,0u):val(c->w,SpvOpVectorShuffle,c->v2,4,source,source,1u,2u);
    }else{
        unsigned input=texture_input(c->state,unit),signs=c->state->g8b8_signed[input];
        uint32_t dx=(signs&1)?extract(c,c->signed_gb[input],0):val(c->w,SpvOpFunctionCall,c->f,2,c->sign_fn,extract(c,source,2));
        uint32_t dy=(signs&2)?extract(c,c->signed_gb[input],1):val(c->w,SpvOpFunctionCall,c->f,2,c->sign_fn,extract(c,source,1));
        uint32_t delta=val(c->w,SpvOpCompositeConstruct,c->v2,2,dx,dy);
        /* Existing mat2[4] layout is eight padded vec2 columns; no CPU matrix copy. */
        uint32_t matrix=val(c->w,SpvOpCompositeConstruct,c->bump_m2,2,unif(c,BUMP_MAT,unit*2),unif(c,BUMP_MAT,unit*2+1));
        delta=binary(c,SpvOpMatrixTimesVector,c->v2,matrix,delta);
        lookup=binary(c,SpvOpFAdd,c->v2,xy(c,coord),delta);
        if(kind==1)lookup=val(c->w,SpvOpCompositeConstruct,c->v3,3,extract(c,lookup,0),extract(c,lookup,1),extract(c,coord,2));
        else if(c->state->rect_tex[unit])lookup=texture_rect_uv(c,sample,kind,lookup,unit);
    }
    uint32_t color=val(c->w,SpvOpImageSampleImplicitLod,c->v4,2,sample,lookup);
    if(mode==PS_TEXTUREMODES_BUMPENVMAP_LUM){
        uint32_t factor=add(c,mul(c,unif(c,BUMP_SCALE,unit),extract(c,source,0)),unif(c,BUMP_OFFSET,unit));
        color=binary(c,SpvOpFMul,c->v4,color,splat(c,c->v4,factor,4));
    }
    return color;
}
static uint32_t texture_border(Context *c,uint32_t coord,unsigned unit)
{
    if(!c->state->border_adjust[unit])return coord;
    uint32_t xyz_coord=binary(c,SpvOpFMul,c->v3,xyz(c,coord),unif(c,BORDER_LOGICAL,unit));
    xyz_coord=binary(c,SpvOpFAdd,c->v3,xyz_coord,splat(c,c->v3,c->cf[F_FOUR],3));
    xyz_coord=binary(c,SpvOpFMul,c->v3,xyz_coord,unif(c,BORDER_INV,unit));
    return v4(c,extract(c,xyz_coord,0),extract(c,xyz_coord,1),extract(c,xyz_coord,2),extract(c,coord,3));
}
static uint32_t texture_color_key(Context *c,uint32_t color,unsigned unit)
{
    unsigned mode=c->state->colorkey_mode[unit];if(mode==COLOR_KEY_NONE)return color;
    uint32_t scaled=binary(c,SpvOpFAdd,c->v4,
        binary(c,SpvOpFMul,c->v4,color,splat(c,c->v4,c->cf[F_BYTE_MAX],4)),
        splat(c,c->v4,c->cf[F_HALF],4));
    uint32_t bytes=unary(c,SpvOpConvertFToU,c->uvec4_t,scaled),components[4];
    for(unsigned j=0;j<4;j++)components[j]=val(c->w,SpvOpCompositeExtract,c->u,2,bytes,j);
    uint32_t packed=binary(c,SpvOpBitwiseOr,c->u,
        binary(c,SpvOpShiftLeftLogical,c->u,components[3],c->ui32[24]),
        binary(c,SpvOpShiftLeftLogical,c->u,components[0],c->ui32[16]));
    packed=binary(c,SpvOpBitwiseOr,c->u,packed,binary(c,SpvOpShiftLeftLogical,c->u,components[1],c->ui32[8]));
    packed=binary(c,SpvOpBitwiseOr,c->u,packed,components[2]);
    uint32_t mask=unif(c,COLOR_MASK,unit),key=unif(c,COLOR_KEY,unit);
    uint32_t match=binary(c,SpvOpIEqual,c->b,binary(c,SpvOpBitwiseAnd,c->u,packed,mask),binary(c,SpvOpBitwiseAnd,c->u,key,mask));
    if(mode==COLOR_KEY_DISCARD){discard_if(c,match);return color;}
    uint32_t killed=mode==COLOR_KEY_KILL_ALPHA?
        v4(c,extract(c,color,0),extract(c,color,1),extract(c,color,2),c->cf[F_ZERO]):splat(c,c->v4,c->cf[F_ZERO],4);
    return choose(c,c->v4,match,killed,color);
}
static uint32_t texture_convolution(Context *c,uint32_t sample,uint32_t uv,uint32_t q)
{
    /* Convolution uses the reference raw Q, not PROJECT2D's positive clamp. */
    uv=binary(c,SpvOpFDiv,c->v2,uv,splat(c,c->v2,q,2));
    uint32_t image=unary(c,SpvOpImage,c->image_t,sample);
    uint32_t size=val(c->w,SpvOpImageQuerySizeLod,c->iv2,2,image,c->si[0]);
    uint32_t step=binary(c,SpvOpFDiv,c->v2,splat(c,c->v2,c->cf[F_ONE],2),unary(c,SpvOpConvertSToF,c->v2,size));
    uint32_t zero=splat(c,c->v4,c->cf[F_ZERO],4);
    uint32_t entry=c->w->block,header=id(c->w),body=id(c->w),cont=id(c->w),merge=id(c->w);
    uint32_t index=id(c->w),next=id(c->w),sum=id(c->w),added=id(c->w);
    op(c->w,SpvOpBranch,1,header);op(c->w,SpvOpLabel,1,header);
    op(c->w,SpvOpPhi,6,c->i,index,c->si[0],entry,next,cont);
    op(c->w,SpvOpPhi,6,c->v4,sum,zero,entry,added,cont);
    uint32_t active=binary(c,SpvOpSLessThan,c->b,index,c->si[9]);
    op(c->w,SpvOpLoopMerge,3,merge,cont,0u);op(c->w,SpvOpBranchConditional,3,active,body,merge);
    op(c->w,SpvOpLabel,1,body);
    uint32_t offset=unary(c,SpvOpLoad,c->v2,val(c->w,SpvOpAccessChain,c->conv_offset_element,2,c->conv_offsets,index));
    uint32_t weight=unary(c,SpvOpLoad,c->f,val(c->w,SpvOpAccessChain,c->conv_element,2,c->conv_weights,index));
    uint32_t coord=binary(c,SpvOpFAdd,c->v2,uv,binary(c,SpvOpFMul,c->v2,offset,step));
    uint32_t texel=val(c->w,SpvOpImageSampleImplicitLod,c->v4,2,sample,coord);
    uint32_t weighted=binary(c,SpvOpFMul,c->v4,texel,splat(c,c->v4,weight,4));
    op(c->w,SpvOpFAdd,4,c->v4,added,sum,weighted);
    op(c->w,SpvOpBranch,1,cont);op(c->w,SpvOpLabel,1,cont);
    op(c->w,SpvOpIAdd,4,c->i,next,index,c->si[1]);op(c->w,SpvOpBranch,1,header);op(c->w,SpvOpLabel,1,merge);
    return sum;
}
static void texture_allocate(Context *c)
{
    for(unsigned kind=1;kind<4;kind++)if(has_sampler_kind(c->state,kind)){
        c->image_other[kind-1]=id(c->w);c->sampled_other[kind-1]=id(c->w);c->sampler_other_ptr[kind-1]=id(c->w);
    }
    for(unsigned j=0;j<4;j++){
        if(cube_stage(c->state,j) && !c->state->tex_cubemap[j] && !c->cube_fn){c->cube_fn=id(c->w);c->cube_fn_type=id(c->w);}
        if(texture_mode(c->state,j)==PS_TEXTUREMODES_PROJECT2D && sampler_kind(c->state,j)==2 && !c->project_cube_fn){c->project_cube_fn=id(c->w);c->project_cube_type=id(c->w);}
    }
    if(has_bump(c->state)){
        c->bump_array=id(c->w);c->bump_m2=id(c->w);c->uniform_v2=id(c->w);
    }
    if(has_bump(c->state) || has_signed_gb(c->state)){
        c->sign_fn=id(c->w);c->sign_type=id(c->w);
        for(unsigned j=0;j<3;j++)c->sign_constants[j]=id(c->w);
    }
    if(has_border_samples(c->state)){c->border_array=id(c->w);c->uniform_v3=id(c->w);}
    if(has_color_key(c->state)){c->key_array=id(c->w);c->uvec4_t=id(c->w);}
    if((has_sampler_kind(c->state,3) || has_f16_shadow(c->state)) && !c->uvec4_t)c->uvec4_t=id(c->w);
    if(has_f16_shadow(c->state)){c->f16_bvec4=id(c->w);c->f16_base=id(c->w);}
    if(shadow_constant_needed(c->state,false))c->shadow_max24=id(c->w);
    if(shadow_constant_needed(c->state,true))c->shadow_max_float=id(c->w);
    if(has_convolution(c->state)){
        uint32_t *ids[]={&c->conv_array,&c->conv_offsets_array,&c->conv_ptr,&c->conv_offsets_ptr,
            &c->conv_element,&c->conv_offset_element,&c->conv_weights,&c->conv_offsets,
            &c->conv_weight_init,&c->conv_offset_init,&c->conv_negative};
        for(unsigned j=0;j<sizeof(ids)/sizeof(ids[0]);j++)*ids[j]=id(c->w);
        for(unsigned j=0;j<3;j++)c->conv_weight[j]=id(c->w);
        for(unsigned j=0;j<9;j++)c->conv_vec2[j]=id(c->w);
    }
}
static void texture_types(Context *c)
{
    Writer *w=c->w;
    for(unsigned kind=1;kind<4;kind++)if(c->image_other[kind-1]){
        op(w,SpvOpTypeImage,8,c->image_other[kind-1],kind==3?c->u:c->f,(uint32_t)(kind==1?SpvDim3D:kind==2?SpvDimCube:SpvDim2D),0u,0u,0u,1u,(uint32_t)SpvImageFormatUnknown);
        op(w,SpvOpTypeSampledImage,2,c->sampled_other[kind-1],c->image_other[kind-1]);
        op(w,SpvOpTypePointer,3,c->sampler_other_ptr[kind-1],(uint32_t)SpvStorageClassUniformConstant,c->sampled_other[kind-1]);
    }
    if(c->shadow_max24){float x=16777215;uint32_t bits;memcpy(&bits,&x,4);op(w,SpvOpConstant,3,c->f,c->shadow_max24,bits);}
    if(c->shadow_max_float){float x=511.9375f;uint32_t bits;memcpy(&bits,&x,4);op(w,SpvOpConstant,3,c->f,c->shadow_max_float,bits);}
    if(c->bump_m2){
        op(w,SpvOpTypeMatrix,3,c->bump_m2,c->v2,2u);
        op(w,SpvOpTypePointer,3,c->uniform_v2,(uint32_t)SpvStorageClassUniform,c->v2);
    }
    if(c->sign_fn){
        op(w,SpvOpTypeFunction,3,c->sign_type,c->f,c->f);
        const float values[]={127,128,256};
        for(unsigned j=0;j<3;j++){uint32_t bits;memcpy(&bits,values+j,4);op(w,SpvOpConstant,3,c->f,c->sign_constants[j],bits);}
    }
    if(c->cube_fn)op(w,SpvOpTypeFunction,3,c->cube_fn_type,c->v2,c->v3);
    if(c->project_cube_fn)op(w,SpvOpTypeFunction,3,c->project_cube_type,c->v3,c->v3);
    if(c->uniform_v3)op(w,SpvOpTypePointer,3,c->uniform_v3,(uint32_t)SpvStorageClassUniform,c->v3);
    if(c->uvec4_t)op(w,SpvOpTypeVector,3,c->uvec4_t,c->u,4u);
    if(c->f16_bvec4)op(w,SpvOpTypeVector,3,c->f16_bvec4,c->b,4u);
    if(c->f16_base)op(w,SpvOpConstant,3,c->u,c->f16_base,0x3c000000u);
    if(!has_convolution(c->state))return;
    op(w,SpvOpTypeArray,3,c->conv_array,c->f,c->ui32[9]);op(w,SpvOpTypeArray,3,c->conv_offsets_array,c->v2,c->ui32[9]);
    op(w,SpvOpTypePointer,3,c->conv_ptr,(uint32_t)SpvStorageClassPrivate,c->conv_array);
    op(w,SpvOpTypePointer,3,c->conv_offsets_ptr,(uint32_t)SpvStorageClassPrivate,c->conv_offsets_array);
    op(w,SpvOpTypePointer,3,c->conv_element,(uint32_t)SpvStorageClassPrivate,c->f);
    op(w,SpvOpTypePointer,3,c->conv_offset_element,(uint32_t)SpvStorageClassPrivate,c->v2);
    const float constants[]={.0625f,.125f,.25f,-1};
    for(unsigned j=0;j<4;j++){uint32_t bits;memcpy(&bits,constants+j,4);op(w,SpvOpConstant,3,c->f,j<3?c->conv_weight[j]:c->conv_negative,bits);}
    uint32_t values[]={c->conv_negative,c->cf[F_ZERO],c->cf[F_ONE]};
    for(unsigned j=0;j<9;j++)op(w,SpvOpConstantComposite,4,c->v2,c->conv_vec2[j],values[j%3],values[j/3]);
    word(w,(12u<<16)|SpvOpConstantComposite);word(w,c->conv_array);word(w,c->conv_weight_init);
    for(unsigned j=0;j<9;j++)word(w,c->conv_weight[j==4?2:(j&1)?1:0]);
    word(w,(12u<<16)|SpvOpConstantComposite);word(w,c->conv_offsets_array);word(w,c->conv_offset_init);
    for(unsigned j=0;j<9;j++)word(w,c->conv_vec2[j]);
    op(w,SpvOpVariable,4,c->conv_ptr,c->conv_weights,(uint32_t)SpvStorageClassPrivate,c->conv_weight_init);
    op(w,SpvOpVariable,4,c->conv_offsets_ptr,c->conv_offsets,(uint32_t)SpvStorageClassPrivate,c->conv_offset_init);
}

/* Share helpers across units instead of cloning their branches/arithmetic. */
static void texture_helpers(Context *c)
{
    if(c->sign_fn){
        op(c->w,SpvOpFunction,4,c->f,c->sign_fn,0u,c->sign_type);
        uint32_t input=id(c->w);op(c->w,SpvOpFunctionParameter,2,c->f,input);op(c->w,SpvOpLabel,1,id(c->w));
        uint32_t scaled=mul(c,input,c->cf[F_BYTE_MAX]);
        uint32_t high=binary(c,SpvOpFOrdGreaterThanEqual,c->b,scaled,c->sign_constants[1]);
        uint32_t yes=id(c->w),no=id(c->w),merge=id(c->w);
        op(c->w,SpvOpSelectionMerge,2,merge,0u);op(c->w,SpvOpBranchConditional,3,high,yes,no);
        op(c->w,SpvOpLabel,1,yes);uint32_t negative=divf(c,sub(c,scaled,c->sign_constants[2]),c->sign_constants[0]);
        op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,no);uint32_t positive=divf(c,scaled,c->sign_constants[0]);
        op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,merge);
        uint32_t value=val(c->w,SpvOpPhi,c->f,4,negative,yes,positive,no);
        op(c->w,SpvOpReturnValue,1,value);op(c->w,SpvOpFunctionEnd,0);
    }
    if(c->cube_fn){
        op(c->w,SpvOpFunction,4,c->v2,c->cube_fn,0u,c->cube_fn_type);
        uint32_t coord=id(c->w);op(c->w,SpvOpFunctionParameter,2,c->v3,coord);op(c->w,SpvOpLabel,1,id(c->w));
        c->cube_body=true;uint32_t value=texture_cube_to_2d(c,coord);c->cube_body=false;
        op(c->w,SpvOpReturnValue,1,value);op(c->w,SpvOpFunctionEnd,0);
    }
    if(c->project_cube_fn){
        op(c->w,SpvOpFunction,4,c->v3,c->project_cube_fn,0u,c->project_cube_type);
        uint32_t coord=id(c->w);op(c->w,SpvOpFunctionParameter,2,c->v3,coord);op(c->w,SpvOpLabel,1,id(c->w));
        c->project_cube_body=true;uint32_t value=texture_2d_to_cube(c,coord);c->project_cube_body=false;
        op(c->w,SpvOpReturnValue,1,value);op(c->w,SpvOpFunctionEnd,0);
    }
}
