/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdarg.h>
#include <string.h>
#include <spirv/unified1/spirv.h>
/* GLSL.std.450 numbers, as in the other native emitters; no extra SDK header. */
enum { GLSLstd450Round=1, GLSLstd450FAbs=4, GLSLstd450Floor=8,
       GLSLstd450Ceil=9, GLSLstd450Fract=10, GLSLstd450UMin=38, GLSLstd450FMax=40, GLSLstd450FClamp=43,
       GLSLstd450FMix=46, GLSLstd450Fma=50, GLSLstd450Normalize=69 };
#include "psh.h"
#include "hw/xbox/nv2a/pgraph/glsl/g8b8-lod.h"

/* Count/write share an immutable key. Only bounded arithmetic IDs are
 * recorded on the stack and lent to the final write; no word-buffer clone. */
typedef struct Writer {
    uint32_t *words, next, block;
    size_t count, capacity;
    uint32_t *precise;
    unsigned recorded, annotations;
} Writer;
static uint32_t id(Writer *w) { return w->next++; }
static void word(Writer *w,uint32_t x) { if(w->words){if(w->count>=w->capacity)abort();w->words[w->count]=x;}w->count++; }
static void op(Writer *w,SpvOp code,unsigned n,...)
{
    word(w,((n+1)<<16)|code);va_list a;va_start(a,n);
    for(unsigned j=0;j<n;j++){uint32_t x=va_arg(a,uint32_t);if(code==SpvOpLabel && !j)w->block=x;word(w,x);}va_end(a);
}
static uint32_t val(Writer *w,SpvOp code,uint32_t type,unsigned n,...)
{
    uint32_t result=id(w);word(w,((n+3)<<16)|code);word(w,type);word(w,result);va_list a;va_start(a,n);
    for(unsigned j=0;j<n;j++){word(w,va_arg(a,uint32_t));}va_end(a);return result;
}
enum Uniform { CLIP, REGION, CONSTANTS, FACTOR, OFFSET, FOG, SCALE, SIDE, ALPHA_REF, ALPHA_FUNC, ALPHA_ENABLE, BASE_UNIFORMS, TEX_SCALE=BASE_UNIFORMS, ALPHA_KILL, COMB_CTL, COMB_FINAL0, COMB_FINAL1, COMB_RGB_IN, COMB_RGB_OUT, COMB_ALPHA_IN, COMB_ALPHA_OUT, BORDER_LOGICAL, BORDER_INV, COLOR_KEY, COLOR_MASK, BUMP_MAT, BUMP_SCALE, BUMP_OFFSET, UNIFORMS };
static const struct {const char *name;unsigned width,count,offset;bool integer;} uniforms[UNIFORMS]={
    {"clipRange",4,1,0,false},{"clipRegion",4,8,16,true},{"consts",4,18,144,false},
    {"depthFactor",1,1,432,false},{"depthOffset",1,1,436,false},{"fogColor",4,1,448,false},
    {"surfaceScale",2,1,464,true},{"twoSideSel",1,1,472,true},{"alphaRef",1,1,476,true},
    {"alphaFunc",1,1,480,true},{"alphaTestEnable",1,1,484,true},
    {"texScale",1,4,496,false},{"alphaKillMask",1,1,560,true},
    {"combCtl",1,1,0,true},{"combFinal0",1,1,0,true},{"combFinal1",1,1,0,true},
    {"combRgbIn",1,8,0,true},{"combRgbOut",1,8,0,true},
    {"combAlphaIn",1,8,0,true},{"combAlphaOut",1,8,0,true},
    {"borderLogicalSize",3,4,0,false},{"borderInvRealSize",3,4,0,false},
    {"colorKey",1,4,0,true},{"colorKeyMask",1,4,0,true},
    {"bumpMat",2,8,0,false},{"bumpScale",1,4,0,false},{"bumpOffset",1,4,0,false}
};
static unsigned texture_mode(const PshState *s,unsigned stage)
{ return (s->shader_stage_program>>(stage*5))&31; }
/* These comparisons produce a texture-stage result without reading an image. */
static bool constant_shadow(const PshState *s,unsigned stage)
{
    unsigned mode=texture_mode(s,stage);
    return (mode==PS_TEXTUREMODES_PROJECT2D || mode==PS_TEXTUREMODES_PROJECT3D) && s->shadow_map[stage] && (s->shadow_depth_func==SHADOW_DEPTH_FUNC_NEVER || s->shadow_depth_func==SHADOW_DEPTH_FUNC_ALWAYS);
}
static bool f16_shadow(const PshState *s,unsigned stage)
{
    unsigned mode=texture_mode(s,stage),flags=s->shadow_map[stage];
    return (mode==PS_TEXTUREMODES_PROJECT2D || mode==PS_TEXTUREMODES_PROJECT3D) && (flags&3)==3 && !(flags&~31u) && s->shadow_depth_func>=SHADOW_DEPTH_FUNC_LESS && s->shadow_depth_func<=SHADOW_DEPTH_FUNC_GEQUAL;
}
static bool has_f16_shadow(const PshState *s)
{ for(unsigned j=0;j<4;j++)if(f16_shadow(s,j))return true;return false; }
static bool sampled_shadow(const PshState *s,unsigned stage)
{
    unsigned mode=texture_mode(s,stage);
    return (mode==PS_TEXTUREMODES_PROJECT2D || mode==PS_TEXTUREMODES_PROJECT3D) && (s->shadow_map[stage]==1 || f16_shadow(s,stage)) && s->shadow_depth_func>=SHADOW_DEPTH_FUNC_LESS && s->shadow_depth_func<=SHADOW_DEPTH_FUNC_GEQUAL;
}
static bool shadow_constant_needed(const PshState *s,bool floating)
{
    if(floating && has_f16_shadow(s))return true;
    for(unsigned j=0;j<4;j++)if(sampled_shadow(s,j) && !f16_shadow(s,j) && (floating?(texture_mode(s,j)==PS_TEXTUREMODES_PROJECT3D && !s->tex_x8y24[j]):(texture_mode(s,j)==PS_TEXTUREMODES_PROJECT3D || s->tex_x8y24[j])))return true;
    return false;
}
static bool dot_mode(unsigned mode)
{ return mode==PS_TEXTUREMODES_DOTPRODUCT || mode==PS_TEXTUREMODES_DOT_ST || mode==PS_TEXTUREMODES_DOT_ZW || mode==PS_TEXTUREMODES_DOT_RFLCT_DIFF || mode==PS_TEXTUREMODES_DOT_RFLCT_SPEC || mode==PS_TEXTUREMODES_DOT_STR_3D || mode==PS_TEXTUREMODES_DOT_STR_CUBE; }
static bool cube_stage(const PshState *s,unsigned stage)
{ unsigned m=texture_mode(s,stage);return m==PS_TEXTUREMODES_CUBEMAP || m==PS_TEXTUREMODES_DOT_RFLCT_DIFF || m==PS_TEXTUREMODES_DOT_RFLCT_SPEC || m==PS_TEXTUREMODES_DOT_STR_CUBE; }
static unsigned dot_mapping(const PshState *s,unsigned stage)
{ return stage?(s->other_stage_input>>((stage-1)*4))&15:0; }
static bool dot_mapping_used(const PshState *s,unsigned mapping)
{
    for(unsigned j=1;j<4;j++)if(dot_mode(texture_mode(s,j)) && dot_mapping(s,j)==mapping)return true;
    return texture_mode(s,2)==PS_TEXTUREMODES_DOT_RFLCT_DIFF && dot_mapping(s,3)==mapping;
}
static bool has_dot_depth(const PshState *s)
{ return texture_mode(s,2)==PS_TEXTUREMODES_DOT_ZW || texture_mode(s,3)==PS_TEXTUREMODES_DOT_ZW; }
static bool bump_stage(const PshState *s,unsigned stage)
{ unsigned mode=texture_mode(s,stage);return mode==PS_TEXTUREMODES_BUMPENVMAP || mode==PS_TEXTUREMODES_BUMPENVMAP_LUM; }
static bool dependent_stage(const PshState *s,unsigned stage)
{ unsigned mode=texture_mode(s,stage);return bump_stage(s,stage) || mode==PS_TEXTUREMODES_DPNDNT_AR || mode==PS_TEXTUREMODES_DPNDNT_GB; }
static unsigned texture_input(const PshState *s,unsigned stage)
{ return stage<2?0:(s->other_stage_input>>(16+(stage-2)*4))&15; }
static bool has_bump(const PshState *s)
{ for(unsigned j=0;j<4;j++)if(bump_stage(s,j))return true;return false; }
static bool has_bump_luma(const PshState *s)
{ for(unsigned j=0;j<4;j++)if(texture_mode(s,j)==PS_TEXTUREMODES_BUMPENVMAP_LUM)return true;return false; }
static bool texture_result_stage(const PshState *s,unsigned stage)
{ unsigned mode=texture_mode(s,stage);return mode==PS_TEXTUREMODES_PROJECT2D || mode==PS_TEXTUREMODES_PROJECT3D || mode==PS_TEXTUREMODES_CUBEMAP || dependent_stage(s,stage) || (dot_mode(mode) && mode!=PS_TEXTUREMODES_DOTPRODUCT && mode!=PS_TEXTUREMODES_DOT_ZW); }
static bool sample_stage(const PshState *s,unsigned stage)
{ return texture_result_stage(s,stage) && !constant_shadow(s,stage); }
static bool raw_color(const PshState *s,unsigned stage)
{
    if(!s->tex_x8y24[stage] || s->shadow_map[stage])return false;
    unsigned mode=texture_mode(s,stage);
    return (mode==PS_TEXTUREMODES_PROJECT2D && s->dim_tex[stage]==2) || (mode==PS_TEXTUREMODES_PROJECT3D && (s->dim_tex[stage]==2 || s->dim_tex[stage]==3));
}
/* Same descriptor slot; only its SPIR-V sampler type/dimension varies. */
static unsigned sampler_kind(const PshState *s,unsigned stage)
{
    unsigned mode=texture_mode(s,stage);
    if(sampled_shadow(s,stage))return s->tex_x8y24[stage]?3:0;
    if(raw_color(s,stage))return 3;
    if(cube_stage(s,stage))return s->tex_cubemap[stage]?2:0;
    if(s->dim_tex[stage]==3)return 1;
    return mode==PS_TEXTUREMODES_PROJECT2D && s->tex_cubemap[stage]?2:0;
}
static bool has_sampler_kind(const PshState *s,unsigned kind)
{ for(unsigned j=0;j<4;j++)if(sample_stage(s,j) && sampler_kind(s,j)==kind)return true;return false; }
static bool has_texture_results(const PshState *s)
{ for(unsigned j=0;j<4;j++)if(texture_result_stage(s,j))return true;return false; }
static bool has_rect_samples(const PshState *s)
{ for(unsigned j=0;j<4;j++)if(sample_stage(s,j) && !cube_stage(s,j) && s->rect_tex[j])return true;return false; }
static bool has_texture_scale(const PshState *s)
{ return has_rect_samples(s) || has_f16_shadow(s); }
static bool has_signed_gb(const PshState *s)
{ for(unsigned j=0;j<4;j++)if(s->g8b8_signed[j])return true;return false; }
static bool has_signed_query(const PshState *s)
{ for(unsigned j=0;j<4;j++)if(s->g8b8_signed[j] && ((s->g8b8_signed[j]&(16|64)) || ((s->g8b8_signed[j]&128) && (s->g8b8_mip_flags&(1u<<j)))))return true;return false; }
static bool has_border_samples(const PshState *s)
{ for(unsigned j=0;j<4;j++)if(sample_stage(s,j) && !s->shadow_map[j] && (texture_mode(s,j)==PS_TEXTUREMODES_PROJECT2D || texture_mode(s,j)==PS_TEXTUREMODES_PROJECT3D || dot_mode(texture_mode(s,j))) && s->border_adjust[j])return true;return false; }
static bool has_color_key(const PshState *s)
{ for(unsigned j=0;j<4;j++)if(texture_result_stage(s,j) && s->colorkey_mode[j]!=COLOR_KEY_NONE)return true;return false; }
static bool convolution_stage(const PshState *s,unsigned stage)
{ return texture_mode(s,stage)==PS_TEXTUREMODES_PROJECT2D && !sampled_shadow(s,stage) && s->conv_tex[stage]!=CONVOLUTION_FILTER_DISABLED; }
static bool has_convolution(const PshState *s)
{ for(unsigned j=0;j<4;j++)if(convolution_stage(s,j))return true;return false; }
static bool uniform_enabled(const PshState *s,unsigned which)
{
    if(which>=BUMP_MAT)return which==BUMP_MAT?has_bump(s):has_bump_luma(s);
    if(which>=BORDER_LOGICAL)return which<=BORDER_INV?has_border_samples(s):has_color_key(s);
    return which<BASE_UNIFORMS || (which>=COMB_CTL?s->combiner_dynamic:which==TEX_SCALE?has_texture_scale(s):s->alpha_dynamic && has_texture_results(s));
}
static unsigned uniform_member(const PshState *s,unsigned which)
{ unsigned n=0;for(unsigned j=0;j<which;j++)n+=uniform_enabled(s,j);return n; }
static unsigned uniform_count(const PshState *s)
{ return uniform_member(s,UNIFORMS); }
static unsigned texture_uniform_size(const PshState *s)
{ return has_texture_scale(s)?(s->alpha_dynamic?576:560):(s->alpha_dynamic && has_texture_results(s)?512:496); }
static unsigned uniform_offset(const PshState *s,unsigned which)
{
    if(which>=BUMP_MAT){unsigned base=texture_uniform_size(s)+(s->combiner_dynamic?528:0)+(has_border_samples(s)?128:0)+(has_color_key(s)?128:0);return which==BUMP_MAT?base:base+128+(which-BUMP_SCALE)*64;}
    if(which>=BORDER_LOGICAL){unsigned base=texture_uniform_size(s)+(s->combiner_dynamic?528:0);return which<=BORDER_INV?base+(which-BORDER_LOGICAL)*64:base+(has_border_samples(s)?128:0)+(which-COLOR_KEY)*64;}
    if(which>=COMB_CTL){unsigned base=texture_uniform_size(s);return which<COMB_RGB_IN?base+(which-COMB_CTL)*4:base+16+(which-COMB_RGB_IN)*128;}
    return which==ALPHA_KILL?(has_texture_scale(s)?560:496):uniforms[which].offset;
}
static unsigned uniform_size(const PshState *s)
{ return texture_uniform_size(s)+(s->combiner_dynamic?528:0)+(has_border_samples(s)?128:0)+(has_color_key(s)?128:0)+(has_bump(s)?128:0)+(has_bump_luma(s)?128:0); }

enum Floats { F_ZERO, F_HALF, F_ONE, F_TWO, F_FOUR, F_BYTE_MAX, F_D16_MAX, F_D24_SCALE, F_FLOAT_MAX, FLOATS };
typedef struct Context {
    Writer *w;const PshState *state;SpvPshOptions opts;
    uint32_t void_t,fn,f,v2,v3,v4,i,u,b,iv2,iv4,in_f,in_v4,in_b,out_f,out_v4,uf,uv4,uiv2,uiv4,ui,uu;
    uint32_t region_array,const_array,ubo_type,ubo_ptr,ubo,entry,ext;
    uint32_t si[32],ui32[32],cf[FLOATS],u127,u313,yes,no,ui_marker,ui_rect;
    uint32_t inputs[9],frag_coord,front,color,depth;
    uint32_t tex_inputs[4],samplers[4],image_t,sampled_t,sampler_ptr,scale_array,min_q,point_coord,in_v2;
    uint32_t image_other[3],sampled_other[3],sampler_other_ptr[3];
    uint32_t r[16],ef,z,positions[3],coord,clip;
    /* Shader-local bank and immutable mapping-table IDs; never host payloads. */
    uint32_t comb_array,bank_array,bank_ptr,bank_element,bank;
    uint32_t map_array,map_ptr,map_element,map_const[4],maps[4],negative[3],flags[6],ctl,fetch_fn,fetch_type;
    uint32_t border_array,uniform_v3,key_array,uvec4_t;
    uint32_t conv_array,conv_offsets_array,conv_ptr,conv_offsets_ptr,conv_element,conv_offset_element;
    uint32_t conv_weights,conv_offsets,conv_weight_init,conv_offset_init,conv_negative,conv_weight[3],conv_vec2[9];
    uint32_t cube_fn,cube_fn_type,project_cube_fn,project_cube_type;
    uint32_t bump_array,bump_m2,uniform_v2,sign_fn,sign_type,sign_constants[3];
    uint32_t dots[4],dot_sign[2],dot_sign_type,dot_constants[4],dot_maps[4],dot_map_type;
    uint32_t shadow_max24,shadow_max_float,f16_bvec4,f16_base;
    uint32_t gb_coord[4],gb_sample[4],signed_gb[4];
    bool cube_body,project_cube_body;
    bool fetch_body;
    bool precise;
} Context;
static uint32_t unary(Context *c,SpvOp code,uint32_t type,uint32_t a){return val(c->w,code,type,1,a);}
static uint32_t binary(Context *c,SpvOp code,uint32_t type,uint32_t a,uint32_t b){return val(c->w,code,type,2,a,b);}
static uint32_t pin(Context *c,uint32_t result)
{
    if(c->precise){unsigned n=c->w->recorded++;if(n>=128)abort();if(c->w->words){if(c->w->precise[n]!=result)abort();}else c->w->precise[n]=result;}return result;
}
static uint32_t math(Context *c,SpvOp code,uint32_t type,uint32_t a,uint32_t b){return pin(c,binary(c,code,type,a,b));}
static uint32_t ext1(Context *c,unsigned code,uint32_t type,uint32_t a){return pin(c,val(c->w,SpvOpExtInst,type,3,c->ext,code,a));}
static uint32_t ext2(Context *c,unsigned code,uint32_t type,uint32_t a,uint32_t b){return pin(c,val(c->w,SpvOpExtInst,type,4,c->ext,code,a,b));}
static uint32_t ext3(Context *c,unsigned code,uint32_t type,uint32_t a,uint32_t b,uint32_t d){return pin(c,val(c->w,SpvOpExtInst,type,5,c->ext,code,a,b,d));}
static uint32_t extract(Context *c,uint32_t x,unsigned n){return val(c->w,SpvOpCompositeExtract,c->f,2,x,n);}
static uint32_t splat(Context *c,uint32_t type,uint32_t x,unsigned n){return n==1?x:n==2?val(c->w,SpvOpCompositeConstruct,type,2,x,x):n==3?val(c->w,SpvOpCompositeConstruct,type,3,x,x,x):val(c->w,SpvOpCompositeConstruct,type,4,x,x,x,x);}
static uint32_t v4(Context *c,uint32_t a,uint32_t b,uint32_t d,uint32_t e){return val(c->w,SpvOpCompositeConstruct,c->v4,4,a,b,d,e);}
static uint32_t xyz(Context *c,uint32_t x){return val(c->w,SpvOpVectorShuffle,c->v3,5,x,x,0u,1u,2u);}
static uint32_t xy(Context *c,uint32_t x){return val(c->w,SpvOpVectorShuffle,c->v2,4,x,x,0u,1u);}
static uint32_t choose(Context *c,uint32_t type,uint32_t test,uint32_t a,uint32_t b){return val(c->w,SpvOpSelect,type,3,test,a,b);}
static uint32_t add(Context *c,uint32_t a,uint32_t b){return math(c,SpvOpFAdd,c->f,a,b);}
static uint32_t sub(Context *c,uint32_t a,uint32_t b){return math(c,SpvOpFSub,c->f,a,b);}
static uint32_t mul(Context *c,uint32_t a,uint32_t b){return math(c,SpvOpFMul,c->f,a,b);}
static uint32_t divf(Context *c,uint32_t a,uint32_t b){return math(c,SpvOpFDiv,c->f,a,b);}
static uint32_t unif_at(Context *c,unsigned which,uint32_t index)
{
    const unsigned width=uniforms[which].width;bool integer=uniforms[which].integer;
    uint32_t type=integer?((which==SIDE || which==ALPHA_KILL || which>=COMB_CTL)?c->u:width==4?c->iv4:width==2?c->iv2:c->i):width==4?c->v4:width==3?c->v3:width==2?c->v2:c->f;
    uint32_t ptr=integer?((which==SIDE || which==ALPHA_KILL || which>=COMB_CTL)?c->uu:width==4?c->uiv4:width==2?c->uiv2:c->ui):width==4?c->uv4:width==3?c->uniform_v3:width==2?c->uniform_v2:c->uf;
    unsigned member=uniform_member(c->state,which);
    uint32_t p=uniforms[which].count>1?val(c->w,SpvOpAccessChain,ptr,3,c->ubo,c->si[member],index):val(c->w,SpvOpAccessChain,ptr,2,c->ubo,c->si[member]);
    return unary(c,SpvOpLoad,type,p);
}
static uint32_t unif(Context *c,unsigned which,unsigned index)
{ return unif_at(c,which,c->si[index]); }
static void discard_if(Context *c,uint32_t test)
{
    uint32_t yes=id(c->w),next=id(c->w);op(c->w,SpvOpSelectionMerge,2,next,0u);op(c->w,SpvOpBranchConditional,3,test,yes,next);op(c->w,SpvOpLabel,1,yes);
    if(c->opts.spirv_1_6){op(c->w,SpvOpDemoteToHelperInvocation,0);op(c->w,SpvOpBranch,1,next);}else op(c->w,SpvOpKill,0);
    op(c->w,SpvOpLabel,1,next);
}
static uint32_t area(Context *c,uint32_t sample,uint32_t b,uint32_t d)
{
    /* area() arguments are ordinary arithmetic; kahan_det locals are precise. */
    c->precise=false;uint32_t a=binary(c,SpvOpFSub,c->v2,b,sample),q=binary(c,SpvOpFSub,c->v2,d,sample);
    uint32_t ax=extract(c,a,0),ay=extract(c,a,1),bx=extract(c,q,0),by=extract(c,q,1);
    c->precise=true;uint32_t cd=mul(c,ay,bx),negative=pin(c,unary(c,SpvOpFNegate,c->f,ay));
    uint32_t error=ext3(c,GLSLstd450Fma,c->f,negative,bx,cd);
    negative=pin(c,unary(c,SpvOpFNegate,c->f,cd));uint32_t result=add(c,ext3(c,GLSLstd450Fma,c->f,ax,by,negative),error);c->precise=false;return result;
}
static void barycentric(Context *c)
{
    bool ui=(c->state->depth_format&DEPTH_FORMAT_UI_CLAMP)!=0;
    uint32_t ui_skip=0,ui_merge=0;
    if(ui){
        uint32_t slope=unary(c,SpvOpLoad,c->f,c->inputs[8]);
        c->ui_rect=binary(c,SpvOpIEqual,c->b,unary(c,SpvOpBitcast,c->u,slope),c->ui_marker);
        uint32_t normal=id(c->w);ui_skip=id(c->w);ui_merge=id(c->w);
        op(c->w,SpvOpSelectionMerge,2,ui_merge,0u);
        op(c->w,SpvOpBranchConditional,3,c->ui_rect,ui_skip,normal);
        op(c->w,SpvOpLabel,1,normal);
    }
    uint32_t sample=xy(c,c->coord),scale=unary(c,SpvOpConvertSToF,c->v2,unif(c,SCALE,0));sample=binary(c,SpvOpFDiv,c->v2,sample,scale);
    uint32_t p[3];for(unsigned j=0;j<3;j++)p[j]=xy(c,c->positions[j]);
    uint32_t bc[3];for(unsigned j=0;j<3;j++)bc[j]=area(c,sample,p[(j+1)%3],p[(j+2)%3]);
    c->precise=true;uint32_t probe=add(c,add(c,bc[0],bc[1]),bc[2]);c->precise=false;
    uint32_t retry=binary(c,SpvOpLogicalOr,c->b,unary(c,SpvOpIsNan,c->b,probe),unary(c,SpvOpIsInf,c->b,probe));
    uint32_t yes=id(c->w),no=id(c->w),merge=id(c->w);op(c->w,SpvOpSelectionMerge,2,merge,0u);op(c->w,SpvOpBranchConditional,3,retry,yes,no);op(c->w,SpvOpLabel,1,yes);
    uint32_t maximum=ext2(c,GLSLstd450FMax,c->f,ext1(c,GLSLstd450FAbs,c->f,extract(c,p[0],0)),ext1(c,GLSLstd450FAbs,c->f,extract(c,p[0],1)));
    for(unsigned j=1;j<4;j++){uint32_t v=j==3?sample:p[j];uint32_t m=ext2(c,GLSLstd450FMax,c->f,ext1(c,GLSLstd450FAbs,c->f,extract(c,v,0)),ext1(c,GLSLstd450FAbs,c->f,extract(c,v,1)));maximum=ext2(c,GLSLstd450FMax,c->f,maximum,m);}
    uint32_t finite=binary(c,SpvOpLogicalAnd,c->b,binary(c,SpvOpFOrdGreaterThan,c->b,maximum,c->cf[F_ZERO]),unary(c,SpvOpLogicalNot,c->b,binary(c,SpvOpLogicalOr,c->b,unary(c,SpvOpIsInf,c->b,maximum),unary(c,SpvOpIsNan,c->b,maximum))));
    uint32_t inner=id(c->w),skip=id(c->w),im=id(c->w);op(c->w,SpvOpSelectionMerge,2,im,0u);op(c->w,SpvOpBranchConditional,3,finite,inner,skip);op(c->w,SpvOpLabel,1,inner);
    uint32_t bits=unary(c,SpvOpBitcast,c->u,maximum),exponent=binary(c,SpvOpShiftRightLogical,c->u,bits,c->ui32[23]);
    uint32_t power=ext2(c,GLSLstd450UMin,c->u,c->u127,binary(c,SpvOpISub,c->u,c->u313,exponent));
    uint32_t factor=unary(c,SpvOpBitcast,c->f,binary(c,SpvOpShiftLeftLogical,c->u,power,c->ui32[23]));
    c->precise=true;uint32_t vs=splat(c,c->v2,factor,2),sp=math(c,SpvOpFMul,c->v2,sample,vs),scaled[3];
    for(unsigned j=0;j<3;j++){scaled[j]=math(c,SpvOpFMul,c->v2,p[j],vs);}c->precise=false;
    uint32_t fixed[3];for(unsigned j=0;j<3;j++)fixed[j]=area(c,sp,scaled[(j+1)%3],scaled[(j+2)%3]);
    uint32_t last=c->w->block;op(c->w,SpvOpBranch,1,im);op(c->w,SpvOpLabel,1,skip);op(c->w,SpvOpBranch,1,im);op(c->w,SpvOpLabel,1,im);
    for(unsigned j=0;j<3;j++)fixed[j]=val(c->w,SpvOpPhi,c->f,4,fixed[j],last,bc[j],skip);
    last=c->w->block;op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,no);op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,merge);
    for(unsigned j=0;j<3;j++)bc[j]=val(c->w,SpvOpPhi,c->f,4,fixed[j],last,bc[j],no);
    c->precise=true;if(c->state->z_perspective)for(unsigned j=0;j<3;j++)bc[j]=divf(c,bc[j],extract(c,c->positions[j],3));c->precise=false;
    uint32_t inverse=divf(c,c->cf[F_ONE],add(c,add(c,bc[0],bc[1]),bc[2]));inverse=choose(c,c->f,unary(c,SpvOpIsInf,c->b,inverse),c->cf[F_ZERO],inverse);
    c->precise=true;bc[1]=mul(c,bc[1],inverse);bc[2]=mul(c,bc[2],inverse);
    if(ui){
        uint32_t last=c->w->block;
        op(c->w,SpvOpBranch,1,ui_merge);op(c->w,SpvOpLabel,1,ui_skip);
        op(c->w,SpvOpBranch,1,ui_merge);op(c->w,SpvOpLabel,1,ui_merge);
        bc[1]=val(c->w,SpvOpPhi,c->f,4,bc[1],last,c->cf[F_ZERO],ui_skip);
        bc[2]=val(c->w,SpvOpPhi,c->f,4,bc[2],last,c->cf[F_ZERO],ui_skip);
    }
    unsigned component=c->state->z_perspective?3:2;uint32_t z0=extract(c,c->positions[0],component),z1=extract(c,c->positions[1],component),z2=extract(c,c->positions[2],component);
    c->z=add(c,z0,add(c,mul(c,bc[1],sub(c,z1,z0)),mul(c,bc[2],sub(c,z2,z0))));c->precise=false;
    uint32_t offset=unif(c,OFFSET,0),fz=unif(c,FACTOR,0),slope=unary(c,SpvOpLoad,c->f,c->inputs[8]);
    if(c->state->z_perspective){
        uint32_t positive=binary(c,SpvOpFOrdGreaterThan,c->b,c->z,c->cf[F_ZERO]);yes=id(c->w);no=id(c->w);merge=id(c->w);
        op(c->w,SpvOpSelectionMerge,2,merge,0u);op(c->w,SpvOpBranchConditional,3,positive,yes,no);op(c->w,SpvOpLabel,1,yes);
        uint32_t ofs=mul(c,mul(c,mul(c,fz,slope),c->z),c->z);c->precise=true;uint32_t z=add(c,add(c,c->z,offset),ofs);c->precise=false;
        op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,no);op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,merge);c->z=val(c->w,SpvOpPhi,c->f,4,z,yes,c->cf[F_FLOAT_MAX],no);
        c->z=choose(c,c->f,unary(c,SpvOpIsNan,c->b,c->z),c->cf[F_FLOAT_MAX],c->z);
    }else{c->precise=true;c->z=add(c,add(c,c->z,offset),mul(c,fz,slope));c->precise=false;}
    if(!has_dot_depth(c->state)){
    uint32_t low=extract(c,c->clip,2),high=extract(c,c->clip,3);
    if(c->state->depth_clipping)discard_if(c,binary(c,SpvOpLogicalOr,c->b,binary(c,SpvOpFOrdLessThan,c->b,c->z,low),binary(c,SpvOpFOrdLessThan,c->b,high,c->z)));
    else{c->precise=true;c->z=ext3(c,GLSLstd450FClamp,c->f,c->z,low,high);c->precise=false;}
    }
}

static bool source_valid(unsigned byte,bool ef)
{ unsigned r=byte&15;return r!=6 && r!=7 && (ef || r!=PS_REGISTER_EF_PROD); }
static bool dest_valid(unsigned r)
{ return r==0 || r==4 || r==5 || (r>=8 && r<=13); }
bool pgraph_spv_psh_supported(const PshState *s,SpvPshOptions opts)
{
    if(opts.ubo_binding>=32 || (!s->combiner_dynamic && ((s->combiner_control&255)>8 || !(s->final_inputs_0 || s->final_inputs_1))) || (s->g8b8_mip_flags && !has_signed_gb(s)) || ((s->depth_format&DEPTH_FORMAT_UI_CLAMP) && !pgraph_glsl_ui_clamp_stage(s)))return false;
    bool dots[4]={false};
    for(unsigned j=0;j<4;j++){
        unsigned mode=texture_mode(s,j);
        if(dot_mode(mode)){
            if(!j || texture_input(s,j)>=j)return false;
            if(mode==PS_TEXTUREMODES_DOTPRODUCT){if(j!=1 && j!=2)return false;}
            else{
                if(j<2 || !dots[j-1])return false;
                if(mode==PS_TEXTUREMODES_DOT_RFLCT_DIFF){if(j!=2 || texture_input(s,3)>=j)return false;}
                if(mode==PS_TEXTUREMODES_DOT_RFLCT_SPEC || mode==PS_TEXTUREMODES_DOT_STR_3D || mode==PS_TEXTUREMODES_DOT_STR_CUBE){if(j!=3 || !dots[j-2])return false;}
                if(mode==PS_TEXTUREMODES_DOT_ST && (s->dim_tex[j]!=2 || s->border_adjust[j]))return false;
            }
            dots[j]=true;
        }
        if(s->g8b8_signed[j] && (!(s->g8b8_signed[j]&15) || (s->g8b8_signed[j]&~255u) || mode!=PS_TEXTUREMODES_PROJECT2D || s->dim_tex[j]!=2 || s->tex_cubemap[j] || s->shadow_map[j] || s->tex_x8y24[j] || s->border_adjust[j]))return false;
        if((s->tex_x8y24[j] || s->shadow_map[j]) && !constant_shadow(s,j) && !sampled_shadow(s,j) && !raw_color(s,j) && !(mode==PS_TEXTUREMODES_PROJECT2D && s->dim_tex[j]==3 && !s->shadow_map[j]))return false;
        if(dependent_stage(s,j)){
            if(!j || texture_input(s,j)>=j)return false;
            if(!bump_stage(s,j) && (s->dim_tex[j]!=2 || s->rect_tex[j] || s->border_adjust[j]))return false;
        }
        if(constant_shadow(s,j)){
            if(s->dim_tex[j]!=2 || s->colorkey_mode[j]<COLOR_KEY_NONE || s->colorkey_mode[j]>COLOR_KEY_DISCARD)return false;
        }else if(sample_stage(s,j)){
            if(opts.tex_binding>=29 || (opts.ubo_binding>=opts.tex_binding && opts.ubo_binding<opts.tex_binding+4) || s->colorkey_mode[j]<COLOR_KEY_NONE || s->colorkey_mode[j]>COLOR_KEY_DISCARD)return false;
            if(sampled_shadow(s,j)){if(s->dim_tex[j]!=2 || s->tex_cubemap[j] || (f16_shadow(s,j) && s->tex_x8y24[j]))return false;}
            else if(raw_color(s,j)){
                if(mode==PS_TEXTUREMODES_PROJECT2D){
                    if(s->conv_tex[j]==CONVOLUTION_FILTER_DISABLED){if(s->tex_cubemap[j])return false;}
                    else if(s->conv_tex[j]!=CONVOLUTION_FILTER_GAUSSIAN && s->conv_tex[j]!=CONVOLUTION_FILTER_QUINCUNX)return false;
                }
            }
            else if(cube_stage(s,j)){if(s->dim_tex[j]!=2)return false;}
            else{
                if(s->dim_tex[j]!=2 && s->dim_tex[j]!=3)return false;
                /* The reference rectangle helper is 2D, including cube sizes. */
                if(s->dim_tex[j]==3 && s->rect_tex[j])return false;
                if(mode==PS_TEXTUREMODES_PROJECT2D && s->conv_tex[j]!=CONVOLUTION_FILTER_DISABLED){
                    if(s->dim_tex[j]!=2 || s->tex_cubemap[j] || (s->conv_tex[j]!=CONVOLUTION_FILTER_GAUSSIAN && s->conv_tex[j]!=CONVOLUTION_FILTER_QUINCUNX))return false;
                }
            }
        }else if(mode==PS_TEXTUREMODES_PASSTHRU){if(s->border_adjust[j])return false;}
        else if(mode!=PS_TEXTUREMODES_NONE && mode!=PS_TEXTUREMODES_CLIPPLANE && !dot_mode(mode))return false;
    }
    if(s->point_sprite && s->rect_tex[3])return false;
    for(unsigned j=0;j<3;j++)if(((s->other_stage_input>>(j*4))&15)>7)return false;
    if(s->combiner_dynamic)return true;
    bool r0=false,mux=false;
    for(unsigned j=0;j<(s->combiner_control&255);j++)for(unsigned a=0;a<2;a++){
        uint32_t inputs=a?s->alpha_inputs[j]:s->rgb_inputs[j],out=a?s->alpha_outputs[j]:s->rgb_outputs[j];
        for(unsigned k=0;k<4;k++){unsigned b=(inputs>>(k*8))&255;if(!source_valid(b,false))return false;r0|=(b&15)==12 || (b&15)==14;}
        for(unsigned k=0;k<3;k++){unsigned d=(out>>(k*4))&15;if(!dest_valid(d))return false;r0|=d==12;}
        mux|=(out&(4u<<12)) && ((out>>8)&15);
    }
    for(unsigned k=0;k<7;k++){
        unsigned b=k<4?(s->final_inputs_0>>(24-k*8))&255:(s->final_inputs_1>>(24-(k-4)*8))&255;
        if(!source_valid(b,k<4 || k==6)){return false;}
        r0|=(b&15)==12 || (b&15)==14;
    }
    return !mux || r0;
}
static uint32_t reg_value(Context *c,unsigned r,unsigned stage)
{
    if(r==PS_REGISTER_C0 || r==PS_REGISTER_C1){unsigned flags=c->state->combiner_control>>8,which=r==PS_REGISTER_C1;unsigned unique=which?PS_COMBINERCOUNT_UNIQUE_C1:PS_COMBINERCOUNT_UNIQUE_C0;return unif(c,CONSTANTS,(stage==8 || (flags&unique)?stage:0)*2+which);}
    if(r==PS_REGISTER_EF_PROD)return v4(c,extract(c,c->ef,0),extract(c,c->ef,1),extract(c,c->ef,2),c->cf[F_ZERO]);
    if(r==PS_REGISTER_V1R0_SUM){
        unsigned flags=c->state->final_inputs_1&255;uint32_t a=c->r[PS_REGISTER_V1],b=c->r[PS_REGISTER_R0],one=splat(c,c->v4,c->cf[F_ONE],4);
        if(flags&PS_FINALCOMBINERSETTING_COMPLEMENT_V1)a=binary(c,SpvOpFSub,c->v4,one,a);
        if(flags&PS_FINALCOMBINERSETTING_COMPLEMENT_R0)b=binary(c,SpvOpFSub,c->v4,one,b);
        uint32_t sum=binary(c,SpvOpFAdd,c->v3,xyz(c,a),xyz(c,b)),v=v4(c,extract(c,sum,0),extract(c,sum,1),extract(c,sum,2),c->cf[F_ZERO]);
        return flags&PS_FINALCOMBINERSETTING_CLAMP_SUM?ext3(c,GLSLstd450FClamp,c->v4,v,splat(c,c->v4,c->cf[F_ZERO],4),one):v;
    }
    return c->r[r];
}
static uint32_t input_value(Context *c,unsigned byte,bool alpha,unsigned stage)
{
    unsigned n=alpha?1:3;uint32_t type=alpha?c->f:c->v3,reg=reg_value(c,byte&15,stage);
    uint32_t x=alpha?extract(c,reg,byte&PS_CHANNEL_ALPHA?3:2):byte&PS_CHANNEL_ALPHA?splat(c,c->v3,extract(c,reg,3),3):xyz(c,reg);
    unsigned mod=byte&0xe0;if(mod==PS_INPUTMAPPING_SIGNED_IDENTITY)return x;
    if(mod==PS_INPUTMAPPING_SIGNED_NEGATE)return unary(c,SpvOpFNegate,type,x);
    uint32_t zero=splat(c,type,c->cf[F_ZERO],n),one=splat(c,type,c->cf[F_ONE],n),half=splat(c,type,c->cf[F_HALF],n),two=splat(c,type,c->cf[F_TWO],n);
    if(mod==PS_INPUTMAPPING_UNSIGNED_INVERT)return binary(c,SpvOpFSub,type,one,ext3(c,GLSLstd450FClamp,type,x,zero,one));
    x=ext2(c,GLSLstd450FMax,type,x,zero);
    if(mod==PS_INPUTMAPPING_UNSIGNED_IDENTITY)return x;
    if(mod==PS_INPUTMAPPING_EXPAND_NORMAL)return binary(c,SpvOpFSub,type,binary(c,SpvOpFMul,type,two,x),one);
    if(mod==PS_INPUTMAPPING_EXPAND_NEGATE)return binary(c,SpvOpFAdd,type,binary(c,SpvOpFMul,type,unary(c,SpvOpFNegate,type,two),x),one);
    if(mod==PS_INPUTMAPPING_HALFBIAS_NORMAL)return binary(c,SpvOpFSub,type,x,half);
    return binary(c,SpvOpFAdd,type,unary(c,SpvOpFNegate,type,x),half);
}
static uint32_t map_output(Context *c,uint32_t x,unsigned mapping,bool alpha)
{
    unsigned n=alpha?1:3;uint32_t type=alpha?c->f:c->v3;
    if(mapping==PS_COMBINEROUTPUT_BIAS || mapping==PS_COMBINEROUTPUT_SHIFTLEFT_1_BIAS)x=binary(c,SpvOpFSub,type,x,splat(c,type,c->cf[F_HALF],n));
    if(mapping==PS_COMBINEROUTPUT_SHIFTLEFT_1 || mapping==PS_COMBINEROUTPUT_SHIFTLEFT_1_BIAS)x=binary(c,SpvOpFMul,type,x,splat(c,type,c->cf[F_TWO],n));
    if(mapping==PS_COMBINEROUTPUT_SHIFTLEFT_2)x=binary(c,SpvOpFMul,type,x,splat(c,type,c->cf[F_FOUR],n));
    if(mapping==PS_COMBINEROUTPUT_SHIFTRIGHT_1){x=binary(c,SpvOpFDiv,type,x,splat(c,type,c->cf[F_TWO],n));}
    return x;
}
typedef struct Stage {uint32_t ab,cd,mux;unsigned out;bool alpha;} Stage;
static Stage stage_values(Context *c,unsigned stage,bool alpha)
{
    uint32_t encoded=alpha?c->state->alpha_inputs[stage]:c->state->rgb_inputs[stage],out=alpha?c->state->alpha_outputs[stage]:c->state->rgb_outputs[stage];
    uint32_t a=input_value(c,encoded>>24,alpha,stage),b=input_value(c,(encoded>>16)&255,alpha,stage),d=input_value(c,(encoded>>8)&255,alpha,stage),e=input_value(c,encoded&255,alpha,stage);
    unsigned flags=out>>12;uint32_t type=alpha?c->f:c->v3;
    uint32_t ab=(!alpha && (flags&PS_COMBINEROUTPUT_AB_DOT_PRODUCT))?splat(c,type,binary(c,SpvOpDot,c->f,a,b),3):binary(c,SpvOpFMul,type,a,b);
    uint32_t cd=(!alpha && (flags&PS_COMBINEROUTPUT_CD_DOT_PRODUCT))?splat(c,type,binary(c,SpvOpDot,c->f,d,e),3):binary(c,SpvOpFMul,type,d,e),mux=0;
    if((out>>8)&15){
        if(flags&PS_COMBINEROUTPUT_AB_CD_MUX){
            uint32_t r0=extract(c,c->r[PS_REGISTER_R0],3),test;
            if((c->state->combiner_control>>8)&PS_COMBINERCOUNT_MUX_MSB)test=binary(c,SpvOpFOrdGreaterThanEqual,c->b,r0,c->cf[F_HALF]);
            else{uint32_t bits=unary(c,SpvOpConvertFToU,c->u,mul(c,r0,c->cf[F_BYTE_MAX]));test=binary(c,SpvOpIEqual,c->b,binary(c,SpvOpBitwiseAnd,c->u,bits,c->ui32[1]),c->ui32[1]);}
            mux=choose(c,type,test,cd,ab);
        }else mux=binary(c,SpvOpFAdd,type,ab,cd);
        mux=map_output(c,mux,flags&0x38,alpha);
    }
    return (Stage){map_output(c,ab,flags&0x38,alpha),map_output(c,cd,flags&0x38,alpha),mux,out,alpha};
}
static void stage_store(Context *c,Stage s)
{
    uint32_t values[]={s.ab,s.cd,s.mux},type=s.alpha?c->f:c->v3;unsigned n=s.alpha?1:3;
    uint32_t one=splat(c,type,c->cf[F_ONE],n),negative=unary(c,SpvOpFNegate,type,one);
    for(unsigned j=0;j<3;j++){
        unsigned r=(s.out>>(j==0?4:j==1?0:8))&15;if(!r)continue;uint32_t x=ext3(c,GLSLstd450FClamp,type,values[j],negative,one),old=c->r[r];
        if(s.alpha)c->r[r]=v4(c,extract(c,old,0),extract(c,old,1),extract(c,old,2),x);
        else{uint32_t a=extract(c,old,3);if(j<2 && ((s.out>>12)&(j==0?PS_COMBINEROUTPUT_AB_BLUE_TO_ALPHA:PS_COMBINEROUTPUT_CD_BLUE_TO_ALPHA)))a=extract(c,x,2);c->r[r]=v4(c,extract(c,x,0),extract(c,x,1),extract(c,x,2),a);}
    }
}
/* Existing geometry owns the marker and UV bounds; lend only SSA IDs. */
static uint32_t ui_clamp_coord(Context *c,uint32_t coord,unsigned unit)
{
    if(!(c->state->depth_format&DEPTH_FORMAT_UI_CLAMP) || pgraph_glsl_ui_clamp_stage(c->state)!=unit+1)return coord;
    uint32_t uv=ext3(c,GLSLstd450FClamp,c->v2,xy(c,coord),xy(c,c->positions[1]),xy(c,c->positions[2]));
    uint32_t clamped=v4(c,extract(c,uv,0),extract(c,uv,1),extract(c,coord,2),extract(c,coord,3));
    return choose(c,c->v4,c->ui_rect,clamped,coord);
}
#include "psh-texture.inc"
#include "psh-raw.inc"
#include "psh-dot.inc"
#include "psh-f16.inc"
#include "psh-shadow.inc"
#include "psh-g8b8.inc"

static void texture_finish(Context *c,uint32_t color,unsigned j)
{
    c->r[PS_REGISTER_T0+j]=color;
    if(c->state->alpha_dynamic || c->state->alphakill[j]){
        uint32_t kill=binary(c,SpvOpFOrdEqual,c->b,extract(c,color,3),c->cf[F_ZERO]);
        if(c->state->alpha_dynamic){uint32_t mask=unif(c,ALPHA_KILL,0);mask=binary(c,SpvOpBitwiseAnd,c->u,mask,c->ui32[1u<<j]);kill=binary(c,SpvOpLogicalAnd,c->b,binary(c,SpvOpINotEqual,c->b,mask,c->ui32[0]),kill);}discard_if(c,kill);
    }
    c->r[PS_REGISTER_T0+j]=texture_color_key(c,color,j);
    if(c->state->g8b8_signed[j])c->signed_gb[j]=gb_stage(c,c->r[PS_REGISTER_T0+j],c->gb_coord[j],c->gb_sample[j],j);
}
static void texture_stages(Context *c)
{
    for(unsigned j=0;j<4;j++){
        unsigned mode=texture_mode(c->state,j);if(mode==PS_TEXTUREMODES_NONE)continue;
        if(constant_shadow(c->state,j)){
            uint32_t color=splat(c,c->v4,c->cf[c->state->shadow_depth_func==SHADOW_DEPTH_FUNC_ALWAYS?F_ONE:F_ZERO],4);texture_finish(c,color,j);continue;
        }
        uint32_t coord;
        if(j==3 && c->state->point_sprite){
            uint32_t point=unary(c,SpvOpLoad,c->v2,c->point_coord);
            coord=v4(c,extract(c,point,0),extract(c,point,1),c->cf[F_ONE],c->cf[F_ONE]);
        }else coord=unary(c,SpvOpLoad,c->v4,c->tex_inputs[j]);
        coord=ui_clamp_coord(c,coord,j);
        if(c->state->g8b8_signed[j])c->gb_coord[j]=coord;
        if(mode==PS_TEXTUREMODES_DOTPRODUCT || mode==PS_TEXTUREMODES_DOT_ZW){
            c->dots[j]=texture_dot(c,coord,j);c->r[PS_REGISTER_T0+j]=splat(c,c->v4,c->cf[F_ZERO],4);
            if(mode==PS_TEXTUREMODES_DOT_ZW)texture_dot_depth(c,j);
        }else if(mode==PS_TEXTUREMODES_PASSTHRU)c->r[PS_REGISTER_T0+j]=coord;
        else if(mode==PS_TEXTUREMODES_CLIPPLANE){
            c->r[PS_REGISTER_T0+j]=splat(c,c->v4,c->cf[F_ZERO],4);
            for(unsigned k=0;k<4;k++)discard_if(c,binary(c,c->state->compare_mode[j][k]?SpvOpFOrdGreaterThanEqual:SpvOpFOrdLessThan,c->b,extract(c,coord,k),c->cf[F_ZERO]));
        }else{
            unsigned kind=sampler_kind(c->state,j);
            if(!sampled_shadow(c->state,j) && !dependent_stage(c->state,j) && !dot_mode(mode) && mode!=PS_TEXTUREMODES_CUBEMAP)coord=texture_border(c,coord,j);
            uint32_t sample=unary(c,SpvOpLoad,texture_sampled_type(c,kind),c->samplers[j]),uv=xy(c,coord);
            if(c->state->g8b8_signed[j])c->gb_sample[j]=sample;
            if(c->state->rect_tex[j] && mode!=PS_TEXTUREMODES_CUBEMAP && !sampled_shadow(c->state,j) && !dependent_stage(c->state,j) && !dot_mode(mode)){
                uint32_t image=unary(c,SpvOpImage,texture_image_type(c,kind),sample),size=val(c->w,SpvOpImageQuerySizeLod,c->iv2,2,image,c->si[0]);
                uint32_t scaled=binary(c,SpvOpFDiv,c->v2,unary(c,SpvOpConvertSToF,c->v2,size),splat(c,c->v2,unif(c,TEX_SCALE,j),2));
                uv=binary(c,SpvOpFDiv,c->v2,uv,scaled);
            }
            uint32_t color;
            if(sampled_shadow(c->state,j))color=texture_shadow(c,coord,sample,kind,j);
            else if(raw_color(c->state,j))color=texture_raw_color(c,coord,uv,sample,j);
            else if(dot_mode(mode))color=texture_dot_sample(c,coord,sample,kind,j);
            else if(dependent_stage(c->state,j))color=texture_dependent(c,coord,sample,kind,j);
            else if(mode==PS_TEXTUREMODES_CUBEMAP){
                uint32_t lookup=kind==2?xyz(c,coord):texture_cube_to_2d(c,xyz(c,coord));
                color=val(c->w,SpvOpImageSampleImplicitLod,c->v4,2,sample,lookup);
            }else if(mode==PS_TEXTUREMODES_PROJECT3D || kind==1){
                uint32_t projected=coord;
                if(mode==PS_TEXTUREMODES_PROJECT2D || c->state->rect_tex[j])projected=v4(c,extract(c,uv,0),extract(c,uv,1),c->cf[F_ZERO],extract(c,coord,3));
                /* Match the reference textureProj instruction and coordinate width. */
                color=val(c->w,SpvOpImageSampleProjImplicitLod,c->v4,2,sample,projected);
            }else if(kind==2){
                uint32_t projected=val(c->w,SpvOpCompositeConstruct,c->v3,3,extract(c,uv,0),extract(c,uv,1),extract(c,coord,3));
                uint32_t direction=texture_2d_to_cube(c,projected);
                color=val(c->w,SpvOpImageSampleImplicitLod,c->v4,2,sample,direction);
            }else if(convolution_stage(c->state,j))color=texture_convolution(c,sample,uv,extract(c,coord,3),kind);
            else{
                uint32_t q=ext2(c,GLSLstd450FMax,c->f,extract(c,coord,3),c->min_q);
                uv=binary(c,SpvOpFDiv,c->v2,uv,splat(c,c->v2,q,2));
                color=val(c->w,SpvOpImageSampleImplicitLod,c->v4,2,sample,uv);
            }
            texture_finish(c,color,j);
        }
    }
    if(has_signed_gb(c->state))gb_remap(c);
    /* GLSL initializes R0 alpha from T0 when the first stage is active. */
    if(texture_mode(c->state,0)!=PS_TEXTUREMODES_NONE)c->r[PS_REGISTER_R0]=v4(c,c->cf[F_ZERO],c->cf[F_ZERO],c->cf[F_ZERO],extract(c,c->r[PS_REGISTER_T0],3));
}
#include "psh-combiner.inc"

static uint32_t combiners(Context *c)
{
    uint32_t zero=splat(c,c->v4,c->cf[F_ZERO],4);for(unsigned j=0;j<16;j++)c->r[j]=zero;
    uint32_t use_back=binary(c,SpvOpLogicalAnd,c->b,binary(c,SpvOpINotEqual,c->b,unif(c,SIDE,0),c->ui32[0]),unary(c,SpvOpLogicalNot,c->b,unary(c,SpvOpLoad,c->b,c->front)));
    c->r[PS_REGISTER_V0]=choose(c,c->v4,use_back,unary(c,SpvOpLoad,c->v4,c->inputs[2]),unary(c,SpvOpLoad,c->v4,c->inputs[0]));
    c->r[PS_REGISTER_V1]=choose(c,c->v4,use_back,unary(c,SpvOpLoad,c->v4,c->inputs[3]),unary(c,SpvOpLoad,c->v4,c->inputs[1]));
    uint32_t fog=unif(c,FOG,0),alpha=ext3(c,GLSLstd450FClamp,c->f,unary(c,SpvOpLoad,c->f,c->inputs[4]),c->cf[F_ZERO],c->cf[F_ONE]);
    c->r[PS_REGISTER_FOG]=v4(c,extract(c,fog,0),extract(c,fog,1),extract(c,fog,2),alpha);
    for(unsigned j=8;j<=12;j++)c->r[j]=v4(c,c->cf[F_ZERO],c->cf[F_ZERO],c->cf[F_ZERO],c->cf[F_ONE]);
    texture_stages(c);
    if(c->state->combiner_dynamic)return dynamic_combiner(c);
    for(unsigned j=0;j<(c->state->combiner_control&255);j++){Stage rgb=stage_values(c,j,false),a=stage_values(c,j,true);stage_store(c,rgb);stage_store(c,a);}
    uint32_t in0=c->state->final_inputs_0,in1=c->state->final_inputs_1;
    uint32_t e=input_value(c,in1>>24,false,8),f=input_value(c,(in1>>16)&255,false,8);c->ef=binary(c,SpvOpFMul,c->v3,e,f);
    uint32_t a=input_value(c,in0>>24,false,8),b=input_value(c,(in0>>16)&255,false,8),d=input_value(c,(in0>>8)&255,false,8),g=input_value(c,in0&255,false,8),alpha_out=input_value(c,(in1>>8)&255,true,8);
    uint32_t rgb=binary(c,SpvOpFAdd,c->v3,g,ext3(c,GLSLstd450FMix,c->v3,d,b,a));return v4(c,extract(c,rgb,0),extract(c,rgb,1),extract(c,rgb,2),alpha_out);
}
static void window_clip(Context *c)
{
    uint32_t x=sub(c,extract(c,c->coord,0),c->cf[F_HALF]),y=sub(c,extract(c,c->coord,1),c->cf[F_HALF]);
    uint32_t entry=c->w->block,header=id(c->w),body=id(c->w),cont=id(c->w),merge=id(c->w),index=id(c->w),next=id(c->w);
    op(c->w,SpvOpBranch,1,header);op(c->w,SpvOpLabel,1,header);op(c->w,SpvOpPhi,6,c->i,index,c->si[0],entry,next,cont);
    uint32_t within=binary(c,SpvOpSLessThan,c->b,index,c->si[8]);op(c->w,SpvOpLoopMerge,3,merge,cont,0u);op(c->w,SpvOpBranchConditional,3,within,body,merge);op(c->w,SpvOpLabel,1,body);
    uint32_t r=unary(c,SpvOpConvertSToF,c->v4,unif_at(c,REGION,index));
    uint32_t outside=binary(c,SpvOpLogicalOr,c->b,binary(c,SpvOpFOrdLessThan,c->b,x,extract(c,r,0)),binary(c,SpvOpFOrdLessThan,c->b,y,extract(c,r,1)));
    outside=binary(c,SpvOpLogicalOr,c->b,outside,binary(c,SpvOpFOrdGreaterThanEqual,c->b,x,extract(c,r,2)));outside=binary(c,SpvOpLogicalOr,c->b,outside,binary(c,SpvOpFOrdGreaterThanEqual,c->b,y,extract(c,r,3)));
    uint32_t inside=unary(c,SpvOpLogicalNot,c->b,outside),found=0;
    if(c->state->window_clip_exclusive)discard_if(c,inside);
    else{
        found=id(c->w);uint32_t after=id(c->w);op(c->w,SpvOpSelectionMerge,2,after,0u);op(c->w,SpvOpBranchConditional,3,inside,found,after);
        op(c->w,SpvOpLabel,1,found);op(c->w,SpvOpBranch,1,merge);op(c->w,SpvOpLabel,1,after);
    }
    op(c->w,SpvOpBranch,1,cont);op(c->w,SpvOpLabel,1,cont);op(c->w,SpvOpIAdd,4,c->i,next,index,c->si[1]);op(c->w,SpvOpBranch,1,header);op(c->w,SpvOpLabel,1,merge);
    if(!c->state->window_clip_exclusive){uint32_t contained=val(c->w,SpvOpPhi,c->b,4,c->yes,found,c->no,header);discard_if(c,unary(c,SpvOpLogicalNot,c->b,contained));}
}
static void alpha_test(Context *c,uint32_t color)
{
    if(!c->state->alpha_dynamic && !c->state->alpha_test)return;
    uint32_t alpha_merge=0;
    if(c->state->alpha_dynamic){
        uint32_t enabled=binary(c,SpvOpINotEqual,c->b,unif(c,ALPHA_ENABLE,0),c->si[0]),yes=id(c->w);alpha_merge=id(c->w);
        op(c->w,SpvOpSelectionMerge,2,alpha_merge,0u);op(c->w,SpvOpBranchConditional,3,enabled,yes,alpha_merge);op(c->w,SpvOpLabel,1,yes);
    }
    uint32_t a=unary(c,SpvOpConvertFToS,c->i,ext1(c,GLSLstd450Round,c->f,mul(c,extract(c,color,3),c->cf[F_BYTE_MAX]))),ref=unif(c,ALPHA_REF,0),func=unif(c,ALPHA_FUNC,0),pass=c->yes;
    const SpvOp comparison[]={SpvOpNop,SpvOpSLessThan,SpvOpIEqual,SpvOpSLessThanEqual,SpvOpSGreaterThan,SpvOpINotEqual,SpvOpSGreaterThanEqual};
    for(int j=6;j>=0;j--){uint32_t value=j?binary(c,comparison[j],c->b,a,ref):c->no;pass=choose(c,c->b,binary(c,SpvOpIEqual,c->b,func,c->si[j]),value,pass);}
    uint32_t fail=unary(c,SpvOpLogicalNot,c->b,pass);
    discard_if(c,fail);
    if(alpha_merge){op(c->w,SpvOpBranch,1,alpha_merge);op(c->w,SpvOpLabel,1,alpha_merge);}
}

static void allocate(Context *c)
{
    uint32_t *types[]={&c->void_t,&c->fn,&c->f,&c->v2,&c->v3,&c->v4,&c->i,&c->u,&c->b,&c->iv2,&c->iv4,&c->in_f,&c->in_v4,&c->in_b,&c->out_f,&c->out_v4,&c->uf,&c->uv4,&c->uiv2,&c->uiv4,&c->ui,&c->uu,&c->region_array,&c->const_array,&c->ubo_type,&c->ubo_ptr};
    for(unsigned j=0;j<sizeof(types)/sizeof(types[0]);j++)*types[j]=id(c->w);
    for(unsigned j=0;j<32;j++){c->si[j]=id(c->w);c->ui32[j]=id(c->w);}
    for(unsigned j=0;j<FLOATS;j++)c->cf[j]=id(c->w);
    c->u127=id(c->w);c->u313=id(c->w);c->yes=id(c->w);c->no=id(c->w);
    for(unsigned j=0;j<9;j++)c->inputs[j]=id(c->w);
    c->frag_coord=id(c->w);c->front=id(c->w);c->color=id(c->w);c->depth=id(c->w);c->ubo=id(c->w);c->entry=id(c->w);c->ext=id(c->w);
    for(unsigned j=0;j<4;j++)if((texture_mode(c->state,j)!=PS_TEXTUREMODES_NONE && !constant_shadow(c->state,j)) || (j==3 && texture_mode(c->state,2)==PS_TEXTUREMODES_DOT_RFLCT_DIFF)){
        if(j==3 && c->state->point_sprite){c->point_coord=id(c->w);c->in_v2=id(c->w);}else c->tex_inputs[j]=id(c->w);
        if(sample_stage(c->state,j))c->samplers[j]=id(c->w);
    }
    if(has_sampler_kind(c->state,0)){c->image_t=id(c->w);c->sampled_t=id(c->w);c->sampler_ptr=id(c->w);c->min_q=id(c->w);}
    else for(unsigned j=0;j<4;j++)if(raw_color(c->state,j) && texture_mode(c->state,j)==PS_TEXTUREMODES_PROJECT2D && !convolution_stage(c->state,j)){c->min_q=id(c->w);break;}
    if(has_texture_scale(c->state) || has_bump_luma(c->state))c->scale_array=id(c->w);
    if(c->state->combiner_dynamic)dynamic_allocate(c);
    texture_allocate(c);dot_allocate(c);
    if(c->state->depth_format&DEPTH_FORMAT_UI_CLAMP)c->ui_marker=id(c->w);
}
static void emit(Writer *w,const PshState *state,SpvPshOptions opts)
{
    Context c={.w=w,.state=state,.opts=opts};allocate(&c);
    unsigned interfaces=0;for(unsigned j=0;j<4;j++)interfaces+=!!c.tex_inputs[j]+!!c.samplers[j];interfaces+=!!c.point_coord;if(state->combiner_dynamic)interfaces+=5;if(has_convolution(state))interfaces+=2;
    word(w,SpvMagicNumber);word(w,opts.spirv_1_6?0x00010600u:0x00010500u);word(w,0);word(w,0);word(w,0);
    op(w,SpvOpCapability,1,(uint32_t)SpvCapabilityShader);
    if(has_rect_samples(state) || has_convolution(state) || has_f16_shadow(state) || has_signed_query(state))op(w,SpvOpCapability,1,(uint32_t)SpvCapabilityImageQuery);
    if(opts.spirv_1_6)op(w,SpvOpCapability,1,(uint32_t)SpvCapabilityDemoteToHelperInvocation);
    op(w,SpvOpExtInstImport,5,c.ext,0x4c534c47u,0x6474732eu,0x3035342eu,0u);
    op(w,SpvOpMemoryModel,2,(uint32_t)SpvAddressingModelLogical,(uint32_t)SpvMemoryModelGLSL450);
    word(w,((19u+interfaces)<<16)|SpvOpEntryPoint);word(w,SpvExecutionModelFragment);word(w,c.entry);word(w,0x6e69616du);word(w,0);
    for(unsigned j=0;j<9;j++){word(w,c.inputs[j]);}word(w,c.frag_coord);word(w,c.front);word(w,c.color);word(w,c.depth);word(w,c.ubo);
    for(unsigned j=0;j<4;j++){if(c.tex_inputs[j])word(w,c.tex_inputs[j]);if(c.samplers[j])word(w,c.samplers[j]);}if(c.point_coord)word(w,c.point_coord);
    if(state->combiner_dynamic){for(unsigned j=0;j<4;j++)word(w,c.maps[j]);word(w,c.bank);}
    if(has_convolution(state)){word(w,c.conv_weights);word(w,c.conv_offsets);}
    op(w,SpvOpExecutionMode,2,c.entry,(uint32_t)SpvExecutionModeOriginUpperLeft);op(w,SpvOpExecutionMode,2,c.entry,(uint32_t)SpvExecutionModeDepthReplacing);
    for(unsigned j=0;j<9;j++){
        unsigned location=j<5?j:j<8?j+4:12;op(w,SpvOpDecorate,3,c.inputs[j],(uint32_t)SpvDecorationLocation,location);
        if(j>=5 || (j<4 && !state->smooth_shading))op(w,SpvOpDecorate,2,c.inputs[j],(uint32_t)SpvDecorationFlat);
    }
    for(unsigned j=0;j<4;j++){
        if(c.tex_inputs[j])op(w,SpvOpDecorate,3,c.tex_inputs[j],(uint32_t)SpvDecorationLocation,5u+j);
        if(c.samplers[j]){op(w,SpvOpDecorate,3,c.samplers[j],(uint32_t)SpvDecorationDescriptorSet,0u);op(w,SpvOpDecorate,3,c.samplers[j],(uint32_t)SpvDecorationBinding,opts.tex_binding+j);}
    }
    if(c.point_coord)op(w,SpvOpDecorate,3,c.point_coord,(uint32_t)SpvDecorationBuiltIn,(uint32_t)SpvBuiltInPointCoord);
    op(w,SpvOpDecorate,3,c.frag_coord,(uint32_t)SpvDecorationBuiltIn,(uint32_t)SpvBuiltInFragCoord);
    op(w,SpvOpDecorate,3,c.front,(uint32_t)SpvDecorationBuiltIn,(uint32_t)SpvBuiltInFrontFacing);
    op(w,SpvOpDecorate,3,c.depth,(uint32_t)SpvDecorationBuiltIn,(uint32_t)SpvBuiltInFragDepth);
    op(w,SpvOpDecorate,3,c.color,(uint32_t)SpvDecorationLocation,0u);
    op(w,SpvOpDecorate,2,c.ubo_type,(uint32_t)SpvDecorationBlock);
    for(unsigned j=0,member=0;j<UNIFORMS;j++)if(uniform_enabled(state,j))op(w,SpvOpMemberDecorate,4,c.ubo_type,member++,(uint32_t)SpvDecorationOffset,uniform_offset(state,j));
    if(c.scale_array)op(w,SpvOpDecorate,3,c.scale_array,(uint32_t)SpvDecorationArrayStride,16u);
    op(w,SpvOpDecorate,3,c.region_array,(uint32_t)SpvDecorationArrayStride,16u);op(w,SpvOpDecorate,3,c.const_array,(uint32_t)SpvDecorationArrayStride,16u);
    op(w,SpvOpDecorate,3,c.ubo,(uint32_t)SpvDecorationDescriptorSet,0u);op(w,SpvOpDecorate,3,c.ubo,(uint32_t)SpvDecorationBinding,opts.ubo_binding);
    if(state->combiner_dynamic)op(w,SpvOpDecorate,3,c.comb_array,(uint32_t)SpvDecorationArrayStride,16u);
    if(c.border_array)op(w,SpvOpDecorate,3,c.border_array,(uint32_t)SpvDecorationArrayStride,16u);
    if(c.key_array)op(w,SpvOpDecorate,3,c.key_array,(uint32_t)SpvDecorationArrayStride,16u);
    if(c.bump_array)op(w,SpvOpDecorate,3,c.bump_array,(uint32_t)SpvDecorationArrayStride,16u);
    for(unsigned j=0;j<w->annotations;j++)op(w,SpvOpDecorate,2,w->precise[j],(uint32_t)SpvDecorationNoContraction);
    op(w,SpvOpTypeVoid,1,c.void_t);op(w,SpvOpTypeFunction,2,c.fn,c.void_t);op(w,SpvOpTypeFloat,2,c.f,32u);op(w,SpvOpTypeInt,3,c.i,32u,1u);op(w,SpvOpTypeInt,3,c.u,32u,0u);op(w,SpvOpTypeBool,1,c.b);
    op(w,SpvOpTypeVector,3,c.v2,c.f,2u);op(w,SpvOpTypeVector,3,c.v3,c.f,3u);op(w,SpvOpTypeVector,3,c.v4,c.f,4u);op(w,SpvOpTypeVector,3,c.iv2,c.i,2u);op(w,SpvOpTypeVector,3,c.iv4,c.i,4u);
    for(unsigned j=0;j<32;j++){op(w,SpvOpConstant,3,c.i,c.si[j],j);op(w,SpvOpConstant,3,c.u,c.ui32[j],j);}
    if(c.ui_marker)op(w,SpvOpConstant,3,c.u,c.ui_marker,0x80000000u);
    const float constants[]={0,.5f,1,2,4,255,65535,16777216,0x1.fffffep127f};
    for(unsigned j=0;j<FLOATS;j++){uint32_t bits;memcpy(&bits,constants+j,4);op(w,SpvOpConstant,3,c.f,c.cf[j],bits);}
    op(w,SpvOpConstant,3,c.u,c.u127,127u);op(w,SpvOpConstant,3,c.u,c.u313,313u);op(w,SpvOpConstantTrue,2,c.b,c.yes);op(w,SpvOpConstantFalse,2,c.b,c.no);
    if(c.min_q){float q=1e-15f;uint32_t bits;memcpy(&bits,&q,4);op(w,SpvOpConstant,3,c.f,c.min_q,bits);}
    if(c.scale_array)op(w,SpvOpTypeArray,3,c.scale_array,c.f,c.ui32[4]);
    if(c.image_t){op(w,SpvOpTypeImage,8,c.image_t,c.f,(uint32_t)SpvDim2D,0u,0u,0u,1u,(uint32_t)SpvImageFormatUnknown);op(w,SpvOpTypeSampledImage,2,c.sampled_t,c.image_t);op(w,SpvOpTypePointer,3,c.sampler_ptr,(uint32_t)SpvStorageClassUniformConstant,c.sampled_t);}
    if(c.in_v2)op(w,SpvOpTypePointer,3,c.in_v2,(uint32_t)SpvStorageClassInput,c.v2);
    op(w,SpvOpTypeArray,3,c.region_array,c.iv4,c.ui32[8]);op(w,SpvOpTypeArray,3,c.const_array,c.v4,c.ui32[18]);
    if(state->combiner_dynamic)op(w,SpvOpTypeArray,3,c.comb_array,c.u,c.ui32[8]);
    if(c.border_array)op(w,SpvOpTypeArray,3,c.border_array,c.v3,c.ui32[4]);
    if(c.key_array)op(w,SpvOpTypeArray,3,c.key_array,c.u,c.ui32[4]);
    if(c.bump_array)op(w,SpvOpTypeArray,3,c.bump_array,c.v2,c.ui32[8]);
    word(w,((2u+uniform_count(state))<<16)|SpvOpTypeStruct);word(w,c.ubo_type);
    uint32_t fields[]={c.v4,c.region_array,c.const_array,c.f,c.f,c.v4,c.iv2,c.u,c.i,c.i,c.i,c.scale_array,c.u,c.u,c.u,c.u,c.comb_array,c.comb_array,c.comb_array,c.comb_array,c.border_array,c.border_array,c.key_array,c.key_array,c.bump_array,c.scale_array,c.scale_array};for(unsigned j=0;j<UNIFORMS;j++)if(uniform_enabled(state,j))word(w,fields[j]);
    uint32_t pointers[][3]={{c.in_f,SpvStorageClassInput,c.f},{c.in_v4,SpvStorageClassInput,c.v4},{c.in_b,SpvStorageClassInput,c.b},{c.out_f,SpvStorageClassOutput,c.f},{c.out_v4,SpvStorageClassOutput,c.v4},{c.uf,SpvStorageClassUniform,c.f},{c.uv4,SpvStorageClassUniform,c.v4},{c.uiv2,SpvStorageClassUniform,c.iv2},{c.uiv4,SpvStorageClassUniform,c.iv4},{c.ui,SpvStorageClassUniform,c.i},{c.uu,SpvStorageClassUniform,c.u},{c.ubo_ptr,SpvStorageClassUniform,c.ubo_type}};
    for(unsigned j=0;j<sizeof(pointers)/sizeof(pointers[0]);j++)op(w,SpvOpTypePointer,3,pointers[j][0],pointers[j][1],pointers[j][2]);
    if(state->combiner_dynamic)dynamic_types(&c);
    texture_types(&c);dot_types(&c);
    for(unsigned j=0;j<9;j++)op(w,SpvOpVariable,3,j==4||j==8?c.in_f:c.in_v4,c.inputs[j],(uint32_t)SpvStorageClassInput);
    op(w,SpvOpVariable,3,c.in_v4,c.frag_coord,(uint32_t)SpvStorageClassInput);op(w,SpvOpVariable,3,c.in_b,c.front,(uint32_t)SpvStorageClassInput);
    op(w,SpvOpVariable,3,c.out_v4,c.color,(uint32_t)SpvStorageClassOutput);op(w,SpvOpVariable,3,c.out_f,c.depth,(uint32_t)SpvStorageClassOutput);op(w,SpvOpVariable,3,c.ubo_ptr,c.ubo,(uint32_t)SpvStorageClassUniform);
    for(unsigned j=0;j<4;j++){
        if(c.tex_inputs[j])op(w,SpvOpVariable,3,c.in_v4,c.tex_inputs[j],(uint32_t)SpvStorageClassInput);
        if(c.samplers[j])op(w,SpvOpVariable,3,texture_sampler_pointer(&c,sampler_kind(state,j)),c.samplers[j],(uint32_t)SpvStorageClassUniformConstant);
    }
    if(c.point_coord)op(w,SpvOpVariable,3,c.in_v2,c.point_coord,(uint32_t)SpvStorageClassInput);
    if(state->combiner_dynamic)dynamic_helpers(&c);
    texture_helpers(&c);dot_helpers(&c);
    op(w,SpvOpFunction,4,c.void_t,c.entry,0u,c.fn);op(w,SpvOpLabel,1,id(w));
    c.coord=unary(&c,SpvOpLoad,c.v4,c.frag_coord);c.clip=unif(&c,CLIP,0);for(unsigned j=0;j<3;j++)c.positions[j]=unary(&c,SpvOpLoad,c.v4,c.inputs[j+5]);
    window_clip(&c);barycentric(&c);uint32_t color=combiners(&c);alpha_test(&c,color);op(w,SpvOpStore,2,c.color,color);
    uint32_t depth;
    switch(state->depth_format&DEPTH_FORMAT_MASK){
    case DEPTH_FORMAT_D16:depth=divf(&c,ext1(&c,GLSLstd450Floor,c.f,c.z),c.cf[F_D16_MAX]);break;
    case DEPTH_FORMAT_D24:{uint32_t z=divf(&c,ext1(&c,GLSLstd450Floor,c.f,c.z),c.cf[F_D24_SCALE]);depth=unary(&c,SpvOpBitcast,c.f,binary(&c,SpvOpIAdd,c.u,unary(&c,SpvOpBitcast,c.u,z),c.ui32[1]));break;}
    default:depth=divf(&c,c.z,extract(&c,c.clip,1));break;
    }
    op(w,SpvOpStore,2,c.depth,depth);op(w,SpvOpReturn,0);op(w,SpvOpFunctionEnd,0);if(w->words)w->words[3]=w->next;
}
static GlslcLayout layout(const PshState *state)
{
    unsigned count=uniform_count(state);size_t bytes=count*sizeof(GlslcUniform);for(unsigned j=0;j<UNIFORMS;j++)if(uniform_enabled(state,j))bytes+=strlen(uniforms[j].name)+1;
    GlslcUniform *m=malloc(bytes);if(!m)abort();char *text=(char *)(m+count);
    for(unsigned j=0,member=0;j<UNIFORMS;j++)if(uniform_enabled(state,j)){size_t n=strlen(uniforms[j].name)+1;memcpy(text,uniforms[j].name,n);m[member++]=(GlslcUniform){.name=text,.dim_v=uniforms[j].width,.dim_a=uniforms[j].count,.stride=uniforms[j].count>1?16:uniforms[j].width*4,.offset=uniform_offset(state,j)};text+=n;}
    return (GlslcLayout){.members=m,.count=count,.size=uniform_size(state)};
}
bool pgraph_spv_psh(const PshState *state,SpvPshOptions opts,GlslcResult *out)
{
    memset(out,0,sizeof(*out));if(!pgraph_spv_psh_supported(state,opts))return false;
    uint32_t precise[128];Writer count={.next=1,.precise=precise};emit(&count,state,opts);
    size_t size=count.count+count.recorded*3;uint32_t *words=malloc(size*4);if(!words)abort();
    Writer output={.words=words,.capacity=size,.next=1,.precise=precise,.annotations=count.recorded};emit(&output,state,opts);
    if(output.count!=size || output.next!=count.next || output.recorded!=count.recorded)abort();
    out->words=words;out->num_words=out->allocation_words=size;out->uniforms=layout(state);return true;
}
