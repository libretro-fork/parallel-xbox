#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual prepared programmable-vertex startup and code-storage ownership."""
from pathlib import Path
import os,shutil,subprocess,sys,tempfile
root=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
source=(root/'hw/xbox/nv2a/pgraph/gl/ready-vsh.inc').read_text()
def function(text,signature):
 start=text.index(signature);end=text.index('{',start)+1;depth=1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]
production='\n'.join(function(source,s) for s in ['static void ready_vsh_finalize(', 'static void ready_vsh_init('])
template=Path(__file__).with_suffix('.c').read_text()
env=dict(os.environ);env['PATH']=str(Path(shutil.which(cc) or cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='ready-vsh-startup-') as tmp:
 d=Path(tmp);(d/'qemu').mkdir();(d/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n#include <stdlib.h>\n');(d/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 c=d/'test.c';exe=d/('test.exe' if os.name=='nt' else 'test')
 def run(text):
  c.write_text(template.replace('/* PRODUCTION */',text))
  subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(d),'-I'+str(root),str(c),'-o',str(exe)],env=env,check=True)
  return subprocess.run([str(exe)],env=env,capture_output=True,timeout=30)
 result=run(production);assert result.returncode==0,(result.returncode,result.stdout,result.stderr)
 assert b'Prepared vertex initialization declined: prior GL error 0x501' in result.stderr
 print(result.stdout.decode(),end='',flush=True)
 mutations=[
  ('multiple own error flags retained','for (unsigned i = 0; i < 8; i++) {','for (unsigned i = 0; i < 1; i++) {'),
  ('zero program queried','if (!stage->gl_stage_program) goto unavailable;','(void)stage->gl_stage_program;'),
  ('diagnostic storage not initialized','char log[2048] = {0};',"char log[2048] = {'x'};"),
  ('zero buffer admitted','if (!r->ready_vsh.code_buffer) goto unavailable;','(void)r->ready_vsh.code_buffer;'),
  ('wrong storage size admitted','storage_size != VSH_INTERPRETER_BUFFER_SIZE','storage_size < VSH_INTERPRETER_BUFFER_SIZE'),
  ('own GL error ignored','if (error != GL_NO_ERROR || storage_size != VSH_INTERPRETER_BUFFER_SIZE)','if (storage_size != VSH_INTERPRETER_BUFFER_SIZE)'),
  ('prior error silently drained','if (error != GL_NO_ERROR) {','if (false) {'),
  ('caller binding not restored','glBindBuffer(GL_UNIFORM_BUFFER, previous);','(void)previous;'),
  ('buffer owner leaked','glDeleteBuffers(1, &r->ready_vsh.code_buffer);','(void)glDeleteBuffers;'),
  ('stage owner leaked','delete_program(r, stage->gl_stage_program);','(void)delete_program;'),
  ('link failure admitted','if (!linked) {','if (false) {'),
  ('missing code block admitted','if (block == GL_INVALID_INDEX) {','if (false) {'),
  ('early ready publication','glGenBuffers(1, &r->ready_vsh.code_buffer);','r->ready_vsh.ready=true;glGenBuffers(1, &r->ready_vsh.code_buffer);'),
 ]
 for name,before,after in mutations:
  assert before in production,name;result=run(production.replace(before,after,1))
  assert result.returncode==86 and b'VSH_OWNER_FAIL' in result.stderr,(name,result.returncode,result.stdout,result.stderr)
  print('Required VSH startup fault rejected:',name,flush=True)
