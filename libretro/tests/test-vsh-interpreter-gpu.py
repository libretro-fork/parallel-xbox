#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the core interpreter against specialized GLSL, including native reuse.

Only the test header adds a code field to the numerical UBO. This does not
change the renderer ABI or establish a production transport/readiness path.
The reference omits that field, preserving its original shader declarations.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile

root = None
cc = None


def prepare(r, p, mutation=''):
    inc = p / 'include/hw/xbox/nv2a/pgraph'
    (inc / 'glsl').mkdir(parents=True)
    (inc / 'spv').mkdir()
    h = (r / 'hw/xbox/nv2a/pgraph/glsl/vsh.h').read_text()
    needle = '    DECL(S, surfaceSize, vec2, 1)'
    assert h.count(needle) == 1
    h = h.replace(needle, needle + ' \\\n    DECL(S, vpCode, ivec4, NV2A_MAX_TRANSFORM_PROGRAM_LENGTH)')
    h = h.replace('\n#endif', '\nMString *pgraph_glsl_gen_vsh_interpreter(const VshState *, GenVshGlslOptions);\n#endif')
    (inc / 'glsl/vsh.h').write_text(h)
    (inc / 'spv/vsh.h').write_text((r / 'hw/xbox/nv2a/pgraph/spv/vsh.h').read_text())

    source = r / 'hw/xbox/nv2a/pgraph/glsl/vsh-prog.c'
    mutations = {
        'shared_c': ('if(ilu>=2u && ilu<=6u)value=vec4(value.x);',
                     'if(false && ilu>=2u && ilu<=6u)value=vec4(value.x);'),
        'mirror_fold': ('bool rcpMirror=ilu==2u', 'bool rcpMirror=false && ilu==2u'),
        'lit_fold': ('bool litMirror=ilu==7u', 'bool litMirror=false && ilu==7u'),
        'lit_result': ('result=vec4(1.0,x,x>0.0?exp2(1.0*log2(y)):0.0,1.0);',
                       'result=vec4(1.0,x,0.0,1.0);'),
        'final_bit': ('if((t.w&1u)!=0u)break;', 'if(false && (t.w&1u)!=0u)break;'),
    }
    if mutation in mutations:
        code = source.with_name('vsh-interpreter.inc').read_text()
        before, after = mutations[mutation]
        assert code.count(before) == 1
        (p / 'vsh-interpreter.inc').write_text(code.replace(before, after))
        (p / 'vsh-prog.c').write_text(source.read_text())
        source = p / 'vsh-prog.c'

    script = (r / 'libretro/tests/test-spv-vsh-gpu.py').read_text()
    needle = "ref+=function(r/'hw/xbox/nv2a/pgraph/glsl/vsh.c','pgraph_glsl_gen_vsh')+'\\n'"
    assert script.count(needle) == 1
    old_call = '''        pgraph_glsl_gen_vsh_prog(
            VSH_VERSION_XVS, (uint32_t *)state->programmable.program_data,
            state->programmable.program_length, header, body);'''
    replacement = '        pgraph_glsl_gen_vsh_prog_interpreter(header, body);'
    extra = "\n base=function(r/'hw/xbox/nv2a/pgraph/glsl/vsh.c','pgraph_glsl_gen_vsh')\n"
    extra += " assert base.count(" + repr(old_call) + ") == 1\n"
    extra += " ref+=base.replace('const UniformInfo *info = &VshUniformInfo[i];', 'const UniformInfo *info = &VshUniformInfo[i]; if(i==VshUniform_vpCode)continue;')+'\\n'\n"
    extra += " interpreted=base.replace('pgraph_glsl_gen_vsh(', 'pgraph_glsl_gen_vsh_interpreter(',1)\n"
    if mutation != 'compile_program':
        extra += ' interpreted=interpreted.replace(' + repr(old_call) + ', ' + repr(replacement) + ')\n'
    extra += " ref+=interpreted+'\\n'"
    script = script.replace(needle, extra)
    needle = "'-I'+str(d),'-I'+str(r)"
    assert script.count(needle) == 1
    include_dirs = [p / 'include', inc / 'glsl',
                    r / 'hw/xbox/nv2a/pgraph/glsl', r / 'hw/xbox/nv2a/pgraph/spv']
    script = script.replace(needle, "'-I'+str(d)," + ','.join("'-I'+" + repr(str(x)) for x in include_dirs) + ",'-I'+str(r)")
    needle = "r/'hw/xbox/nv2a/pgraph/glsl/vsh-prog.c'"
    assert script.count(needle) == 1
    script = script.replace(needle, 'Path(' + repr(str(source)) + ')')
    batches = [(0, 1568), (1568, 3136), (36368, 37392), (37392, 38416),
               (38416, 39440), (39440, 40464), (40464, 42512)]
    line = next(line for line in script.splitlines() if line.startswith(' batches='))
    script = script.replace(line, ' batches=' + repr(batches))
    # The original harness retains its four-worker cap, shared 300s batch
    # watchdog, exact comparisons, validation errors and orderly child joins.
    (p / 'driver.py').write_text(script)

    c=(r/'libretro/tests/test-spv-vsh-gpu.c').read_text()
    c=c.replace('assert(pgraph_spv_vsh(&state,opts,&modules[1]));direct_ticks+=clock()-before;direct_words+=modules[1].num_words;', 'assert(pgraph_spv_vsh_supported(&state,opts));')
    needle='  VshUniformValues values={0};';assert c.count(needle)==1
    c=c.replace(needle,'''  before=clock();
      MString *interpreted=pgraph_glsl_gen_vsh_interpreter(&state,(GenVshGlslOptions){.vulkan=true,.use_push_constants_for_uniform_attrs=opts.push_constants});
      if(!state.is_fixed_function) {
       /* A different valid microcode must not alter the host shader source.
        * Mutate/restore only one 16-byte token and length in this local recipe. */
       uint32_t saved_token[4];memcpy(saved_token,state.programmable.program_data[0],sizeof(saved_token));
       int saved_length=state.programmable.program_length;
       token(state.programmable.program_data[0],MAC_MOV,ILU_NOP,15,0,false,true);
       state.programmable.program_length=1;
       MString *different=pgraph_glsl_gen_vsh_interpreter(&state,(GenVshGlslOptions){.vulkan=true,.use_push_constants_for_uniform_attrs=opts.push_constants});
       assert(!strcmp(mstring_get_str(interpreted),mstring_get_str(different)));
       mstring_unref(different);
       memcpy(state.programmable.program_data[0],saved_token,sizeof(saved_token));state.programmable.program_length=saved_length;
      }
      if(!glslc_compile(GLSLC_STAGE_VERTEX,mstring_get_str(interpreted),&glopts,&modules[1])){fprintf(stderr,"INTERPRETER: %s\\n",modules[1].error);abort();}
      mstring_unref(interpreted);
      direct_ticks+=clock()-before;direct_words+=modules[1].num_words;
    '''+needle)
    needle='  for(unsigned path=0;path<2;path++) {';assert c.count(needle)==1
    c=c.replace(needle,'''  memcpy(values.vpCode,state.programmable.program_data,sizeof(values.vpCode));
      if(!state.is_fixed_function && state.programmable.program_length<NV2A_MAX_TRANSFORM_PROGRAM_LENGTH){
       uint32_t poison[4];token(poison,MAC_MOV,ILU_NOP,15,9,false,true);
       memcpy(values.vpCode[NV2A_MAX_TRANSFORM_PROGRAM_LENGTH-1],poison,sizeof(poison));
      }
    '''+needle)
    c=c.replace('Translation CPU clock: GLSL %.3f ms, direct %.3f ms; emitted words GLSL=%zu direct=%zu','Translation CPU clock: specialized %.3f ms, interpreter %.3f ms; emitted words specialized=%zu interpreter=%zu')
    c=c.replace('Direct VSH Vulkan:','Private VSH interpreter Vulkan:')
    # Both compared GLSL paths need the same defined FP32 environment for point
    # calculations. The original fixture's direct emitter supplies it itself.
    c=c.replace('if(!path && (state.point_params_enable || state.is_fixed_function))','if(state.point_params_enable || state.is_fixed_function)')
    def add_ready_owners(c):
        needle='static VkDevice device;';assert c.count(needle)==1
        c=c.replace(needle,needle+'''
    /* Test-only bounded owners. No word/source clone: each new entry takes its
     * first MString and compiled module; later cases borrow that final owner. */
    static struct PrototypeReady {MString *source;GlslcResult module;VkPipeline pipeline;} ready[128];
    static unsigned ready_count,ready_pipeline_builds,ready_pipeline_reuses;
    ''')
        first=c.index('  if(!glslc_compile(GLSLC_STAGE_VERTEX,mstring_get_str(interpreted)')
        last=c.index('  VshUniformValues values={0};',first)
        c=c[:first]+'''  unsigned ready_index=0;
      for(;ready_index<ready_count;ready_index++)if(!strcmp(mstring_get_str(ready[ready_index].source),mstring_get_str(interpreted)))break;
      if(ready_index==ready_count){
       assert(ready_count<128);
       ready[ready_index].source=interpreted;
       if(!glslc_compile(GLSLC_STAGE_VERTEX,mstring_get_str(interpreted),&glopts,&ready[ready_index].module)){fprintf(stderr,"INTERPRETER: %s\\n",ready[ready_index].module.error);abort();}
       ready_count++;
       direct_words+=ready[ready_index].module.num_words;
      }else mstring_unref(interpreted);
      modules[1]=ready[ready_index].module; /* immutable metadata/word lend */
      direct_ticks+=clock()-before;
    '''+c[last:]
        first=c.index('   size_t words;GlslcResult reference_module=modules[path];')
        last=c.index('   VkCommandBufferBeginInfo bi=',first)
        section=c[first:last].replace('VkShaderModule shader;','').replace('VkPipeline pipeline;','')
        c=c[:first]+'''   VkPipeline pipeline=VK_NULL_HANDLE;VkShaderModule shader=VK_NULL_HANDLE;
       if(path && ready[ready_index].pipeline){pipeline=ready[ready_index].pipeline;ready_pipeline_reuses++;}
       else {
    '''+section+'''    if(path){ready[ready_index].pipeline=pipeline;ready_pipeline_builds++;}
       }
    '''+c[last:]
        needle='vkDestroyPipeline(device,pipeline,NULL);vkDestroyShaderModule(device,shader,NULL);';assert c.count(needle)==1
        c=c.replace(needle,'if(!path)vkDestroyPipeline(device,pipeline,NULL);if(shader)vkDestroyShaderModule(device,shader,NULL);')
        needle='release(&modules[0]);release(&modules[1]);';assert c.count(needle)==1
        c=c.replace(needle,'release(&modules[0]); /* modules[1] belongs to ready[], not this borrow */')
        needle=' drop(input);drop(output);drop(uniform);';assert c.count(needle)==1
        c=c.replace(needle,''' assert(ready_pipeline_builds==ready_count);
     printf("Interpreter ready owners: %u shader compiles, %u native PSOs, %u pipeline reuses; cache handoff payload clones=0 (TF conversion excluded)\\n",ready_count,ready_pipeline_builds,ready_pipeline_reuses);
     for(unsigned i=0;i<ready_count;i++){vkDestroyPipeline(device,ready[i].pipeline,NULL);release(&ready[i].module);mstring_unref(ready[i].source);}
    '''+needle)
        return c
    c=add_ready_owners(c)
    (p/'driver.c').write_text(c)



def run(folder, mutation='', case=None):
    prepare(root, folder, mutation)
    env = dict(os.environ)
    compiler = shutil.which(cc) or cc
    env['PATH'] = str(Path(compiler).resolve().parent) + os.pathsep + env.get('PATH', '')
    if case is not None:
        env['SPV_VSH_GPU_RANGE'] = str(case) + ':' + str(case + 1)
    log = folder / 'result.log'
    with log.open('w') as stream:
        result = subprocess.run([sys.executable, str(folder / 'driver.py'), str(root), cc],
                                stdout=stream, stderr=subprocess.STDOUT, env=env)
    text = log.read_text(errors='replace')
    if not mutation:
        print(text, end='', flush=True)
        assert result.returncode == 0, result.returncode
    elif mutation == 'compile_program':
        assert result.returncode != 0 and 'Assertion' in text and '!strcmp(mstring_get_str(interpreted),mstring_get_str(different))' in text, text
    else:
        mismatch = re.search(r'[1-9][0-9]* differences, 0 validation errors;', text)
        assert result.returncode != 0 and mismatch, text
        print('Control evidence:', mutation, mismatch.group(0), flush=True)
    if mutation:
        print('Required interpreter control rejected:', mutation, flush=True)
    return text


def main():
    global root, cc
    root = Path(sys.argv[1]).resolve()
    cc = sys.argv[2] if len(sys.argv) > 2 else 'cc'
    with tempfile.TemporaryDirectory(prefix='vsh-interpreter-') as temporary:
        directory = Path(temporary)
        positive = directory / 'positive'
        positive.mkdir()
        device_log = run(positive)
        for name, case in [('shared_c', 31), ('mirror_fold', 589), ('lit_fold', 659),
                           ('lit_result', 659), ('final_bit', 0), ('compile_program', 0)]:
            # The literal-exponent mutation distinguishes Mesa's optimization.
            # NVIDIA's reference keeps exp/log, so that mutation is not a valid
            # negative control on that driver. Keep it required in software CI.
            if name == 'lit_fold' and 'llvmpipe' not in device_log:
                print('Literal-fold control requires llvmpipe; the LIT-result control remains required here', flush=True)
                continue
            folder = directory / name
            folder.mkdir()
            run(folder, name, case)


if __name__ == '__main__':
    main()
