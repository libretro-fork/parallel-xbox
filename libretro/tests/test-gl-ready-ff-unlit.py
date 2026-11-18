#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exact separable-GL proof for unlit controls with already-dynamic fog.

Keep the original full FF corpus, references, numeric rules and shared batch
deadlines. Count cases outside the additional domain explicitly. No literal
fog conversion, GPU readback or source lookup is added to the runtime.
"""
from pathlib import Path
import importlib.util,os,re,shutil,subprocess,sys,tempfile


def once(text,before,after):
    assert text.count(before)==1,before
    return text.replace(before,after)


def prepare(root,directory,mutation=''):
    path=Path(__file__).with_name('test-vsh-interpreter-gl.py')
    spec=importlib.util.spec_from_file_location('ordinary_gl_fixture',path)
    fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
    fixture.prepare(root,directory)
    code=(directory/'driver.c').read_text()
    code='#include "'+(root/'hw/xbox/nv2a/pgraph/gl/ready-ff-state.h').as_posix()+'"\n'+code
    code=once(code,'static GLuint compile_program(MString *text)','static GLuint compile_program(MString *text,bool prefix)')
    code=once(code,'    glAttachShader(program,shader);','    glProgramParameteri(program,GL_PROGRAM_SEPARABLE,GL_TRUE);glAttachShader(program,shader);')
    needle='    glTransformFeedbackVaryings(program,ARRAY_SIZE(varyings),varyings,GL_INTERLEAVED_ATTRIBS);'
    prefix='''    const char *prefixed[]={"v_vtxD0","v_vtxD1","v_vtxB0","v_vtxB1","v_vtxFog","gl_SkipComponents3","v_vtxT0","v_vtxT1","v_vtxT2","v_vtxT3","v_vtxPos0","v_vtxPos1","v_vtxPos2","v_triMZ","gl_SkipComponents3","gl_Position","gl_PointSize","gl_SkipComponents3"};
    if(prefix)memcpy(varyings,prefixed,sizeof(varyings));
'''
    code=once(code,needle,prefix+needle)
    code=once(code,'assert(first<limit && limit<=42512)','assert(first>=3136 && first<limit && limit<=36368)')
    code=once(code,'unsigned compared=0,failures=0;','unsigned compared=0,failures=0,eligible=0,outside=0;')
    start=code.index('  GenVshGlslOptions glopts=');end=code.index('  VshUniformValues values=',start)
    block='''  bool prefix=!!((test/8)&1);
  int index=ready_ff_state_index(&state,prefix);
  if(index<0 || state.fixed_function.csv0c_dynamic || state.fixed_function.lighting){outside++;continue;}
  eligible++;
  GenVshGlslOptions glopts={.locations=true,.prefix_outputs=prefix};
  VshState canonical;ready_ff_state_init(&canonical,index);
  /* MUTATE_SOURCE */
  MString *text[2]={pgraph_glsl_gen_vsh(&state,glopts),pgraph_glsl_gen_vsh(&canonical,glopts)};
  unsigned ri=0;for(;ri<ready_count;ri++)if(!strcmp(mstring_get_str(ready[ri].source),mstring_get_str(text[1])))break;
  if(ri==ready_count){assert(ready_count<56);ready[ri].source=text[1];ready[ri].program=compile_program(text[1],prefix);ready_count++;}
  else{mstring_unref(text[1]);ready_reuses++;}
  GLuint program[2]={compile_program(text[0],prefix),ready[ri].program};mstring_unref(text[0]);
'''
    code=code[:start]+block+code[end:]
    start=code.index('  memcpy(values.vpCode,state.programmable.program_data,');end=code.index('  glBindBuffer(GL_ARRAY_BUFFER',start)
    pack='''  /* Independent input packing mirrors hardware controls that the original
   * literal shader consumed as static fields; its generated text is unchanged. */
  const FixedFunctionVshState *ff=&state.fixed_function;
  values.csv0cCtl[0]=(ff->local_eye?2u:0u)|(ff->normalization?4u:0u)|
   ((unsigned)ff->emission_src<<4)|((unsigned)ff->ambient_src<<6)|((unsigned)ff->diffuse_src<<8)|((unsigned)ff->specular_src<<10)|
   (state.specular_enable?4096u:0u)|(state.separate_specular?8192u:0u)|(state.ignore_specular_alpha?16384u:0u)|(ff->two_sided?32768u:0u);
  if(!ff->texgen_dynamic){
   values.texMatEnable[0]=0;
   for(unsigned t=0;t<4;t++){
    uint32_t packed=values.texgenMode[t]&4096u;
    for(unsigned a=0;a<4;a++)packed|=(unsigned)ff->texgen[t][a]<<(a*3);
    values.texgenMode[t]=packed;if(ff->texture_matrix_enable[t])values.texMatEnable[0]|=1u<<t;
   }
  }
'''
    code=code[:start]+pack+code[end:]
    code=once(code,'  glBindBuffer(GL_UNIFORM_BUFFER,buffers[2]);glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(values.vpCode),values.vpCode);','')
    code=once(code,'return failures?1:0;','printf("Unlit FF domain: %u eligible, %u outside; exact checks=%u\\n",eligible,outside,compared);return failures?1:0;')
    if mutation in ['point','skin']:
        source={'point':'canonical.point_params_enable=!canonical.point_params_enable;',
                'skin':'canonical.fixed_function.skinning=(canonical.fixed_function.skinning+1)%7;'}[mutation]
        code=once(code,'  /* MUTATE_SOURCE */',source)
    elif mutation:
        field,mask={'specular':('csv0cCtl',4096),'matrix':('texMatEnable',1),'fog':('fogCtl',1)}[mutation]
        needle='   glUseProgram(program[path]);upload_uniforms(program[path],&values);'
        code=once(code,needle,needle+'\n   if(path)glUniform1ui(glGetUniformLocation(program[path],"'+field+'"),values.'+field+'[0]^'+str(mask)+'u);')
    (directory/'driver.c').write_text(code)
    script=(directory/'driver.py').read_text()
    line=next(line for line in script.splitlines() if line.startswith(' batches='))
    batches=[(3136,4160),(4160,5120),(5120,6416),(6416,9488),(9488,11536),(11536,12560),(12560,14096),(14096,15120),(15120,19216),(19216,19984),(19984,24080),(24080,28176),(28176,32272),(32272,36368)]
    script=once(script,line,' batches='+repr(batches))
    (directory/'driver.py').write_text(script)


def main():
    root=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
    with tempfile.TemporaryDirectory(prefix='unlit-ff-bank-') as tmp:
        parent=Path(tmp)
        for mutation,case in [('',None),('point',15144),('skin',15136),('specular',15136),('matrix',15136),('fog',15136)]:
            directory=parent/(mutation or 'positive');directory.mkdir();prepare(root,directory,mutation)
            env=dict(os.environ,EGL_PLATFORM='surfaceless');env['PATH']=str(Path(shutil.which(cc) or cc).resolve().parent)+os.pathsep+env.get('PATH','')
            env.pop('SPV_VSH_GPU_RANGE',None)
            env.pop('SPV_VSH_FIXTURE_ROUND',None)
            env.pop('SPV_VSH_FIXTURE_ROUNDS',None)
            if case is not None:env['SPV_VSH_GPU_RANGE']=str(case)+':'+str(case+1)
            with (directory/'run.log').open('w') as stream:result=subprocess.run([sys.executable,str(directory/'driver.py'),str(root),cc],env=env,stdout=stream,stderr=subprocess.STDOUT)
            log=(directory/'run.log').read_text(errors='replace')
            if not mutation:
                assert result.returncode==0,log[-4000:]
                rows=re.findall(r'Unlit FF domain: (\d+) eligible, (\d+) outside; exact checks=(\d+)',log)
                assert rows and sum(int(a)+int(b) for a,b,c in rows)==33232,rows
                count=sum(int(a) for a,b,c in rows);checks=sum(int(c) for a,b,c in rows)
                assert count==816 and checks==count*1632,(count,checks)
                print(f'PASS unlit FF: full 33232-case domain ledger; {count} eligible / {checks} exact scalar checks; literal fog and active-light classes retain fallback',flush=True)
            else:
                assert result.returncode!=0 and re.search(r'[1-9][0-9]* differences;',log),(mutation,result.returncode,log[-4000:])
                print('Required unlit FF fault rejected:',mutation,flush=True)
    # A historical candidate without this later capability retains its
    # complete original corpus. Advertised coverage with a missing fixture
    # must fail, rather than silently skipping the additional proof.
    idle=root/'libretro/tests/test-gl-ready-ff-idle.py'
    header=(root/'hw/xbox/nv2a/pgraph/gl/ready-ff-state.h').read_text()
    if idle.is_file() or 'GL_READY_FF_IDLE_LIGHTING' in header:
        assert idle.is_file(), 'Advertised idle-lighting coverage has no numerical fixture'
        subprocess.run([sys.executable,str(idle),str(root),cc],check=True)
    if 'GL_READY_FF_LITERAL_LIGHTING' in header:
        literal=root/'libretro/tests/test-gl-ready-ff-literal.py'
        assert literal.is_file(), 'Advertised literal-lighting coverage has no numerical fixture'
        subprocess.run([sys.executable,str(literal),str(root),cc],check=True)
    if 'GL_READY_FF_CSV_LITERAL_TEXGEN' in header:
        csv_texgen=root/'libretro/tests/test-gl-ready-ff-csv-texgen.py'
        assert csv_texgen.is_file(), 'Advertised CSV/literal-texgen coverage has no numerical fixture'
        subprocess.run([sys.executable,str(csv_texgen),str(root),cc],check=True)


if __name__=='__main__':main()
