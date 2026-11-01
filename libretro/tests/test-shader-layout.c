/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"
#ifdef TEST_SPIRV_REFLECT
#include <spirv_reflect.h>
#endif
/* TYPES */
typedef struct { GlslcLayout uniforms, push_constants; } ShaderLayouts;
typedef struct { ShaderUniformLayout uniforms, push_constants; } ShaderModuleInfo;
static unsigned allocations;
static void *module_alloc(size_t n) { allocations++; return calloc(1,n); }
#define g_malloc0_n(n,s) calloc(n,s)
#define g_malloc0(n) module_alloc(n)
#define g_free free
/* PRODUCTION */

static unsigned cases;
static void compare(const GlslcLayout *layout, const GlslcUniform *expected,
                    size_t count, size_t size)
{
    assert(layout->count == count && layout->size == size);
    for (size_t i = 0; i < count; i++) {
        const GlslcUniform *a = &layout->members[i], *b = &expected[i];
        assert(!strcmp(a->name, b->name) && a->offset == b->offset);
        assert(a->dim_v == b->dim_v && a->dim_a == b->dim_a && a->stride == b->stride);
    }
}

#ifdef TEST_SPIRV_REFLECT
static void oracle_block(const GlslcLayout *layout, const SpvReflectBlockVariable *block)
{
    assert(layout->count == block->member_count && layout->size == block->size);
    for (size_t i = 0; i < layout->count; i++) {
        const GlslcUniform *a = &layout->members[i];
        const SpvReflectBlockVariable *b = &block->members[i];
        size_t count = 1, stride = b->array.stride;
        if (b->numeric.matrix.stride > stride) stride = b->numeric.matrix.stride;
        for (unsigned j = 0; j < b->array.dims_count; j++) count *= b->array.dims[j];
        if (b->numeric.matrix.column_count) {
            count *= b->numeric.matrix.column_count;
            if (b->array.stride) stride = b->array.stride / b->numeric.matrix.column_count;
        }
        size_t width = b->numeric.vector.component_count ? b->numeric.vector.component_count : 1;
        assert(!strcmp(a->name, b->name) && a->offset == b->offset);
        assert(a->dim_v == width && a->dim_a == count && a->stride == stride);
    }
}
static void oracle(const GlslcResult *code)
{
    SpvReflectShaderModule module;
    assert(spvReflectCreateShaderModule(code->num_words * 4, code->words, &module) == SPV_REFLECT_RESULT_SUCCESS);
    unsigned uniform_count = 0;
    for (unsigned i = 0; i < module.descriptor_binding_count; i++) {
        if (module.descriptor_bindings[i].descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            oracle_block(&code->uniforms, &module.descriptor_bindings[i].block);
            uniform_count++;
        }
    }
    assert(uniform_count == (code->uniforms.count != 0));
    assert(module.push_constant_block_count == (code->push_constants.count != 0));
    if (module.push_constant_block_count) oracle_block(&code->push_constants, &module.push_constant_blocks[0]);
    spvReflectDestroyShaderModule(&module);
}
#endif

static void test(GlslcStage stage, const char *text, bool version,
                 const GlslcUniform *uniforms, size_t count, size_t size,
                 const GlslcUniform *push, size_t push_count, size_t push_size)
{
    GlslcOptions opts = { .spirv_1_6 = version, .emit_layout = true };
    GlslcResult code, bare;
    if (!glslc_compile(stage, text, &opts, &code)) {
        fprintf(stderr, "line %d: %s\n%s\n", code.error_line, code.error, text);
        abort();
    }
    if (count != SIZE_MAX && (code.uniforms.count != count || code.uniforms.size != size || code.push_constants.count != push_count || code.push_constants.size != push_size)) fprintf(stderr,"layout sizes: U=%zu/%zu expected %zu/%zu P=%zu/%zu expected %zu/%zu\n%s\n",code.uniforms.count,code.uniforms.size,count,size,code.push_constants.count,code.push_constants.size,push_count,push_size,text);
    if (count != SIZE_MAX) {
        compare(&code.uniforms, uniforms, count, size);
        compare(&code.push_constants, push, push_count, push_size);
    }
#ifdef TEST_SPIRV_REFLECT
    oracle(&code);
#endif
    opts.emit_layout = false;
    assert(glslc_compile(stage, text, &opts, &bare));
    assert(!bare.uniforms.members && !bare.push_constants.members);
    assert(bare.num_words == code.num_words && !memcmp(bare.words, code.words, code.num_words * 4));
    ShaderLayouts layouts = {code.uniforms, code.push_constants};
    unsigned before = allocations;
    ShaderModuleInfo *first = alloc_shader_module_info(&layouts);
    ShaderModuleInfo *second = alloc_shader_module_info(&layouts);
    assert(allocations == before + 2);
    free(code.words); free(bare.words);
    /* Names are shared; mutable upload buffers must
     * remain independent across modules sharing that source. */
    ShaderUniformLayout *a[] = { &first->uniforms, &first->push_constants };
    ShaderUniformLayout *b[] = { &second->uniforms, &second->push_constants };
    for (unsigned i = 0; i < 2; i++) {
        assert(a[i]->total_size == b[i]->total_size);
        for (size_t j = 0; j < a[i]->num_uniforms; j++) {
            assert(!strcmp(a[i]->uniforms[j].name, b[i]->uniforms[j].name));
            assert(a[i]->uniforms == b[i]->uniforms);
        }
        if (a[i]->total_size) {
            for (size_t j = 0; j < a[i]->total_size; j++) assert(!((unsigned char *)a[i]->allocation)[j]);
            memset(a[i]->allocation, 0xa5, a[i]->total_size);
            for (size_t j = 0; j < b[i]->total_size; j++) assert(!((unsigned char *)b[i]->allocation)[j]);
        }
        if (a[i]->allocation) assert((uintptr_t)a[i]->allocation % 16 == 0);
        if (b[i]->allocation) assert((uintptr_t)b[i]->allocation % 16 == 0);
    }
    free(first); free(second);
    free(code.uniforms.members); free(code.push_constants.members);
    cases++;
}

int main(int argc, char **argv)
{
    ShaderModuleInfo *plain = alloc_shader_module_info(NULL);
    assert(allocations == 1 && !plain->uniforms.allocation &&
           !plain->push_constants.allocation);
    free(plain);
    static const GlslcUniform uniforms[] = {
        {"color",4,1,0,0}, {"transform",4,4,16,16}, {"lights",3,3,16,80},
    };
    static const GlslcUniform push[] = {{"attr",4,16,16,0}};
    for (unsigned version = 0; version < 2; version++) {
        test(GLSLC_STAGE_VERTEX, "#version 450\nlayout(binding=0,std140) uniform U { vec4 color; mat4 transform; vec3 lights[3]; } u; layout(push_constant) uniform P { vec4 attr[16]; } p; void main(){gl_Position=u.transform*p.attr[0]+u.color;}", version, uniforms,3,128,push,1,256);
        test(GLSLC_STAGE_FRAGMENT, "#version 450\nlayout(location=0) out vec4 o;void main(){o=vec4(1);}",version,NULL,0,0,NULL,0,0);
        test(GLSLC_STAGE_GEOMETRY, "#version 450\nlayout(points) in;layout(points,max_vertices=1) out;void main(){gl_Position=gl_in[0].gl_Position;EmitVertex();EndPrimitive();}",version,NULL,0,0,NULL,0,0);
        test(GLSLC_STAGE_COMPUTE, "#version 450\nlayout(local_size_x=1) in;layout(binding=0,std430) buffer B{uint data[];}b;void main(){b.data[0]=1u;}",version,NULL,0,0,NULL,0,0);
        const GlslcUniform matrix_ubo = {"matrix",2,2,16,0};
        const GlslcUniform matrix_push = {"matrix",2,2,8,0};
        test(GLSLC_STAGE_VERTEX, "#version 450\nlayout(binding=0,std140) uniform U{mat2 matrix;}u;layout(push_constant) uniform P{mat2 matrix;}p;void main(){gl_Position=vec4(u.matrix*p.matrix*vec2(1),0,1);}",version,&matrix_ubo,1,32,&matrix_push,1,16);
        test(GLSLC_STAGE_VERTEX, "#version 450\nlayout(push_constant) uniform P{mat2 matrix;}p;layout(binding=0,std140) uniform U{mat2 matrix;}u;void main(){gl_Position=vec4(u.matrix*p.matrix*vec2(1),0,1);}",version,&matrix_ubo,1,32,&matrix_push,1,16);
        /* Scalar/vector/matrix arrays, including arrays of length one and
         * matrices whose std430 column stride differs from std140. */
        const char *types[] = {"float","int","uint","bool","vec2","vec3","vec4","ivec4","mat2","mat3","mat4","mat2x3","mat3x2"};
        for (unsigned storage = 0; storage < 2; storage++)
        for (unsigned standard = 0; standard < 2; standard++)
        for (unsigned type = 0; type < sizeof(types)/sizeof(types[0]); type++)
        for (unsigned array = 0; array < 3; array++) {
            if (type == 3 && array) continue; /* compiler excludes bool arrays */
            unsigned width = type < 4 ? 1 : type == 4 ? 2 : type == 5 ? 3 : type < 8 ? 4 : type == 8 ? 2 : type == 9 ? 3 : type == 10 ? 4 : type == 11 ? 3 : 2;
            unsigned columns = type < 8 ? 1 : type == 8 || type == 11 ? 2 : type == 9 || type == 12 ? 3 : 4;
            unsigned stride = 0, extent = width * 4;
            if (columns > 1) {
                stride = (!standard && !storage) || width >= 3 ? 16 : width * 4;
                extent = stride * columns;
            }
            unsigned elements = array == 2 ? 3 : 1;
            if (array) {
                unsigned alignment = (!standard && !storage) ? 16 : width == 3 ? 16 : width * 4;
                unsigned array_stride = (extent + alignment - 1) / alignment * alignment;
                stride = columns > 1 ? array_stride / columns : array_stride;
                extent = array_stride * elements;
            }
            GlslcUniform expected = {"value",width,elements*columns,stride,0};
            char text[1024];
            snprintf(text,sizeof(text),"#version 450\nlayout(%s%s,std%s) uniform U{%s value%s;}u;layout(location=0)out vec4 o;void main(){o=vec4(1);}",storage?"push_constant":"binding=0","",standard?"430":"140",types[type],array==0?"":array==1?"[1]":"[3]");
            test(GLSLC_STAGE_FRAGMENT,text,version,storage?NULL:&expected,storage?0:1,storage?0:(extent+15)/16*16,storage?&expected:NULL,storage?1:0,storage?extent:0);
        }
        GlslcResult bad;
        GlslcOptions opts = {.emit_layout=true};
        assert(!glslc_compile(GLSLC_STAGE_VERTEX,"layout(binding=0) uniform U{vec4 x;}; invalid",&opts,&bad));
        assert(!bad.words && !bad.uniforms.members && !bad.push_constants.members);
    }
    if (argc == 2) {
        FILE *manifest = fopen(argv[1], "r"); assert(manifest);
        char path[4096];
        while (fgets(path, sizeof(path), manifest)) {
            path[strcspn(path,"\r\n")] = 0;
            FILE *file = fopen(path,"rb"); assert(file);
            assert(!fseek(file,0,SEEK_END)); long length = ftell(file); assert(length >= 0);
            rewind(file); char *text = malloc(length + 1); assert(text);
            assert(fread(text,1,length,file) == (size_t)length); text[length] = 0; fclose(file);
            GlslcStage stage = strstr(path,"_vsh") ? GLSLC_STAGE_VERTEX : strstr(path,"_psh") ? GLSLC_STAGE_FRAGMENT : GLSLC_STAGE_GEOMETRY;
            for (unsigned version = 0; version < 2; version++)
                test(stage,text,version,NULL,SIZE_MAX,0,NULL,0,0);
            free(text);
        }
        fclose(manifest);
    }
    printf("PASS: %u compiler layouts, identical SPIR-V, padding/arrays/matrices, optional metadata and independent module lifetimes\n",cases);
}
