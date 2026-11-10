/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Internal texture IR helpers. All arrays below contain SSA IDs; final shader
 * words/layouts remain the only host allocations and borrowed keys do not escape. */
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
    if(has_border_samples(c->state)){c->border_array=id(c->w);c->uniform_v3=id(c->w);}
    if(has_color_key(c->state)){c->key_array=id(c->w);c->uvec4_t=id(c->w);}
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
    if(c->uniform_v3)op(w,SpvOpTypePointer,3,c->uniform_v3,(uint32_t)SpvStorageClassUniform,c->v3);
    if(c->uvec4_t)op(w,SpvOpTypeVector,3,c->uvec4_t,c->u,4u);
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
