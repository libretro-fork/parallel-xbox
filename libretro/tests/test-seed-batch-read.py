# SPDX-License-Identifier: GPL-2.0-or-later
"""Real libretro-common inflate, production seed loaders, poisoned borrowed lifetimes."""
from pathlib import Path
import argparse,os,re,shlex,shutil,struct,subprocess,tempfile,zlib
p=argparse.ArgumentParser();p.add_argument('checkout',type=Path);p.add_argument('cc',nargs='?',default='cc');a=p.parse_args();r=a.checkout.resolve()
s=(r/'hw/xbox/nv2a/pgraph/seed.c').read_text(encoding='utf-8');h=(r/'hw/xbox/nv2a/pgraph/seed.h').read_text(encoding='utf-8')
types=[]
for name in ['SeedHeader','SeedSection','SeedDictHeader','SeedDictEntry']:types.append(re.search(r'typedef struct '+name+r' \{.*?\} '+name+r';',s,re.S).group())
types.append(re.search(r'struct SeedFind \{.*?\};',s,re.S).group());types.append(re.search(r'typedef struct PGRAPHSeedRead \{.*?\} PGRAPHSeedRead;',h,re.S).group())
parts=[]
for name in ['seed_walk','seed_find_visit','seed_find_view','seed_find','pgraph_seed_read','seed_dict_open','pgraph_seed_dict_read','seed_read_batch','pgraph_seed_read_combined','pgraph_seed_read_first']:
 m=re.search(r'^(?:static )?[^\n]*\b'+name+r'\(.*?^\}',s,re.M|re.S);assert m,name;parts.append(m.group())
def seed(items):
 out=struct.pack('<4I',0x44455358,1,len(items),0)
 for section,size,values in items:out+=struct.pack('<4I',section,size,len(values),0)+b''.join(v.to_bytes(size,'little') for v in values)
 return out
local=seed([(1,4,[11,12]),(2,8,[111])]);embedded=seed([(1,4,[21,22,23]),(2,8,[222,333])])
fixtures='\n'.join('static const uint8_t '+name+'[]={'+','.join(map(str,data))+'};' for name,data in [('local_raw',local),('embedded_raw',embedded),('embedded_z',zlib.compress(embedded))])
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */','\n'.join(types)).replace('/* FIXTURES */',fixtures).replace('/* PRODUCTION */','\n'.join(parts))
with tempfile.TemporaryDirectory(prefix='seed-batch-') as tmp:
 d=Path(tmp);(d/'test.c').write_text(code);exe=d/('test.exe' if os.name=='nt' or 'mingw' in a.cc else 'test')
 flags=shlex.split(os.environ.get('SEED_BATCH_CFLAGS','-fsanitize=address,undefined' if os.name!='nt' else '-g'))
 env=os.environ.copy();env['PATH']=str(Path(a.cc).resolve().parent)+os.pathsep+env.get('PATH','')
 subprocess.run([a.cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-I'+str(r/'include'),'-I'+str(r/'libretro-common/include'),*flags,str(d/'test.c'),str(r/'libretro-common/encodings/encoding_deflate.c'),str(r/'libretro-common/encodings/encoding_crc32.c'),'-o',str(exe)],check=True,env=env)
 if os.environ.get('SEED_BATCH_BINARY'):shutil.copy2(exe,os.environ['SEED_BATCH_BINARY'])
 if not os.environ.get('SEED_BATCH_SKIP_RUN'):subprocess.run([str(exe),str(r/'data/shader_seeds.bin')],check=True,env=env)
