# SPDX-License-Identifier: GPL-2.0-or-later
"""Production GL report ownership with deterministic or real-GPU query results."""
from pathlib import Path
import argparse,os,shlex,shutil,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('checkout',type=Path);p.add_argument('cc',nargs='?',default='cc');p.add_argument('--gpu',action='store_true');a=p.parse_args();r=a.checkout.resolve()
h=(r/'hw/xbox/nv2a/pgraph/gl/renderer.h').read_text(encoding='utf-8');lo=h.index('typedef struct QueryReport {');hi=h.index('} QueryReport;',lo)+len('} QueryReport;')
s=(r/'hw/xbox/nv2a/pgraph/gl/reports.c').read_text(encoding='utf-8');s=s[s.index('/* The active interval'):]
pool=(r/'hw/xbox/nv2a/pgraph/pool.h').read_text(encoding='utf-8');pool=pool[pool.index('typedef struct ObjPool {'):pool.index('typedef struct Scratch {')]
draw=(r/'hw/xbox/nv2a/pgraph/gl/draw.c').read_text(encoding='utf-8');assert 'pgraph_gl_alloc_zpass_query(d)' in draw;assert 'gl_zpass_pixel_count_queries' not in draw
reuse='bool pooled;' in h[lo:hi]
if os.environ.get('GL_REPORT_REQUIRE_REUSE'):
 assert reuse,'Required pooled report query reuse implementation is missing'
driver=Path(__file__).with_name('test-gl-report-storage-gpu.c').read_text() if a.gpu else Path(__file__).with_suffix('.c').read_text()
if a.gpu:
 def function(source,name):
  start=source.index(name);i=source.index('{',start)+1;depth=1
  while depth:depth+=(source[i]=='{')-(source[i]=='}');i+=1
  return source[start:i]
 geometry=(r/'libretro/tests/test-geom-dynamic-gl.c').read_text()
 context=function(geometry,'static void quiet_abort(')+'\n'+function(geometry,'static void init_gl(')
 driver=driver.replace('/* CONTEXT */',context.replace('Geometry GL device=','Report GL device='))
variants=[('positive',s)]
if reuse and not os.environ.get('GL_REPORT_STORAGE_SKIP_RUN'):
 take='QueryReport *report = pool->nfree ? pool->free[--pool->nfree] :\n                                     obj_pool_take(pool);'
 assert s.count(take)==1
 alias='    if (!query) {\n'
 assert s.count(alias)==1
 retire='glDeleteQueries(count, slot->inline_queries);'
 assert s.count(retire)==1
 variants += [('lost cache',s.replace(take,'QueryReport *report = obj_pool_take(pool);')),
              ('aliased query',s.replace(alias,'    if (index) query = report->inline_queries[0];\n'+alias)),
              ('cached leak',s.replace(retire,'glDeleteQueries(0, slot->inline_queries);'))]
with tempfile.TemporaryDirectory(prefix='gl-report-storage-') as tmp:
 d=Path(tmp)
 default_flags='-fsanitize=undefined' if a.gpu else '-fsanitize=address,undefined'
 flags=shlex.split(os.environ.get('GL_REPORT_STORAGE_CFLAGS',default_flags if os.name!='nt' else '-g'))
 env=os.environ.copy();env['PATH']=str(Path(a.cc).resolve().parent)+os.pathsep+env.get('PATH','')
 if a.gpu:env.update(EGL_PLATFORM='surfaceless',LP_NUM_THREADS='1')
 for name,source in variants:
  code=driver.replace('/* REPORT TYPE */',h[lo:hi]).replace('/* POOL */',pool).replace('/* PRODUCTION */',source)
  c=d/(name.replace(' ','-')+'.c');c.write_text(code);exe=c.with_suffix('.exe' if os.name=='nt' or 'mingw' in a.cc else '')
  sources=[c];libs=[]
  if a.gpu:
   if os.name=='nt':
    sources += [r/f for f in ['ui/gl-dispatch.c','libretro-common/glsym/rglgen.c','libretro-common/glsym/glsym_gl.c','hw/xbox/nv2a/pgraph/thirdparty/gloffscreen/gloffscreen_libretro.c','libretro/glib/gcore.c','libretro/glib/gcontainers.c']]
    sources += [r/'libretro-common'/f for f in ['compat/compat_strl.c','encodings/encoding_utf.c','encodings/encoding_base64.c','hash/lrc_hash.c','features/features_cpu.c','time/rtime.c','compat/fopen_utf8.c','string/rstrtod.c','streams/file_stream.c','vfs/vfs_implementation.c','file/file_path.c','file/file_path_io.c','compat/compat_posix_string.c','string/stdstring.c']]
    libs=['-lopengl32','-lgdi32','-luser32']
   else:libs=['-lEGL','-lGL']
  subprocess.run([a.cc,'-O2','-std=gnu11','-ffunction-sections','-fdata-sections','-Wall','-Wextra',*([] if a.gpu else ['-Werror']),'-Wno-unused-parameter','-Wno-cast-function-type','-Wno-sign-compare','-Wno-misleading-indentation','-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'libretro/glib'),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'hw/xbox/nv2a/pgraph/thirdparty/gloffscreen'),*(['-DGL_REPORT_REUSE'] if reuse else []),*flags,*map(str,sources),'-Wl,--gc-sections',*libs,'-o',str(exe)],check=True,env=env)
  if name=='positive' and os.environ.get('GL_REPORT_STORAGE_BINARY'):shutil.copy2(exe,os.environ['GL_REPORT_STORAGE_BINARY'])
  if not os.environ.get('GL_REPORT_STORAGE_SKIP_RUN'):
   result=subprocess.run([str(exe)],env=env,capture_output=True,text=True,timeout=30)
   if name=='positive':
    print(result.stdout,end='',flush=True)
    assert result.returncode==0,(result.returncode,result.stdout,result.stderr)
   else:
    assert result.returncode!=0 and ('Assertion' in result.stderr or 'AddressSanitizer' in result.stderr),(name,result.returncode,result.stderr)
    print('Required GL report control rejected:',name,flush=True)
