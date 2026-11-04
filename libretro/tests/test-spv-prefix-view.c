/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static size_t moves, moved_bytes;
static void *counted_move(void *dst, const void *src, size_t bytes)
{ ++moves; moved_bytes += bytes; return memmove(dst,src,bytes); }
#define memmove counted_move
#include "hw/xbox/nv2a/pgraph/spv/glslc.c"
#undef memmove
static void release(GlslcResult *r)
{
    glslc_result_free_words(r);
    assert(!r->words && !r->allocation && !r->num_words && !r->allocation_words);
    free(r->uniforms.members);free(r->push_constants.members);
}
static void layout_equal(const GlslcLayout *a,const GlslcLayout *b)
{
    assert(a->count==b->count && a->size==b->size);
    for(size_t i=0;i<a->count;i++) {
        assert(!strcmp(a->members[i].name,b->members[i].name));
        assert(a->members[i].dim_v==b->members[i].dim_v);
        assert(a->members[i].dim_a==b->members[i].dim_a);
        assert(a->members[i].offset==b->members[i].offset);
        assert(a->members[i].stride==b->members[i].stride);
    }
}
#ifdef BASELINE_COMPILER
bool baseline_glslc_compile(GlslcStage, const char *, const GlslcOptions *, GlslcResult *);
#endif
int main(int argc, char **argv)
{
    const char *sources[]={
        "#version 450\nvoid main(){gl_Position=vec4(1.0);}",
        "#version 450\nlayout(location=0) out vec4 color; float f(float x){return x*x;} void main(){color=vec4(f(2.0));}",
        "#version 450\nlayout(local_size_x=1) in; layout(set=0,binding=0) buffer Data {uint x[];} d; void main(){d.x[gl_GlobalInvocationID.x]=3u;}",
        "#version 450\nlayout(points) in; layout(points,max_vertices=1) out; void main(){gl_Position=gl_in[0].gl_Position;EmitVertex();EndPrimitive();}",
        "#version 450\nlayout(set=0,binding=0) uniform Params {vec4 color;} u; layout(push_constant) uniform Push {float scale;} p; layout(location=0) out vec4 color; void main(){color=u.color*p.scale;}"
    };
    const GlslcStage stages[]={GLSLC_STAGE_VERTEX,GLSLC_STAGE_FRAGMENT,GLSLC_STAGE_COMPUTE,GLSLC_STAGE_GEOMETRY,GLSLC_STAGE_FRAGMENT};
    char large[32768];strcpy(large,"#version 450\nlayout(location=0) out vec4 color; void main(){float x=0.0;");
    for(unsigned i=0;i<350;i++) strcat(large,"x=x*0.97+0.125;");
    strcat(large,"color=vec4(x);}");
    unsigned cases=0,views=0;size_t bytes_avoided=0;
    for(unsigned source=0;source<6;source++) for(unsigned version=0;version<2;version++)
    for(unsigned layouts=0;layouts<2;layouts++) {
        const char *text=source<5?sources[source]:large;
        GlslcStage stage=source<5?stages[source]:GLSLC_STAGE_FRAGMENT;
        GlslcOptions opts={.spirv_1_6=version,.emit_layout=layouts};
        GlslcResult reference;
        moves=moved_bytes=0;
        if(!glslc_compile(stage,text,&opts,&reference)){fprintf(stderr,"%s\n",reference.error);abort();}
        assert(!reference.allocation && moves==1);
        size_t baseline_move=moved_bytes;
#ifdef BASELINE_COMPILER
        GlslcResult old;
        assert(baseline_glslc_compile(stage,text,&opts,&old));
        assert(old.num_words==reference.num_words);
        assert(!memcmp(old.words,reference.words,old.num_words*sizeof(uint32_t)));
        layout_equal(&old.uniforms,&reference.uniforms);layout_equal(&old.push_constants,&reference.push_constants);
        release(&old);
#endif
        const size_t prefixes[]={1,256,8192};
        for(unsigned i=0;i<3;i++) {
            GlslcResult candidate;opts.module_prefix_words=prefixes[i];moves=moved_bytes=0;
            if(!glslc_compile(stage,text,&opts,&candidate)){fprintf(stderr,"%s\n",candidate.error);abort();}
            assert(candidate.num_words==reference.num_words);
            assert(!memcmp(candidate.words,reference.words,reference.num_words*sizeof(uint32_t)));
            if(argc>1 && i==1) {
                char path[4096];snprintf(path,sizeof(path),"%s/module-%u-%u-%u.spv",argv[1],source,version,layouts);
                FILE *file=fopen(path,"wb");assert(file);
                assert(fwrite(candidate.words,sizeof(uint32_t),candidate.num_words,file)==candidate.num_words);
                assert(!fclose(file));
            }
            layout_equal(&candidate.uniforms,&reference.uniforms);
            layout_equal(&candidate.push_constants,&reference.push_constants);
            assert(candidate.allocation_words>=candidate.num_words);
            if(candidate.allocation) {
                assert(!moves && !moved_bytes);
                assert((uint32_t*)candidate.allocation<=candidate.words);
                assert(candidate.words+candidate.num_words<=(uint32_t*)candidate.allocation+candidate.allocation_words);
                ++views;bytes_avoided+=baseline_move;
            } else {assert(moves==1 && moved_bytes==baseline_move);}
            if(i==0)assert(!candidate.allocation);
            if(i==2)assert(candidate.allocation);
            release(&candidate);++cases;
        }
        release(&reference);
    }
    for(unsigned i=0;i<32;i++) {
        GlslcOptions opts={.module_prefix_words=256};GlslcResult bad;
        assert(!glslc_compile(GLSLC_STAGE_FRAGMENT,"#version 450\nvoid main(){unknown_function();}",&opts,&bad));
        assert(!bad.words && !bad.allocation);release(&bad);
    }
    printf("SPIR-V prefix views: %u byte-identical cases, %u views, %zu function bytes not moved; error cleanup PASS\n",cases,views,bytes_avoided);
    return 0;
}
