/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Numeric integer colors reuse the unsigned view; no depth decoding or copies. */
static uint32_t texture_raw_color(Context *c,uint32_t coord,uint32_t uv,uint32_t sample,unsigned unit)
{
    if(convolution_stage(c->state,unit))return texture_convolution(c,sample,uv,extract(c,coord,3),3);
    uint32_t color;
    if(texture_mode(c->state,unit)==PS_TEXTUREMODES_PROJECT3D){
        uint32_t projected=coord;
        if(c->state->rect_tex[unit])projected=v4(c,extract(c,uv,0),extract(c,uv,1),c->cf[F_ZERO],extract(c,coord,3));
        color=val(c->w,SpvOpImageSampleProjImplicitLod,c->uvec4_t,2,sample,projected);
    }else{
        uint32_t q=ext2(c,GLSLstd450FMax,c->f,extract(c,coord,3),c->min_q);
        uv=binary(c,SpvOpFDiv,c->v2,uv,splat(c,c->v2,q,2));
        color=val(c->w,SpvOpImageSampleImplicitLod,c->uvec4_t,2,sample,uv);
    }
    return unary(c,SpvOpConvertUToF,c->v4,color);
}
