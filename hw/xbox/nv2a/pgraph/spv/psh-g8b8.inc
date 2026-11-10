/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Signed texel reconstruction uses existing image owners and bounded SSA IDs. */
static uint32_t gb_sign(Context *c,uint32_t input)
{ return val(c->w,SpvOpFunctionCall,c->f,2,c->sign_fn,input); }
static uint32_t gb_pair(Context *c,uint32_t color)
{ return val(c->w,SpvOpCompositeConstruct,c->v2,2,gb_sign(c,extract(c,color,2)),gb_sign(c,extract(c,color,1))); }
static uint32_t gb_mix(Context *c,uint32_t a,uint32_t b,uint32_t weight)
{ return ext3(c,GLSLstd450FMix,c->v2,a,b,splat(c,c->v2,weight,2)); }
static uint32_t gb_fetch(Context *c,uint32_t image,uint32_t base,uint32_t size,uint32_t level,unsigned tap,const bool edge[2],const bool mirror[2])
{
    uint32_t axis[2];
    for(unsigned j=0;j<2;j++){
        uint32_t coordinate=add(c,extract(c,base,j),c->cf[(tap&(1u<<j))?F_ONE:F_ZERO]);
        uint32_t extent=extract(c,size,j);
        if(mirror[j]){
            uint32_t end=sub(c,extent,c->cf[F_HALF]);
            coordinate=sub(c,end,ext1(c,GLSLstd450FAbs,c->f,sub(c,binary(c,SpvOpFMod,c->f,coordinate,mul(c,c->cf[F_TWO],extent)),end)));
        }else if(edge[j])coordinate=ext3(c,GLSLstd450FClamp,c->f,coordinate,c->cf[F_ZERO],sub(c,extent,c->cf[F_ONE]));
        else coordinate=binary(c,SpvOpFMod,c->f,coordinate,extent);
        axis[j]=unary(c,SpvOpConvertFToS,c->i,coordinate);
    }
    uint32_t texel=val(c->w,SpvOpCompositeConstruct,c->iv2,2,axis[0],axis[1]);
    return gb_pair(c,val(c->w,SpvOpImageFetch,c->v4,4,image,texel,(uint32_t)SpvImageOperandsLodMask,level));
}
static uint32_t gb_level(Context *c,uint32_t image,uint32_t uv,uint32_t level,bool linear,uint32_t mixed,const bool edge[2],const bool mirror[2])
{
    uint32_t size=unary(c,SpvOpConvertSToF,c->v2,val(c->w,SpvOpImageQuerySizeLod,c->iv2,2,image,level));
    uint32_t pos=binary(c,SpvOpFMul,c->v2,uv,size);
    if(linear){
        uint32_t offset=mixed?choose(c,c->f,mixed,c->cf[F_HALF],c->cf[F_ZERO]):c->cf[F_HALF];
        pos=binary(c,SpvOpFSub,c->v2,pos,splat(c,c->v2,offset,2));
    }
    uint32_t base=ext1(c,GLSLstd450Floor,c->v2,pos),taps[4];
    taps[0]=gb_fetch(c,image,base,size,level,0,edge,mirror);
    if(!linear)return taps[0];
    uint32_t yes=0,no=0,merge=0;
    if(mixed){
        yes=id(c->w);no=id(c->w);merge=id(c->w);
        op(c->w,SpvOpSelectionMerge,2,merge,0u);op(c->w,SpvOpBranchConditional,3,mixed,yes,no);op(c->w,SpvOpLabel,1,yes);
    }
    for(unsigned j=1;j<4;j++)taps[j]=gb_fetch(c,image,base,size,level,j,edge,mirror);
    uint32_t weight=ext1(c,GLSLstd450Fract,c->v2,pos);
    uint32_t value=gb_mix(c,gb_mix(c,taps[0],taps[1],extract(c,weight,0)),gb_mix(c,taps[2],taps[3],extract(c,weight,0)),extract(c,weight,1));
    if(mixed){
        uint32_t last=c->w->block;op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,no);op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,merge);
        value=val(c->w,SpvOpPhi,c->v2,4,value,last,taps[0],no);
    }
    return value;
}
static uint32_t gb_mips(Context *c,uint32_t coord,uint32_t sample,unsigned unit)
{
    const PshState *s=c->state;unsigned signs=s->g8b8_signed[unit],bit=1u<<unit;
    bool mixed=(s->g8b8_mip_flags&bit)!=0,shared=(s->g8b8_mip_flags&(bit<<4))!=0;
    bool edge[2]={shared || (s->g8b8_mip_clamp_u&bit),shared || (s->g8b8_mip_clamp_v&bit)};
    bool mirror[2]={(s->g8b8_mip_mirror&bit)!=0,(s->g8b8_mip_mirror&(bit<<4))!=0};
    unsigned minimum=((s->g8b8_mip_clamp_u>>(unit+4))&1u)|(((s->g8b8_mip_clamp_v>>(unit+4))&1u)<<1),address_u,address_v;
    bool extended=psh_g8b8_get_extended_lod(s,unit,&minimum,&address_u,&address_v);
    if(extended){mixed=false;edge[0]=address_u==3;edge[1]=address_v==3;mirror[0]=address_u==2;mirror[1]=address_v==2;}
    bool linear=(signs&16) || mixed,trilinear=(signs&64)!=0;
    uint32_t q=ext2(c,GLSLstd450FMax,c->f,extract(c,coord,3),c->min_q);
    uint32_t uv=binary(c,SpvOpFDiv,c->v2,xy(c,coord),splat(c,c->v2,q,2)),lod,mixed_linear=0;
    if(mixed){
        uint32_t query=val(c->w,SpvOpImageQueryLod,c->v2,2,sample,uv);
        lod=(signs&32)?c->cf[F_ZERO]:ext2(c,GLSLstd450FMax,c->f,extract(c,query,0),c->cf[F_ZERO]);
        mixed_linear=binary(c,(signs&16)?SpvOpFOrdGreaterThan:SpvOpFOrdLessThanEqual,c->b,extract(c,query,1),c->cf[F_ZERO]);
    }else if((signs&32) && !minimum)lod=c->cf[F_ZERO];
    else{
        uint32_t lower=minimum?unary(c,SpvOpConvertUToF,c->f,c->ui32[minimum]):c->cf[F_ZERO];
        lod=ext2(c,GLSLstd450FMax,c->f,extract(c,val(c->w,SpvOpImageQueryLod,c->v2,2,sample,uv),0),lower);
    }
    uint32_t image=unary(c,SpvOpImage,c->image_t,sample),values[2];
    for(unsigned j=0;j<(trilinear?2u:1u);j++){
        uint32_t level=unary(c,SpvOpConvertFToS,c->i,ext1(c,j?GLSLstd450Ceil:GLSLstd450Floor,c->f,lod));
        values[j]=gb_level(c,image,uv,level,linear,mixed_linear,edge,mirror);
    }
    return trilinear?gb_mix(c,values[0],values[1],ext1(c,GLSLstd450Fract,c->f,lod)):values[0];
}
static uint32_t gb_stage(Context *c,uint32_t color,uint32_t coord,uint32_t sample,unsigned unit)
{
    unsigned signs=c->state->g8b8_signed[unit];
    if((signs&128) && ((signs&(16|64)) || (c->state->g8b8_mip_flags&(1u<<unit))))return gb_mips(c,coord,sample,unit);
    uint32_t point=gb_pair(c,color);
    if(!(signs&(16|64)))return point;
    uint32_t q=ext2(c,GLSLstd450FMax,c->f,extract(c,coord,3),c->min_q);
    uint32_t uv=binary(c,SpvOpFDiv,c->v2,xy(c,coord),splat(c,c->v2,q,2));
    if(c->state->rect_tex[unit])uv=texture_rect_uv(c,sample,0,uv,unit);
    uint32_t image=unary(c,SpvOpImage,c->image_t,sample),size=unary(c,SpvOpConvertSToF,c->v2,val(c->w,SpvOpImageQuerySizeLod,c->iv2,2,image,c->si[0]));
    uint32_t pos=binary(c,SpvOpFSub,c->v2,binary(c,SpvOpFMul,c->v2,uv,size),splat(c,c->v2,c->cf[F_HALF],2));
    uint32_t weight=ext1(c,GLSLstd450Fract,c->v2,pos);
    uint32_t quad=binary(c,SpvOpFDiv,c->v2,binary(c,SpvOpFAdd,c->v2,ext1(c,GLSLstd450Floor,c->v2,pos),splat(c,c->v2,c->cf[F_ONE],2)),size),linear=0;
    if(signs&64)linear=binary(c,(signs&16)?SpvOpFOrdGreaterThan:SpvOpFOrdLessThanEqual,c->b,extract(c,val(c->w,SpvOpImageQueryLod,c->v2,2,sample,uv),1),c->cf[F_ZERO]);
    uint32_t components[2]={extract(c,point,0),extract(c,point,1)};
    for(unsigned axis=0;axis<2;axis++)if(signs&(axis?10:5)){
        uint32_t taps=val(c->w,SpvOpImageGather,c->v4,3,sample,quad,c->si[axis?1:2]),values[4];
        for(unsigned j=0;j<4;j++)values[j]=gb_sign(c,extract(c,taps,j));
        uint32_t a=ext3(c,GLSLstd450FMix,c->f,values[3],values[2],extract(c,weight,0)),b=ext3(c,GLSLstd450FMix,c->f,values[0],values[1],extract(c,weight,0));
        uint32_t filtered=ext3(c,GLSLstd450FMix,c->f,a,b,extract(c,weight,1));
        components[axis]=linear?choose(c,c->f,linear,filtered,components[axis]):filtered;
    }
    return val(c->w,SpvOpCompositeConstruct,c->v2,2,components[0],components[1]);
}
static void gb_remap(Context *c)
{
    for(unsigned unit=0;unit<4;unit++)if(c->state->g8b8_signed[unit]){
        uint32_t old=c->r[PS_REGISTER_T0+unit],channels[4];unsigned signs=c->state->g8b8_signed[unit];
        uint32_t signed_value=ext3(c,GLSLstd450FClamp,c->v2,c->signed_gb[unit],splat(c,c->v2,unary(c,SpvOpFNegate,c->f,c->cf[F_ONE]),2),splat(c,c->v2,c->cf[F_ONE],2));
        for(unsigned j=0;j<4;j++){unsigned bit=j==0?4:j==1?2:j==2?1:8;channels[j]=(signs&bit)?extract(c,signed_value,j==0 || j==2?0:1):extract(c,old,j);}
        c->r[PS_REGISTER_T0+unit]=v4(c,channels[0],channels[1],channels[2],channels[3]);
    }
}
