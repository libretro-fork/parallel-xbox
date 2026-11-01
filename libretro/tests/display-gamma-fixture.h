/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define GAMMA_WIDTH 256
#define GAMMA_HEIGHT 8
static unsigned gamma_curve(unsigned curve,unsigned channel,unsigned value)
{
    switch(curve) {
    case 0:return value;
    case 1:return 255-value;
    case 2:return (value*(channel*2+1)+channel*53)&255;
    case 3:return value<128?7+channel*13:241-channel*17;
    default:return (value*value+channel*71)&255;
    }
}
static void gamma_table(uint32_t table[256],unsigned curve)
{
    for(unsigned i=0;i<256;i++)table[i]=gamma_curve(curve,0,i)|
        (gamma_curve(curve,1,i)<<8)|(gamma_curve(curve,2,i)<<16);
}
static uint32_t source_word(unsigned format,unsigned x,unsigned y)
{
    unsigned r=x&31,g=(x*7+y*3)&(format==1?63:31),b=(x*3+y)&31;
    if(!x)r=g=b=0;
    return format==1?(r<<11)|(g<<5)|b:(((x+y)&1)<<15)|(r<<10)|(g<<5)|b;
}
static void source_pixels(unsigned format,uint8_t *bytes)
{
    for(unsigned y=0;y<GAMMA_HEIGHT;y++)for(unsigned x=0;x<GAMMA_WIDTH;x++) {
        unsigned index=y*GAMMA_WIDTH+x;
        if(format==0) {
            bytes[index*4]=x;bytes[index*4+1]=(x*7+y*13)&255;
            bytes[index*4+2]=255-x;bytes[index*4+3]=(x+y*53)&255;
            if(!x)memset(bytes+index*4,0,3);
        } else {
            uint16_t word=source_word(format,x,y);memcpy(bytes+index*2,&word,2);
        }
    }
}
static void source_color(unsigned format,unsigned x,unsigned y,float color[4])
{
    if(format==0) {
        color[0]=x/255.f;color[1]=((x*7+y*13)&255)/255.f;
        color[2]=(255-x)/255.f;color[3]=((x+y*53)&255)/255.f;
        if(!x)color[0]=color[1]=color[2]=0;
    } else {
        uint32_t word=source_word(format,x,y);
        color[0]=((word>>(format==1?11:10))&31)/31.f;
        color[1]=((word>>5)&(format==1?63:31))/(format==1?63.f:31.f);
        color[2]=(word&31)/31.f;color[3]=format==1?1.f:(float)(word>>15);
    }
}
static void gamma_check(const uint8_t *pixels,unsigned format,unsigned curve,
                        unsigned overlay,bool enabled,bool flip,const uint8_t *baseline)
{
    for(unsigned y=0;y<GAMMA_HEIGHT;y++)for(unsigned x=0;x<GAMMA_WIDTH;x++) {
        float color[4];source_color(format,x,flip?GAMMA_HEIGHT-1-y:y,color);
        if(baseline) {
            for(unsigned c=0;c<4;c++)color[c]=baseline[(y*GAMMA_WIDTH+x)*4+c]/255.f;
        } else if(overlay==1||(overlay==2&&color[0]==0&&color[1]==0&&color[2]==0)) {
            color[0]=20/255.f;color[1]=100/255.f;color[2]=210/255.f;color[3]=77/255.f;
        }
        for(unsigned c=0;c<4;c++) {
            unsigned expected=(unsigned)(color[c]*255.f+0.5f);
            if(enabled&&c<3)expected=gamma_curve(curve,c,expected);
            unsigned actual=pixels[(y*GAMMA_WIDTH+x)*4+c];
            if(actual!=expected) {
                fprintf(stderr,"gamma fmt=%u curve=%u overlay=%u enabled=%u pixel=%u,%u channel=%u: %u != %u\n",
                        format,curve,overlay,enabled,x,y,c,actual,expected);abort();
            }
        }
    }
}
