#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual dynamic-control builder, exact vertex outputs and bounded owners.

Keep the full existing programmable corpus and add 384 metadata cases with
observable color, point and fog writes. GL draws through native separable
program pipelines in both interface modes. Vulkan retains separate native
FP32-target owners for the same source; neither target may depend on arrival
order. Readbacks and source-text lookup are correctness machinery only.
"""
from pathlib import Path
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile


def replace_once(text,before,after):
    assert text.count(before)==1,(before,text.count(before))
    return text.replace(before,after)


def prepare(root,directory,backend,mutation=''):
    path=Path(__file__).with_name('test-vsh-interpreter-'+('gl' if backend=='gl' else 'gpu')+'.py')
    spec=importlib.util.spec_from_file_location('base_fixture',path)
    fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
    inherited=mutation if mutation in ['shared_c','mirror_fold','lit_fold','lit_result','final_bit','point_written','compile_program'] else ''
    fixture.prepare(root,directory,inherited)
    h=directory/'include/hw/xbox/nv2a/pgraph/glsl/vsh.h'
    needle='DECL(S, vpCode, ivec4, NV2A_MAX_TRANSFORM_PROGRAM_LENGTH)'
    h.write_text(replace_once(h.read_text(),needle,needle+' \\\n    DECL(S, vpFlags, uint, 1)'))
    script=(directory/'driver.py').read_text()
    script=replace_once(script,"'pgraph_glsl_gen_vsh_interpreter')","'pgraph_glsl_gen_vsh_interpreter_dynamic')")
    script=replace_once(script,'if(i==VshUniform_vpCode)continue;','if(i==VshUniform_vpCode || i==VshUniform_vpFlags)continue;')
    if mutation=='compile_program':
        script=replace_once(script,"interpreted.replace('state, opts, true','state, opts, false')",
                            "interpreted.replace('state, opts, true, true','state, opts, false, false')")
    if mutation=='point_literal':
        controls=(root/'hw/xbox/nv2a/pgraph/glsl/vsh-controls.inc').read_text()
        controls=replace_once(controls,'irPointWritten ? oPts.x * pointScale : 0.0 * pointScale','oPts.x * pointScale')
        (directory/'vsh-controls.inc').write_text(controls)
        script=replace_once(script,"controls=r/'hw/xbox/nv2a/pgraph/glsl/vsh-controls.inc'",'controls=Path('+repr(str(directory/'vsh-controls.inc'))+')')
    line=next(line for line in script.splitlines() if line.startswith(' batches='))
    script=replace_once(script,line,line[:-1]+', (42512, 42896)]')
    script=script.replace('assert 0<=first<limit<=42512','assert 0<=first<limit<=42896')
    code=(directory/'driver.c').read_text().replace('pgraph_glsl_gen_vsh_interpreter(','pgraph_glsl_gen_vsh_interpreter_dynamic(')
    code=replace_once(code,'int main(', '#include "'+Path(__file__).with_suffix('.inc').as_posix()+'"\nint main(')
    code=code.replace('limit<=42512','limit<=42896')
    needle='  /* Native GLSL has no Vulkan execution mode. */' if backend=='gl' else '  opts.preserve_float32=fp.shaderSignedZeroInfNanPreserveFloat32;'
    code=replace_once(code,needle,'  metadata_case(test,&state,&opts,&variant,&m,&l);\n'+needle)
    needle='int saved_length=state.programmable.program_length;'
    code=replace_once(code,needle,needle+'\n  uint32_t saved_controls=control_flags(&state);set_control_flags(&state,saved_controls^31u);state.fog_mode=FOG_MODE_LINEAR;')
    needle='state.programmable.program_length=saved_length;'
    code=replace_once(code,needle,needle+'set_control_flags(&state,saved_controls);')
    needle='memcpy(values.vpCode,state.programmable.program_data,sizeof(values.vpCode));'
    flags='control_flags(&state)|0xa5a50000u'
    if mutation=='flags_zero':flags='0u'
    masks={'flags_point':1,'flags_specular':2,'flags_alpha':4,'flags_fog_enable':8,'flags_fog_dynamic':16,'flags_fog_mode':224}
    if mutation in masks:flags='('+flags+')^'+str(masks[mutation])+'u'
    if mutation=='flags_fog_mode':flags='('+flags+')&~224u'
    code=replace_once(code,needle,'if(test>=42512)values.fogCtl[0]=1u|((test%8)<<1)|0xa5000000u;\n  values.vpFlags[0]='+flags+';\n  '+needle)
    if backend=='gl':
        code=replace_once(code,'glBufferData(GL_UNIFORM_BUFFER,136*16,NULL,GL_STREAM_DRAW);','glBufferData(GL_UNIFORM_BUFFER,VSH_INTERPRETER_BUFFER_SIZE,NULL,GL_STREAM_DRAW);')
        code=replace_once(code,'assert(code_size==136*16);','assert(code_size==VSH_INTERPRETER_BUFFER_SIZE);')
        needle='glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(values.vpCode),values.vpCode);'
        code=replace_once(code,needle,needle+'glBufferSubData(GL_UNIFORM_BUFFER,VSH_INTERPRETER_FLAGS_OFFSET,sizeof(values.vpFlags),values.vpFlags);')
        if mutation=='code_upload':code=replace_once(code,needle,'memset(values.vpCode,0,sizeof(values.vpCode));'+needle)
        code=make_separable(code)
        if mutation=='pipeline_stage':
            needle='glUseProgram(0);glBindProgramPipeline(pipelines[path]);'
            code=replace_once(code,needle,needle+'if(path)glUseProgramStages(pipelines[path],GL_VERTEX_SHADER_BIT,0);')
    else:
        code=replace_once(code,'GlslcResult module;VkPipeline pipeline;','GlslcResult module;VkPipeline pipeline[2];')
        needle='   VkPipeline pipeline=VK_NULL_HANDLE;VkShaderModule shader=VK_NULL_HANDLE;'
        code=replace_once(code,needle,'   unsigned fp_target=state.point_params_enable || state.is_fixed_function;\n'+needle)
        code=code.replace('ready[ready_index].pipeline','ready[ready_index].pipeline[fp_target]')
        code=replace_once(code,'assert(ready_pipeline_builds==ready_count);','unsigned expected=0;for(unsigned i=0;i<ready_count;i++){assert(ready[i].pipeline[0] || ready[i].pipeline[1]);for(unsigned fp=0;fp<2;fp++)expected+=ready[i].pipeline[fp]!=VK_NULL_HANDLE;}assert(ready_pipeline_builds==expected);')
        code=replace_once(code,'vkDestroyPipeline(device,ready[i].pipeline,NULL);','for(unsigned fp=0;fp<2;fp++)if(ready[i].pipeline[fp])vkDestroyPipeline(device,ready[i].pipeline[fp],NULL);')
        if mutation=='fp_target':
            code=replace_once(code,'if(state.point_params_enable || state.is_fixed_function)reference_module.words=preserve_float',
                              'if(path ? !fp_target : (state.point_params_enable || state.is_fixed_function))reference_module.words=preserve_float')
        # Independently check the native target before conversion/creation. Some
        # drivers produce the same arithmetic for both modes, so a numerical
        # mutation alone cannot prove that the owner selected the right target.
        needle='   uint32_t *code=xfb(&reference_module,&words);'
        code=replace_once(code,needle,'''   bool fp32_mode=false;
   for(size_t p=5;p<reference_module.num_words;){
    const uint32_t *instruction=reference_module.words+p;
    unsigned count=instruction[0]>>16;assert(count && p+count<=reference_module.num_words);
    if((instruction[0]&65535)==SpvOpExecutionMode && count==4 &&
       instruction[2]==SpvExecutionModeSignedZeroInfNanPreserve && instruction[3]==32)fp32_mode=true;
    p+=count;
   }
   assert(fp32_mode==(state.point_params_enable || state.is_fixed_function));
'''+needle)
    (directory/'driver.c').write_text(code)
    (directory/'driver.py').write_text(script)


def make_separable(code):
    code=replace_once(code,'glAttachShader(program,shader);','glProgramParameteri(program,GL_PROGRAM_SEPARABLE,GL_TRUE);glAttachShader(program,shader);')
    names=['vtxD0','vtxD1','vtxB0','vtxB1','vtxFog','gl_SkipComponents3','vtxT0','vtxT1','vtxT2','vtxT3','vtxPos0','vtxPos1','vtxPos2','triMZ','gl_SkipComponents3','gl_Position','gl_PointSize','gl_SkipComponents3']
    prefixed=','.join('"'+('v_'+n if not n.startswith('gl_') else n)+'"' for n in names)
    code=replace_once(code,'    glTransformFeedbackVaryings(program,ARRAY_SIZE(varyings),varyings,GL_INTERLEAVED_ATTRIBS);',
                      '    const char *prefixed_varyings[]={'+prefixed+'};\n    glTransformFeedbackVaryings(program,ARRAY_SIZE(varyings),strstr(source,"#define vtxD0 v_vtxD0")?prefixed_varyings:varyings,GL_INTERLEAVED_ATTRIBS);')
    code=replace_once(code,'static struct {MString *source;GLuint program;} ready[128];','static GLuint make_pipeline(GLuint program){GLuint p;glGenProgramPipelines(1,&p);glUseProgramStages(p,GL_VERTEX_SHADER_BIT,program);return p;}\nstatic struct {MString *source;GLuint program,pipeline;} ready[128];')
    code=replace_once(code,'  GenVshGlslOptions glopts={.locations=true};','  GenVshGlslOptions glopts={.locations=true,.prefix_outputs=getenv("VSH_SSO_PREFIX")!=NULL};')
    code=replace_once(code,'ready[ri].program=compile_program(text[1]);ready_count++;','ready[ri].program=compile_program(text[1]);ready[ri].pipeline=make_pipeline(ready[ri].program);ready_count++;')
    needle='  GLuint program[2]={compile_program(text[0]),ready[ri].program};mstring_unref(text[0]);'
    code=replace_once(code,needle,needle+'\n  GLuint pipelines[2]={make_pipeline(program[0]),ready[ri].pipeline};')
    needle='   glUseProgram(program[path]);upload_uniforms(program[path],&values);'
    code=replace_once(code,needle,needle+'\n   glUseProgram(0);glBindProgramPipeline(pipelines[path]);')
    code=replace_once(code,'  glDeleteProgram(program[0]);','  glDeleteProgramPipelines(1,&pipelines[0]);glDeleteProgram(program[0]);')
    code=replace_once(code,' glUseProgram(0);for(unsigned i=0;i<ready_count;i++){glDeleteProgram(ready[i].program);',' glUseProgram(0);glBindProgramPipeline(0);for(unsigned i=0;i<ready_count;i++){glDeleteProgramPipelines(1,&ready[i].pipeline);glDeleteProgram(ready[i].program);')
    return code


def run(root,compiler,directory,backend,mutation='',case=None,prefix=False):
    prepare(root,directory,backend,mutation)
    environment=dict(os.environ,EGL_PLATFORM='surfaceless')
    environment['PATH']=str(Path(shutil.which(compiler) or compiler).resolve().parent)+os.pathsep+environment.get('PATH','')
    environment.pop('VSH_SSO_PREFIX',None)
    if prefix:environment['VSH_SSO_PREFIX']='1'
    if case is not None:environment['SPV_VSH_GPU_RANGE']=str(case)+':'+str(case+1)
    with (directory/'result.log').open('w') as stream:
        result=subprocess.run([sys.executable,str(directory/'driver.py'),str(root),compiler],env=environment,stdout=stream,stderr=subprocess.STDOUT)
    text=(directory/'result.log').read_text(errors='replace')
    if not mutation:
        print(text,end='',flush=True);assert result.returncode==0,result.returncode
    elif mutation=='compile_program':
        assert result.returncode!=0 and 'Assertion' in text and '!strcmp(mstring_get_str(interpreted),mstring_get_str(different))' in text,text
    elif mutation=='pipeline_stage':
        assert result.returncode!=0 and 'GL error 502 case0 path1' in text,text
    elif mutation=='fp_target':
        assert result.returncode!=0 and 'Assertion' in text and 'fp32_mode==' in text,text
    else:
        mismatch=re.search(r'[1-9][0-9]* differences[,;]',text)
        assert result.returncode!=0 and mismatch,text
        print('Control evidence:',mutation,mismatch.group(0),flush=True)
    if mutation:print('Required dynamic interpreter control rejected:',mutation,flush=True)
    return text


def main():
    root=Path(sys.argv[1]).resolve();compiler=sys.argv[2] if len(sys.argv)>2 else 'cc'
    backend=sys.argv[3] if len(sys.argv)>3 else 'vk';assert backend in ['gl','vk']
    with tempfile.TemporaryDirectory(prefix='vsh-dynamic-') as temporary:
        directory=Path(temporary)
        positive=directory/'positive';positive.mkdir();device=run(root,compiler,positive,backend)
        if backend=='gl':
            p=directory/'prefixed';p.mkdir();run(root,compiler,p,backend,prefix=True)
        controls=[('shared_c',31),('mirror_fold',589),('lit_fold',659),('lit_result',659),('final_bit',0),('compile_program',0),
                  ('flags_zero',42514),('flags_point',42513),('flags_specular',42514),('flags_alpha',42518),('flags_fog_enable',42520),('flags_fog_dynamic',42528),('flags_fog_mode',42552)]
        controls+= [('point_literal',1582),('point_written',1709),('code_upload',0),('pipeline_stage',0)] if backend=='gl' else [('fp_target',0)]
        for mutation,case in controls:
            if mutation=='lit_fold' and 'llvmpipe' not in device:
                print('Literal-fold control requires llvmpipe; LIT-result remains required',flush=True);continue
            p=directory/mutation;p.mkdir();run(root,compiler,p,backend,mutation,case)


if __name__=='__main__':main()
