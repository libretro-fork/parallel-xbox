# SPDX-License-Identifier: GPL-2.0-or-later
"""Production texture retirement keeps auxiliary uploads alive independently of draws."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve()
def function(path,name):
 s=(r/path).read_text(encoding='utf-8');m=re.search(r'(?:static )?void '+name+r'\([^;]*?\)\s*\{',s);assert m,name
 end=s.index('{',m.start())+1;depth=1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
parts=[function('hw/xbox/nv2a/pgraph/vk/texture.c','texture_cache_release_node_resources')]
parts += [function('hw/xbox/nv2a/pgraph/vk/draw.c',n) for n in ('pgraph_vk_trash_push','trash_drain_retired')]
code=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#define VK_NULL_HANDLE 0
#define g_renew(t,p,n) ((t *)realloc((p),(n)*sizeof(t)))
typedef struct {unsigned dummy;} ImageInfo;
struct XemuVkTrashEntry {
 uint64_t pipeline,layout,sampler,framebuffer,image_view,image,allocation;
 ImageInfo image_ci;bool pool_ok,pool_view;uint32_t retire_after;uint64_t display_after,aux_after;
};
typedef struct {
 uint64_t device,allocator;struct XemuVkTrashEntry *trash;int trash_n,trash_cap;
 bool in_command_buffer;uint32_t submit_count,settled_submit;
 struct {uint64_t submitted,settled;} display;uint64_t aux_submitted,aux_settled;
} PGRAPHVkState;
typedef struct {unsigned cube_source_mask;bool depth_source_copy,s2t_borrowed;uint64_t allocation,sampler,image_view,image;ImageInfo image_ci;} TextureBinding;
static unsigned images,views,samplers;
#define vkDestroyPipeline(d,h,a) ((void)(d),(void)(h),(void)(a))
#define vkDestroyPipelineLayout(d,h,a) ((void)(d),(void)(h),(void)(a))
#define vkDestroyFramebuffer(d,h,a) ((void)(d),(void)(h),(void)(a))
#define vkDestroySampler(d,h,a) ((void)(d),(void)(a),samplers+=(!!(h)))
#define vkDestroyImageView(d,h,a) ((void)(d),(void)(a),views+=(!!(h)))
static void xemu_vk_destroy_image(uint64_t a,uint64_t i,uint64_t mem){(void)a;(void)i;(void)mem;images++;}
static bool pgraph_vk_image_pool_put(PGRAPHVkState *r,uint64_t i,uint64_t m,const ImageInfo *c){(void)r;(void)i;(void)m;(void)c;return false;}
static bool pgraph_vk_image_pool_put_with_view(PGRAPHVkState *r,uint64_t i,uint64_t m,const ImageInfo *c,uint64_t v){(void)v;return pgraph_vk_image_pool_put(r,i,m,c);}
static void pgraph_vk_poll_display_retired(PGRAPHVkState *r){(void)r;}
static void pgraph_vk_poll_aux_retired(PGRAPHVkState *r){(void)r;}
void pgraph_vk_trash_push(PGRAPHVkState *,const struct XemuVkTrashEntry *);
/* PRODUCTION */
int main(void)
{
 const unsigned order[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
 unsigned cases=0;
 for(unsigned wrap=0;wrap<2;wrap++)for(unsigned open=0;open<2;open++)for(unsigned borrow=0;borrow<2;borrow++)for(unsigned permutation=0;permutation<6;permutation++) {
  PGRAPHVkState r={.submit_count=wrap?UINT32_MAX-1:100,.in_command_buffer=open,.aux_submitted=29,.display={.submitted=13}};
  r.settled_submit=r.submit_count-1;
  TextureBinding b={.cube_source_mask=63,.depth_source_copy=true,.s2t_borrowed=borrow,.allocation=borrow?0:44,.sampler=12,.image_view=13,.image=14};
  images=views=samplers=0;texture_cache_release_node_resources(&r,&b);
  assert(!b.image&&!b.image_view&&!b.sampler&&!b.allocation&&!b.s2t_borrowed&&!b.cube_source_mask&&!b.depth_source_copy);
  assert(r.trash_n==1&&r.trash[0].aux_after==29);
  trash_drain_retired(&r);assert(r.trash_n==1&&!images&&!views&&!samplers);
  for(unsigned step=0;step<3;step++) {
   switch(order[permutation][step]){case 0:r.settled_submit=r.submit_count+open;break;case 1:r.display.settled=13;break;case 2:r.aux_settled=29;break;}
   trash_drain_retired(&r);
   if(step<2)assert(r.trash_n==1&&!images&&!views&&!samplers);
   else assert(!r.trash_n&&views==1&&samplers==1&&images==(borrow?0u:1u));
  }
  free(r.trash);cases++;
 }
 printf("Texture retirement: %u independent draw/display/aux schedules, borrowed ownership and wrap-near counters PASS\n",cases);
}
'''.replace('/* PRODUCTION */','\n'.join(parts))
with tempfile.TemporaryDirectory(prefix='texture-aux-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code)
 cc=sys.argv[2] if len(sys.argv)>2 else 'cc';exe=p/('test.exe' if os.name=='nt' else 'test')
 env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
 subprocess.run([cc,'-std=gnu11','-O2','-Wall','-Wextra','-Werror',*shlex.split(os.environ.get('TEXTURE_AUX_CFLAGS','-g' if os.name=='nt' else '-fsanitize=address,undefined')),str(p/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env)
