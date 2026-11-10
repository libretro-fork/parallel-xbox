# SPDX-License-Identifier: GPL-2.0-or-later
"""Production integer-array eligibility and direct-load byte/error oracle."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'migration/vmstate.c').read_text();parts=[]
for name in ['vmstate_scalar_array','vmstate_load_scalar_array']:
 m=re.search(r'^static (?:bool|int) '+name+r'\(',s,re.M);assert m
 a=s.index('{',m.start());b=a+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 parts.append(s[m.start():b])
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef struct {int unused;} VMStateInfo;
static const VMStateInfo vmstate_info_uint8={1},vmstate_info_int8={2},vmstate_info_uint16={3},vmstate_info_int16={4},vmstate_info_uint32={5},vmstate_info_int32={6},vmstate_info_uint64={7},vmstate_info_int64={8},custom={9};
typedef struct {const VMStateInfo *info;unsigned flags;} VMStateField;
enum {VMS_STRUCT=1,VMS_VSTRUCT=2,VMS_ARRAY_OF_POINTER=4};
typedef struct {const uint8_t *data;size_t available,pos;int error;unsigned calls;} QEMUFile;
static size_t qemu_get_buffer(QEMUFile*f,uint8_t *dst,size_t size){
 size_t n=f->available<size?f->available:size;memcpy(dst,f->data,n);f->pos+=n;f->calls++;return n;
}
static int qemu_file_get_error(QEMUFile*f){return f->error;}
static uint64_t read_be(const void *p,unsigned n){const uint8_t*b=p;uint64_t v=0;while(n--)v=(v<<8)|*b++;return v;}
static uint16_t lduw_be_p(const void*p){return read_be(p,2);}
static uint32_t ldl_be_p(const void*p){return read_be(p,4);}
static uint64_t ldq_be_p(const void*p){return read_be(p,8);}
static void stw_he_p(void*p,uint16_t v){memcpy(p,&v,2);}
static void stl_he_p(void*p,uint32_t v){memcpy(p,&v,4);}
static void stq_he_p(void*p,uint64_t v){memcpy(p,&v,8);}
/* PRODUCTION */
int main(void){
 const VMStateInfo *infos[]={&vmstate_info_uint8,&vmstate_info_int8,&vmstate_info_uint16,&vmstate_info_int16,&vmstate_info_uint32,&vmstate_info_int32,&vmstate_info_uint64,&vmstate_info_int64,&custom};
 const int sizes[]={0,1,2,3,4,8,16},counts[]={-1,0,1,15,16,17,31,256,4096};
 unsigned eligibility=0,loads=0;
 for(unsigned info=0;info<9;info++)for(unsigned flags=0;flags<32;flags++)
 for(unsigned k=0;k<7;k++)for(unsigned j=0;j<9;j++){
  VMStateField field={infos[info],flags};
  bool expected=info<8&&counts[j]>=16&&!(flags&7)&&sizes[k]==(1<<(info/2));
  assert(vmstate_scalar_array(&field,counts[j],sizes[k])==expected);eligibility++;
 }
 uint8_t wire[32768],storage[32784],expected[32784];
 const int errors[]={0,-EIO,-ENOSPC};
 for(unsigned size=1;size<=8;size*=2)for(unsigned j=0;j<9;j++){
  int n=counts[j];if(n<16)continue;
  size_t bytes=(size_t)n*size;
  for(unsigned i=0;i<bytes;i++)wire[i]=(uint8_t)(i*31+j*13);
  for(unsigned offset=0;offset<8;offset++)for(unsigned cut=0;cut<4;cut++)
  for(unsigned e=0;e<3;e++){
   size_t available=cut==0?0:cut==1?bytes/2:cut==2?bytes-1:bytes;
   memset(storage,0xa5,sizeof(storage));memset(expected,0xa5,sizeof(expected));
   QEMUFile f={.data=wire,.available=available,.error=errors[e]};
   int ret=vmstate_load_scalar_array(&f,storage+offset,n,size);
   assert(ret==(available<bytes?(errors[e]?errors[e]:-EIO):errors[e]));
   assert(f.pos==available&&f.calls==1);
   memcpy(expected+offset,wire,available);
   if(available==bytes){
    for(int i=0;i<n;i++){
     /* Independent scalar oracle builds the numeric value and stores
      * its host representation, including signed bit patterns. */
     uint64_t v=0;for(unsigned b=0;b<size;b++)v=v*256+wire[(size_t)i*size+b];
     switch(size){
     case 1:expected[offset+i]=(uint8_t)v;break;
     case 2:{uint16_t x=v;memcpy(expected+offset+(size_t)i*size,&x,2);break;}
     case 4:{uint32_t x=v;memcpy(expected+offset+(size_t)i*size,&x,4);break;}
     case 8:memcpy(expected+offset+(size_t)i*size,&v,8);break;
     }
    }
   }
   assert(!memcmp(storage,expected,sizeof(storage)));loads++;
  }
 }
 printf("PASS: %u field eligibility and %u direct-load cases; endian payloads, unaligned owners, truncation/errors, one read and canaries\n",eligibility,loads);
}
'''.replace('/* PRODUCTION */','\n'.join(parts))
if os.name!='nt':
 import resource
 resource.setrlimit(resource.RLIMIT_CORE,(0,0))
with tempfile.TemporaryDirectory(prefix='vmstate-array-') as d:
 p=Path(d);src=p/'test.c';exe=os.environ.get('VM_ARRAY_BINARY',str(p/'test'))
 flags=shlex.split(os.environ.get('VM_ARRAY_CFLAGS','-fsanitize=address,undefined'))
 args=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror']
 def build(text):
  src.write_text(text);subprocess.run(args+flags+[str(src),'-o',exe],check=True)
 build(code)
 if not os.environ.get('VM_ARRAY_SKIP_RUN'):
  runner=shlex.split(os.environ.get('VM_ARRAY_RUNNER',''))
  subprocess.run(runner+[exe],check=True,timeout=60)
  for label,old,new in [
   ('custom fields bypassed','info == &vmstate_info_uint8 || info == &vmstate_info_int8','true'),
   ('truncated reads accepted','return qemu_file_get_error(f) ?: -EIO;','return 0;'),
   ('byte order discarded','stw_he_p(elem, lduw_be_p(elem));','stw_he_p(elem, __builtin_bswap16(lduw_be_p(elem)));')]:
   assert old in code,label
   build(code.replace(old,new))
   result=subprocess.run(runner+[exe],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
   assert result.returncode and b'Assertion' in result.stdout,(label,result.stdout)
   print('Required negative control rejected:',label,flush=True)
