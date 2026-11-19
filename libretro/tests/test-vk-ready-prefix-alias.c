/* SPDX-License-Identifier: GPL-2.0-or-later */
/* All logical VS prefixes retain identical Vulkan instructions and layouts. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spirv/unified1/spirv.h>
#include <vulkan/vulkan.h>
#include "hw/xbox/nv2a/pgraph/glsl/vsh.h"
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"
#include "hw/xbox/nv2a/pgraph/gl/ready-ff-state.h"
/* ACTUAL BANK CONSTANTS */
typedef struct {struct {VkShaderStageFlagBits kind;unsigned native_kind;struct {unsigned ready_kind,ready_version;bool ready_float32;GenVshGlslOptions glsl_opts;VshState state;}vsh;}key;} Recipe;
static bool pgraph_vk_spirv_1_6;
static void recipe(unsigned i,Recipe *entry){memset(entry,0,sizeof(*entry));/* ACTUAL BANK RECIPE */}
static bool compile_native_source(const Recipe *entry,const char *glsl,GlslcResult *result){bool preserve_float32=entry->key.vsh.ready_float32;/* ACTUAL COMPILER OPTIONS */return glslc_compile(GLSLC_STAGE_VERTEX,glsl,&opts,result);}
static bool debug(unsigned op){switch(op){case SpvOpName:case SpvOpMemberName:case SpvOpSource:case SpvOpSourceExtension:case SpvOpSourceContinued:case SpvOpString:case SpvOpLine:case SpvOpNoLine:case SpvOpModuleProcessed:return true;default:return false;}}
static size_t skip(const GlslcResult *r,size_t at){while(at<r->num_words){unsigned n=r->words[at]>>16;assert(n&&at+n<=r->num_words);if(!debug(r->words[at]&65535))break;at+=n;}return at;}
static void words_equal(const GlslcResult *a,const GlslcResult *b){assert(!memcmp(a->words,b->words,20));size_t x=5,y=5;for(;;){x=skip(a,x);y=skip(b,y);if(x==a->num_words||y==b->num_words){assert(x==a->num_words&&y==b->num_words);break;}unsigned n=a->words[x]>>16;assert(n==(b->words[y]>>16)&&!memcmp(a->words+x,b->words+y,n*4));x+=n;y+=n;}}
static void layout_equal(const GlslcLayout *a,const GlslcLayout *b){assert(a->count==b->count&&a->size==b->size);for(size_t i=0;i<a->count;i++){const GlslcUniform *x=&a->members[i],*y=&b->members[i];assert(!strcmp(x->name,y->name)&&x->dim_v==y->dim_v&&x->dim_a==y->dim_a&&x->stride==y->stride&&x->offset==y->offset);}}
static void dispose(GlslcResult *r){glslc_result_free_words(r);free(r->uniforms.members);free(r->push_constants.members);}
static void write_file(const char *folder,const char *suffix,unsigned env,unsigned i,const void *data,size_t bytes){char file[1024];int n=snprintf(file,sizeof(file),"%s/env%u-index%03u.%s",folder,env,i,suffix);assert(n>0&&(size_t)n<sizeof(file));FILE *f=fopen(file,"wb");assert(f&&fwrite(data,1,bytes,f)==bytes&&!fclose(f));}
static FILE *artifact(const char *folder,const char *suffix,unsigned env,unsigned i){char file[1024];int n=snprintf(file,sizeof(file),"%s/env%u-index%03u.%s",folder,env,i,suffix);assert(n>0&&(size_t)n<sizeof(file));FILE *f=fopen(file,"wb");assert(f);return f;}
static void projection(const char *folder,unsigned env,unsigned i,const GlslcResult *r){FILE *f=artifact(folder,"ndspv",env,i);assert(fwrite(r->words,4,5,f)==5);for(size_t at=5;at<r->num_words;){unsigned n=r->words[at]>>16;assert(n&&at+n<=r->num_words);if(!debug(r->words[at]&65535))assert(fwrite(r->words+at,4,n,f)==n);at+=n;}assert(!fclose(f));f=artifact(folder,"layouts",env,i);const GlslcLayout *ls[]={&r->uniforms,&r->push_constants};for(unsigned k=0;k<2;k++){const GlslcLayout *l=ls[k];fprintf(f,"%u %zu %zu\n",k,l->count,l->size);for(size_t j=0;j<l->count;j++){const GlslcUniform *u=&l->members[j];fprintf(f,"%s\t%zu %zu %zu %zu\n",u->name,u->dim_v,u->dim_a,u->stride,u->offset);}}assert(!fclose(f));}
int main(int argc,char **argv){assert(argc==2||argc==3);unsigned mutation=argc==3?strtoul(argv[2],NULL,10):0,pairs=0;assert(VK_READY_VERTEX_STAGES==120);
 for(unsigned env=0;env<2;env++){pgraph_vk_spirv_1_6=env;for(unsigned i=0;i<120;i+=2){Recipe a,b;recipe(i,&a);recipe(i+1,&b);assert(!a.key.vsh.glsl_opts.prefix_outputs&&b.key.vsh.glsl_opts.prefix_outputs);assert(!memcmp(&a.key.vsh.state,&b.key.vsh.state,sizeof(VshState)));assert(a.key.vsh.ready_float32==b.key.vsh.ready_float32);assert(a.key.native_kind==1&&b.key.native_kind==1);
  MString *sa=i<8?pgraph_glsl_gen_vsh_interpreter_dynamic(&a.key.vsh.state,a.key.vsh.glsl_opts):pgraph_glsl_gen_vsh(&a.key.vsh.state,a.key.vsh.glsl_opts);
  MString *sb=i<8?pgraph_glsl_gen_vsh_interpreter_dynamic(&b.key.vsh.state,b.key.vsh.glsl_opts):pgraph_glsl_gen_vsh(&b.key.vsh.state,b.key.vsh.glsl_opts);assert(sa&&sb);
  if(mutation==3&&i==0)b.key.vsh.ready_float32=!b.key.vsh.ready_float32;
  GlslcResult x={0},y={0};assert(compile_native_source(&a,mstring_get_str(sa),&x));assert(compile_native_source(&b,mstring_get_str(sb),&y));
  if(mutation==1&&i==0)for(size_t at=5;at<y.num_words;at+=y.words[at]>>16)if((y.words[at]&65535)==SpvOpDecorate&&(y.words[at]>>16)==4&&y.words[at+2]==SpvDecorationLocation){y.words[at+3]^=1;break;}
  if(mutation==2&&i==0){assert(y.uniforms.count);y.uniforms.members[0].offset+=4;}
  words_equal(&x,&y);layout_equal(&x.uniforms,&y.uniforms);layout_equal(&x.push_constants,&y.push_constants);
  write_file(argv[1],"glsl",env,i,mstring_get_str(sa),strlen(mstring_get_str(sa)));write_file(argv[1],"glsl",env,i+1,mstring_get_str(sb),strlen(mstring_get_str(sb)));write_file(argv[1],"spv",env,i,x.words,x.num_words*4);write_file(argv[1],"spv",env,i+1,y.words,y.num_words*4);
  projection(argv[1],env,i,&x);projection(argv[1],env,i+1,&y);
  printf("PAIR env=%u even=%u odd=%u kind=%u preserve=%u raw_identical=%u words=%zu/%zu uniforms=%zu/%zu\n",env,i,i+1,a.key.vsh.ready_kind,a.key.vsh.ready_float32,(unsigned)(x.num_words==y.num_words&&!memcmp(x.words,y.words,x.num_words*4)),x.num_words,y.num_words,x.uniforms.size,y.uniforms.size);fflush(stdout);mstring_unref(sa);mstring_unref(sb);dispose(&x);dispose(&y);pairs++;
 }}printf("PASS %u prefix pairs (all120 logical recipes, both SPIR-V5/6): exact non-debug words, IDs, layouts and FP targets\n",pairs);return 0;
}
