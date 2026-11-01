/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Execute the immutable shader's integer/float SSA against an independent
 * Morton oracle without a GPU. This is a deliberately bounded interpreter,
 * not a replacement shader compiler; unknown operations fail the test. */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spirv/unified1/spirv.h>
#include "hw/xbox/nv2a/pgraph/spv/depth.h"
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); _Exit(1); } } while (0)
#include "spv-test-layout.h"
enum { MAX_ID = 512, DEPTH=1, STENCIL, OUTPUT, PUSH, GLOBAL, GROUPS };
typedef struct { unsigned kind,index; } Pointer;
typedef struct { unsigned writes,address,value,reads; } Result;
static float as_float(uint32_t u) { float f; memcpy(&f,&u,4); return f; }
static uint32_t as_word(float f) { uint32_t u; memcpy(&u,&f,4); return u; }
static uint32_t depth_word(unsigned i,unsigned seed,bool floating) {
    return floating ? as_word((float)((i*37u+seed)%100000)/100000.0f) : (i*37u+seed)&0xffffff;
}
static unsigned morton_source(unsigned at,unsigned width,unsigned height) {
    unsigned x=0,y=0,bit=0;
    for (unsigned n=1;n<width||n<height;n*=2) {
        if(n<width) x|=((at>>bit++)&1)*n;
        if(n<height)y|=((at>>bit++)&1)*n;
    }
    return y*width+x;
}
static Result execute(SpvStaticModule module,const uint32_t push[5],unsigned at,
                      unsigned nx,unsigned group,unsigned seed,bool floating) {
    uint32_t value[MAX_ID]={0},vector[MAX_ID][3]={{0}};
    Pointer pointer[MAX_ID]={{0}};
    unsigned binding[MAX_ID]={0},builtin[MAX_ID]={0},main=0;
    bool running=false;
    Result out={0};
    assert(module.words[3]<MAX_ID);
    for (size_t pc=5;pc<module.num_words;) {
        const uint32_t *p=module.words+pc;
        unsigned op=p[0]&65535,n=p[0]>>16;
        assert(n&&pc+n<=module.num_words);
        pc+=n;
        if(!running) {
            switch(op) {
            case SpvOpEntryPoint: main=p[2]; break;
            case SpvOpDecorate:
                if(p[2]==SpvDecorationBinding)binding[p[1]]=p[3]+1;
                if(p[2]==SpvDecorationBuiltIn)builtin[p[1]]=p[3]+1;
                break;
            case SpvOpConstant: value[p[2]]=p[3]; break;
            case SpvOpVariable:
                if(p[3]==SpvStorageClassUniform)pointer[p[2]].kind=binding[p[2]];
                if(p[3]==SpvStorageClassPushConstant)pointer[p[2]].kind=PUSH;
                if(p[3]==SpvStorageClassInput)pointer[p[2]].kind=
                    builtin[p[2]]==SpvBuiltInGlobalInvocationId+1?GLOBAL:GROUPS;
                break;
            case SpvOpFunction: running=p[2]==main; break;
            default: break;
            }
            continue;
        }
        unsigned a=n>3?p[3]:0,b=n>4?p[4]:0;
        switch(op) {
        case SpvOpLabel: case SpvOpSelectionMerge: break;
        case SpvOpAccessChain:
            pointer[p[2]]=pointer[a];pointer[p[2]].index=value[p[n-1]];break;
        case SpvOpLoad: {
            Pointer ptr=pointer[a];
            if(ptr.kind==GLOBAL) {vector[p[2]][0]=at%(nx*group);vector[p[2]][1]=at/(nx*group);break;}
            if(ptr.kind==GROUPS) {vector[p[2]][0]=nx;break;}
            if(ptr.kind==PUSH) {assert(ptr.index<5);value[p[2]]=push[ptr.index];break;}
            unsigned scale=push[0]/push[1],pixels=push[3]*scale*scale;
            if(ptr.kind==DEPTH) {assert(ptr.index<pixels);value[p[2]]=depth_word(ptr.index,seed,floating);}
            else {
                assert(ptr.kind==STENCIL&&ptr.index>=push[2]);
                unsigned first=(ptr.index-push[2])*4;
                assert(first<pixels);
                value[p[2]]=0;
                for(unsigned j=0;j<4;j++) value[p[2]]|=((first+j)*13u+seed)%256<<(j*8);
            }
            out.reads++;break;
        }
        case SpvOpCompositeExtract: value[p[2]]=vector[a][b];break;
        case SpvOpIAdd: value[p[2]]=value[a]+value[b];break;
        case SpvOpISub: value[p[2]]=value[a]-value[b];break;
        case SpvOpIMul: value[p[2]]=value[a]*value[b];break;
        case SpvOpUDiv: assert(value[b]);value[p[2]]=value[a]/value[b];break;
        case SpvOpUMod: assert(value[b]);value[p[2]]=value[a]%value[b];break;
        case SpvOpBitwiseAnd: value[p[2]]=value[a]&value[b];break;
        case SpvOpBitwiseOr: value[p[2]]=value[a]|value[b];break;
        case SpvOpNot: value[p[2]]=~value[a];break;
        case SpvOpShiftLeftLogical: assert(value[b]<32);value[p[2]]=value[a]<<value[b];break;
        case SpvOpShiftRightLogical: assert(value[b]<32);value[p[2]]=value[a]>>value[b];break;
        case SpvOpULessThan: value[p[2]]=value[a]<value[b];break;
        case SpvOpSelect: value[p[2]]=value[a]?value[b]:value[p[5]];break;
        case SpvOpFMul: value[p[2]]=as_word(as_float(value[a])*as_float(value[b]));break;
        case SpvOpConvertFToS: value[p[2]]=(uint32_t)(int32_t)as_float(value[a]);break;
        case SpvOpBitcast: value[p[2]]=value[a];break;
        case SpvOpBranchConditional:
        case SpvOpBranch: {
            unsigned label=op==SpvOpBranch?p[1]:(value[p[1]]?p[2]:p[3]);
            size_t target=5;
            while(target<module.num_words&& !((module.words[target]&65535)==SpvOpLabel&&module.words[target+1]==label))target+=module.words[target]>>16;
            assert(target<module.num_words);pc=target;break;
        }
        case SpvOpStore:
            assert(pointer[p[1]].kind==OUTPUT);out.writes++;
            out.address=pointer[p[1]].index;out.value=value[p[2]];break;
        case SpvOpReturn: return out;
        default: fprintf(stderr,"Unhandled SPIR-V opcode %u\n",op);_Exit(1);
        }
    }
    assert(false);return out;
}
int main(int argc,char **argv) {
    assert(argc==2);unsigned cases=0,seed=1;
    for(unsigned group=128;group<=256;group*=2)for(unsigned f=0;f<2;f++) {
        SpvStaticModule module=pgraph_spv_depth_swizzled(f,group);
        assert(module.words&&module.num_words);
        spv_test_layout(module.words,module.num_words,3,5,group);
        char path[2048];snprintf(path,sizeof(path),"%s/fused-%u-%u.spv",argv[1],group,f);
        FILE *file=fopen(path,"wb");assert(file&&fwrite(module.words,4,module.num_words,file)==module.num_words);assert(!fclose(file));
        for(unsigned i=0;i<12000;i++) {
            seed=seed*1664525u+1013904223u;
            unsigned w=1u<<(seed%13),h=1u<<((seed>>5)%13),scale=1+(seed>>10)%3;
            unsigned count=w*h,at=i%7==0?count+(i%11):(seed>>1)%count;
            unsigned base=((uint64_t)w*h*scale*scale*4+255)/256*64;
            uint32_t push[5]={w*scale,w,base,count,(seed>>25)*4};
            Result result=execute(module,push,at,1+i%3,group,seed,f);
            assert(result.writes==(at<count));
            if(at<count) {
                unsigned src=morton_source(at,w,h);
                src=((src/w)*scale)*(w*scale)+(src%w)*scale;
                uint32_t z=depth_word(src,seed,f);
                if(f)z=(uint32_t)(int32_t)(as_float(z)*16777215.0f);
                uint32_t expected=(z<<8)|((src*13u+seed)%256);
                assert(result.address==push[4]+at&&result.value==expected&&result.reads==2);
            } else assert(!result.reads);
            SpvStaticModule again=pgraph_spv_depth_swizzled(f,group);
            assert(again.words==module.words&&again.num_words==module.num_words);
            cases++;
        }
    }
    assert(!pgraph_spv_depth_swizzled(false,64).words);
    printf("Fused depth SPIR-V: %u SSA executions match independent Morton/depth/stencil oracle; 128/256 groups, scales 1/2/3, square/rectangular/1D, offsets and tail guards passed\n",cases);
}
