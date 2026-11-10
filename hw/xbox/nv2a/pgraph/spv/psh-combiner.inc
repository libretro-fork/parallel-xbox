/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Internal native dynamic combiner emitter. Included after Context/IR helpers.
 * The bank is shader-local, not a host snapshot or a new GPU resource. */
static uint32_t comb_bits(Context *c,uint32_t x,unsigned shift,unsigned mask)
{
    if(shift)x=binary(c,SpvOpShiftRightLogical,c->u,x,c->ui32[shift]);
    return binary(c,SpvOpBitwiseAnd,c->u,x,mask==255?c->flags[0]:c->ui32[mask]);
}
static uint32_t comb_flag(Context *c,uint32_t x,uint32_t mask)
{
    return binary(c,SpvOpINotEqual,c->b,binary(c,SpvOpBitwiseAnd,c->u,x,mask),c->ui32[0]);
}
static uint32_t comb_reg(Context *c,uint32_t reg)
{
    uint32_t ptr=val(c->w,SpvOpAccessChain,c->bank_element,2,c->bank,reg);
    return unary(c,SpvOpLoad,c->v4,ptr);
}
static void comb_store(Context *c,uint32_t reg,uint32_t value)
{
    op(c->w,SpvOpStore,2,val(c->w,SpvOpAccessChain,c->bank_element,2,c->bank,reg),value);
}
static uint32_t comb_fetch(Context *c,uint32_t reg,uint32_t stage)
{
    if(!c->fetch_body)return val(c->w,SpvOpFunctionCall,c->v4,3,c->fetch_fn,reg,stage);
    uint32_t constant=binary(c,SpvOpLogicalOr,c->b,
        binary(c,SpvOpIEqual,c->b,reg,c->ui32[1]),
        binary(c,SpvOpIEqual,c->b,reg,c->ui32[2]));
    uint32_t yes=id(c->w),no=id(c->w),merge=id(c->w);
    op(c->w,SpvOpSelectionMerge,2,merge,0u);
    op(c->w,SpvOpBranchConditional,3,constant,yes,no);
    op(c->w,SpvOpLabel,1,yes);
    uint32_t second=binary(c,SpvOpIEqual,c->b,reg,c->ui32[2]);
    uint32_t flag=choose(c,c->u,second,c->flags[3],c->flags[2]);
    uint32_t unique=binary(c,SpvOpLogicalOr,c->b,
        binary(c,SpvOpIEqual,c->b,stage,c->si[8]),comb_flag(c,unif(c,COMB_CTL,0),flag));
    uint32_t index=binary(c,SpvOpIAdd,c->i,
        binary(c,SpvOpIMul,c->i,choose(c,c->i,unique,stage,c->si[0]),c->si[2]),
        choose(c,c->i,second,c->si[1],c->si[0]));
    uint32_t value=unif_at(c,CONSTANTS,index),a=c->w->block;
    op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,no);
    uint32_t bank=comb_reg(c,reg),b=c->w->block;
    op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,merge);
    return val(c->w,SpvOpPhi,c->v4,4,value,a,bank,b);
}
static uint32_t comb_table(Context *c,unsigned table,uint32_t index)
{
    return unary(c,SpvOpLoad,c->f,val(c->w,SpvOpAccessChain,c->map_element,2,c->maps[table],index));
}
static uint32_t comb_input(Context *c,uint32_t byte,uint32_t stage,bool alpha)
{
    uint32_t source=comb_fetch(c,comb_bits(c,byte,0,15),stage);
    uint32_t type=alpha?c->f:c->v3;unsigned n=alpha?1:3;
    uint32_t channel=comb_flag(c,byte,c->ui32[16]);
    uint32_t x=alpha?choose(c,c->f,channel,extract(c,source,3),extract(c,source,2)):
        choose(c,c->v3,channel,splat(c,c->v3,extract(c,source,3),3),xyz(c,source));
    uint32_t mapping=comb_bits(c,byte,5,7);
    uint32_t zero=splat(c,type,c->cf[F_ZERO],n),one=splat(c,type,c->cf[F_ONE],n);
    uint32_t nonnegative=ext2(c,GLSLstd450FMax,type,x,zero);
    uint32_t base=choose(c,type,binary(c,SpvOpUGreaterThanEqual,c->b,mapping,c->ui32[6]),x,nonnegative);
    base=choose(c,type,binary(c,SpvOpIEqual,c->b,mapping,c->ui32[1]),
        ext3(c,GLSLstd450FClamp,type,x,zero,one),base);
    /* Match dynamic GLSL's table expression, not static mapping algebra. */
    return binary(c,SpvOpFAdd,type,
        binary(c,SpvOpFMul,type,base,splat(c,type,comb_table(c,0,mapping),n)),
        splat(c,type,comb_table(c,1,mapping),n));
}
static uint32_t comb_output(Context *c,uint32_t x,uint32_t mapping,bool alpha)
{
    uint32_t type=alpha?c->f:c->v3;unsigned n=alpha?1:3;
    uint32_t mapped=binary(c,SpvOpFMul,type,
        binary(c,SpvOpFAdd,type,x,splat(c,type,comb_table(c,3,mapping),n)),
        splat(c,type,comb_table(c,2,mapping),n));
    return ext3(c,GLSLstd450FClamp,type,mapped,
        splat(c,type,c->negative[0],n),splat(c,type,c->cf[F_ONE],n));
}
static void comb_write(Context *c,uint32_t reg,uint32_t x,bool alpha)
{
    uint32_t yes=id(c->w),merge=id(c->w);
    uint32_t enabled=binary(c,SpvOpINotEqual,c->b,reg,c->ui32[0]);
    op(c->w,SpvOpSelectionMerge,2,merge,0u);op(c->w,SpvOpBranchConditional,3,enabled,yes,merge);
    op(c->w,SpvOpLabel,1,yes);uint32_t old=comb_reg(c,reg);
    uint32_t value=alpha?v4(c,extract(c,old,0),extract(c,old,1),extract(c,old,2),x):
        v4(c,extract(c,x,0),extract(c,x,1),extract(c,x,2),extract(c,old,3));
    comb_store(c,reg,value);op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,merge);
}
static void comb_blue(Context *c,uint32_t reg,uint32_t x,uint32_t flag)
{
    uint32_t yes=id(c->w),merge=id(c->w);
    op(c->w,SpvOpSelectionMerge,2,merge,0u);op(c->w,SpvOpBranchConditional,3,flag,yes,merge);
    op(c->w,SpvOpLabel,1,yes);comb_write(c,reg,extract(c,x,2),true);
    op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,merge);
}
static void comb_stage(Context *c,uint32_t stage)
{
    uint32_t rin=unif_at(c,COMB_RGB_IN,stage),rout=unif_at(c,COMB_RGB_OUT,stage);
    uint32_t ain=unif_at(c,COMB_ALPHA_IN,stage),aout=unif_at(c,COMB_ALPHA_OUT,stage);
    uint32_t rf=binary(c,SpvOpShiftRightLogical,c->u,rout,c->ui32[12]);
    uint32_t af=binary(c,SpvOpShiftRightLogical,c->u,aout,c->ui32[12]);
    uint32_t rgb[4],alpha[4];
    for(unsigned j=0;j<4;j++)rgb[j]=comb_input(c,comb_bits(c,rin,24-j*8,255),stage,false);
    for(unsigned j=0;j<4;j++)alpha[j]=comb_input(c,comb_bits(c,ain,24-j*8,255),stage,true);
    uint32_t rab=choose(c,c->v3,comb_flag(c,rf,c->ui32[2]),
        splat(c,c->v3,binary(c,SpvOpDot,c->f,rgb[0],rgb[1]),3),binary(c,SpvOpFMul,c->v3,rgb[0],rgb[1]));
    uint32_t rcd=choose(c,c->v3,comb_flag(c,rf,c->ui32[1]),
        splat(c,c->v3,binary(c,SpvOpDot,c->f,rgb[2],rgb[3]),3),binary(c,SpvOpFMul,c->v3,rgb[2],rgb[3]));
    uint32_t aab=mul(c,alpha[0],alpha[1]),acd=mul(c,alpha[2],alpha[3]);
    uint32_t r0=extract(c,comb_reg(c,c->ui32[12]),3);
    uint32_t msb=id(c->w),lsb=id(c->w),merge=id(c->w);
    uint32_t msb_mode=comb_flag(c,c->ctl,c->flags[1]);
    op(c->w,SpvOpSelectionMerge,2,merge,0u);
    op(c->w,SpvOpBranchConditional,3,msb_mode,msb,lsb);
    op(c->w,SpvOpLabel,1,msb);
    uint32_t high=binary(c,SpvOpFOrdGreaterThanEqual,c->b,r0,c->cf[F_HALF]);
    op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,lsb);
    uint32_t bits=unary(c,SpvOpConvertFToU,c->u,mul(c,r0,c->cf[F_BYTE_MAX]));
    uint32_t low=binary(c,SpvOpIEqual,c->b,comb_bits(c,bits,0,1),c->ui32[1]);
    op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,merge);
    uint32_t mux=val(c->w,SpvOpPhi,c->b,4,high,msb,low,lsb);
    uint32_t rmux=choose(c,c->v3,comb_flag(c,rf,c->ui32[4]),
        choose(c,c->v3,mux,rcd,rab),binary(c,SpvOpFAdd,c->v3,rab,rcd));
    uint32_t amux=choose(c,c->f,comb_flag(c,af,c->ui32[4]),choose(c,c->f,mux,acd,aab),add(c,aab,acd));
    uint32_t rmap=comb_bits(c,rf,3,7),amap=comb_bits(c,af,3,7);
    rab=comb_output(c,rab,rmap,false);rcd=comb_output(c,rcd,rmap,false);rmux=comb_output(c,rmux,rmap,false);
    aab=comb_output(c,aab,amap,true);acd=comb_output(c,acd,amap,true);amux=comb_output(c,amux,amap,true);
    uint32_t ab=comb_bits(c,rout,4,15),cd=comb_bits(c,rout,0,15);
    comb_write(c,ab,rab,false);comb_blue(c,ab,rab,comb_flag(c,rf,c->flags[5]));
    comb_write(c,cd,rcd,false);comb_blue(c,cd,rcd,comb_flag(c,rf,c->flags[4]));
    comb_write(c,comb_bits(c,rout,8,15),rmux,false);
    comb_write(c,comb_bits(c,aout,4,15),aab,true);comb_write(c,comb_bits(c,aout,0,15),acd,true);
    comb_write(c,comb_bits(c,aout,8,15),amux,true);
}
static uint32_t dynamic_combiner(Context *c)
{
    for(unsigned j=0;j<16;j++)comb_store(c,c->ui32[j],c->r[j]);
    c->ctl=unif(c,COMB_CTL,0);
    uint32_t entry=c->w->block,header=id(c->w),body=id(c->w),cont=id(c->w),merge=id(c->w);
    uint32_t stage=id(c->w),next=id(c->w);
    op(c->w,SpvOpBranch,1,header);op(c->w,SpvOpLabel,1,header);
    op(c->w,SpvOpPhi,6,c->i,stage,c->si[0],entry,next,cont);
    uint32_t count=unary(c,SpvOpBitcast,c->i,comb_bits(c,c->ctl,0,255));
    uint32_t active=binary(c,SpvOpSLessThan,c->b,stage,count);
    op(c->w,SpvOpLoopMerge,3,merge,cont,0u);op(c->w,SpvOpBranchConditional,3,active,body,merge);
    op(c->w,SpvOpLabel,1,body);comb_stage(c,stage);op(c->w,SpvOpBranch,1,cont);
    op(c->w,SpvOpLabel,1,cont);op(c->w,SpvOpIAdd,4,c->i,next,stage,c->si[1]);
    op(c->w,SpvOpBranch,1,header);op(c->w,SpvOpLabel,1,merge);
    uint32_t f0=unif(c,COMB_FINAL0,0),f1=unif(c,COMB_FINAL1,0);
    uint32_t empty=binary(c,SpvOpIEqual,c->b,binary(c,SpvOpBitwiseOr,c->u,f0,f1),c->ui32[0]);
    uint32_t zero=id(c->w),final=id(c->w),end=id(c->w);
    op(c->w,SpvOpSelectionMerge,2,end,0u);op(c->w,SpvOpBranchConditional,3,empty,zero,final);
    op(c->w,SpvOpLabel,1,zero);uint32_t black=splat(c,c->v4,c->cf[F_ZERO],4);
    op(c->w,SpvOpBranch,1,end);op(c->w,SpvOpLabel,1,final);
    uint32_t one=splat(c,c->v3,c->cf[F_ONE],3);
    uint32_t v1=xyz(c,comb_reg(c,c->ui32[5])),r0=xyz(c,comb_reg(c,c->ui32[12]));
    v1=choose(c,c->v3,comb_flag(c,f1,c->flags[4]),binary(c,SpvOpFSub,c->v3,one,v1),v1);
    uint32_t r0_complement=binary(c,SpvOpShiftLeftLogical,c->u,c->ui32[1],c->ui32[5]);
    r0=choose(c,c->v3,comb_flag(c,f1,r0_complement),binary(c,SpvOpFSub,c->v3,one,r0),r0);
    uint32_t sum=binary(c,SpvOpFAdd,c->v3,v1,r0);
    sum=choose(c,c->v3,comb_flag(c,f1,c->flags[5]),
        ext3(c,GLSLstd450FClamp,c->v3,sum,splat(c,c->v3,c->cf[F_ZERO],3),one),sum);
    comb_store(c,c->ui32[14],v4(c,extract(c,sum,0),extract(c,sum,1),extract(c,sum,2),c->cf[F_ZERO]));
    uint32_t e=comb_input(c,comb_bits(c,f1,24,255),c->si[8],false);
    uint32_t f=comb_input(c,comb_bits(c,f1,16,255),c->si[8],false);
    uint32_t ef=binary(c,SpvOpFMul,c->v3,e,f);
    comb_store(c,c->ui32[15],v4(c,extract(c,ef,0),extract(c,ef,1),extract(c,ef,2),c->cf[F_ZERO]));
    uint32_t a=comb_input(c,comb_bits(c,f0,24,255),c->si[8],false);
    uint32_t b=comb_input(c,comb_bits(c,f0,16,255),c->si[8],false);
    uint32_t cc=comb_input(c,comb_bits(c,f0,8,255),c->si[8],false);
    uint32_t d=comb_input(c,comb_bits(c,f0,0,255),c->si[8],false);
    uint32_t rgb=binary(c,SpvOpFAdd,c->v3,d,ext3(c,GLSLstd450FMix,c->v3,cc,b,a));
    uint32_t alpha=comb_input(c,comb_bits(c,f1,8,255),c->si[8],true);
    uint32_t color=v4(c,extract(c,rgb,0),extract(c,rgb,1),extract(c,rgb,2),alpha),last=c->w->block;
    op(c->w,SpvOpBranch,1,end);op(c->w,SpvOpLabel,1,end);
    return val(c->w,SpvOpPhi,c->v4,4,black,zero,color,last);
}
static void dynamic_allocate(Context *c)
{
    uint32_t *ids[]={&c->comb_array,&c->bank_array,&c->bank_ptr,&c->bank_element,&c->bank,&c->map_array,&c->map_ptr,&c->map_element,&c->fetch_fn,&c->fetch_type};
    for(unsigned j=0;j<sizeof(ids)/sizeof(ids[0]);j++)*ids[j]=id(c->w);
    for(unsigned j=0;j<4;j++){c->map_const[j]=id(c->w);c->maps[j]=id(c->w);}
    for(unsigned j=0;j<3;j++)c->negative[j]=id(c->w);
    for(unsigned j=0;j<6;j++)c->flags[j]=id(c->w);
}
static void dynamic_types(Context *c)
{
    Writer *w=c->w;
    op(w,SpvOpTypeArray,3,c->bank_array,c->v4,c->ui32[16]);
    op(w,SpvOpTypePointer,3,c->bank_ptr,(uint32_t)SpvStorageClassPrivate,c->bank_array);
    op(w,SpvOpTypePointer,3,c->bank_element,(uint32_t)SpvStorageClassPrivate,c->v4);
    op(w,SpvOpTypeFunction,4,c->fetch_type,c->v4,c->u,c->i);
    op(w,SpvOpTypeArray,3,c->map_array,c->f,c->ui32[8]);
    op(w,SpvOpTypePointer,3,c->map_ptr,(uint32_t)SpvStorageClassPrivate,c->map_array);
    op(w,SpvOpTypePointer,3,c->map_element,(uint32_t)SpvStorageClassPrivate,c->f);
    op(w,SpvOpVariable,3,c->bank_ptr,c->bank,(uint32_t)SpvStorageClassPrivate);
    const float negatives[]={-1,-2,-.5f};
    for(unsigned j=0;j<3;j++){uint32_t bits;memcpy(&bits,negatives+j,4);op(w,SpvOpConstant,3,c->f,c->negative[j],bits);}
    const uint32_t flags[]={255,256,4096,65536,64,128};
    for(unsigned j=0;j<6;j++)op(w,SpvOpConstant,3,c->u,c->flags[j],flags[j]);
    uint32_t tables[][8]={
        {c->cf[F_ONE],c->negative[0],c->cf[F_TWO],c->negative[1],c->cf[F_ONE],c->negative[0],c->cf[F_ONE],c->negative[0]},
        {c->cf[F_ZERO],c->cf[F_ONE],c->negative[0],c->cf[F_ONE],c->negative[2],c->cf[F_HALF],c->cf[F_ZERO],c->cf[F_ZERO]},
        {c->cf[F_ONE],c->cf[F_ONE],c->cf[F_TWO],c->cf[F_TWO],c->cf[F_FOUR],c->cf[F_ONE],c->cf[F_HALF],c->cf[F_ONE]},
        {c->cf[F_ZERO],c->negative[2],c->cf[F_ZERO],c->negative[2],c->cf[F_ZERO],c->cf[F_ZERO],c->cf[F_ZERO],c->cf[F_ZERO]}
    };
    for(unsigned j=0;j<4;j++){
        word(w,(11u<<16)|SpvOpConstantComposite);word(w,c->map_array);word(w,c->map_const[j]);
        for(unsigned k=0;k<8;k++)word(w,tables[j][k]);
        op(w,SpvOpVariable,4,c->map_ptr,c->maps[j],(uint32_t)SpvStorageClassPrivate,c->map_const[j]);
    }
}

static void dynamic_helpers(Context *c)
{
    op(c->w,SpvOpFunction,4,c->v4,c->fetch_fn,0u,c->fetch_type);
    uint32_t reg=id(c->w),stage=id(c->w);
    op(c->w,SpvOpFunctionParameter,2,c->u,reg);op(c->w,SpvOpFunctionParameter,2,c->i,stage);
    op(c->w,SpvOpLabel,1,id(c->w));c->fetch_body=true;
    uint32_t value=comb_fetch(c,reg,stage);c->fetch_body=false;
    op(c->w,SpvOpReturnValue,1,value);op(c->w,SpvOpFunctionEnd,0);
}
