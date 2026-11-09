#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production pipeline-job linking and publication ownership."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
job=re.search(r'typedef struct \{\n    bool frag;.*?\} LibJob;',s,re.S).group()
parts=[]
for name in ['pipeline_job_reset','lib_job_link','pump_slot','pump_submit']:
    m=re.search(r'^static (?:void|LibJob \*) *'+name+r'\(.*?^\}',s,re.M|re.S);assert m,name
    parts.append(m.group())
key_types='\n'.join(re.search(r'typedef struct '+n+r' \{.*?\} '+n+r';',s,re.S)[0] for n in ('GplPreKey','GplFragKey'))
code=r'''
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vulkan/vulkan.h>
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); _Exit(1); } } while(0)
#define NV2A_VERTEXSHADER_ATTRIBUTES 16
#define PUMP_RING 64
typedef struct { int unused; } ShaderModuleInfo;
typedef struct {VkFormat color_format,zeta_format;} RenderPassState;
/* LIBRARY KEYS */
typedef struct {
    unsigned tag;
    VkVertexInputBindingDescription binding_descriptions[NV2A_VERTEXSHADER_ATTRIBUTES];
    VkVertexInputAttributeDescription attribute_descriptions[NV2A_VERTEXSHADER_ATTRIBUTES];
} PipelineKey;
/* JOB */
static LibJob pump_jobs[PUMP_RING];
static unsigned pump_w_local, pump_d, starts, wakes;
static int pump_w, pipe_ec_work;
static void pipe_start(void) {starts++;}
static void retro_atomic_store_release_int(int *p,int value) {
    assert(p==&pump_w && value==(int)pump_w_local);
    LibJob *j=&pump_jobs[(pump_w_local-1)%PUMP_RING];
    if(j->full) {
        assert(j->vertex_key==&j->key);
        assert(j->vi.pVertexBindingDescriptions==(j->vi.vertexBindingDescriptionCount?j->key.binding_descriptions:NULL));
        assert(j->vi.pVertexAttributeDescriptions==(j->vi.vertexAttributeDescriptionCount?j->key.attribute_descriptions:NULL));
    }
    *p=value;
}
static void retro_eventcount_notify(int *p) {assert(p==&pipe_ec_work && pump_w==(int)pump_w_local);wakes++;}
/* PRODUCTION */
static void fill_key(PipelineKey *key,unsigned serial) {
    memset(key,0,sizeof(*key));key->tag=serial;
    for(unsigned i=0;i<NV2A_VERTEXSHADER_ATTRIBUTES;i++) {
        key->binding_descriptions[i]=(VkVertexInputBindingDescription){i,serial+i,VK_VERTEX_INPUT_RATE_VERTEX};
        key->attribute_descriptions[i]=(VkVertexInputAttributeDescription){i,i,VK_FORMAT_R32G32B32A32_SFLOAT,serial+i};
    }
}
int main(void) {
    unsigned cases=0;
    for(unsigned generation=0;generation<128;generation++)
    for(unsigned nb=0;nb<=16;nb++)for(unsigned na=0;na<=16;na++) {
        PipelineKey caller;fill_key(&caller,generation+1);
        LibJob local;memset(&local,0xa5,sizeof(local));
        pipeline_job_reset(&local);local.full=true;local.vertex_key=&caller;
        local.vi.vertexBindingDescriptionCount=nb;
        local.vi.vertexAttributeDescriptionCount=na;
        local.ds.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        local.cb.attachmentCount=1;
        lib_job_link(&local);
        assert(local.vi.pVertexBindingDescriptions==(nb?caller.binding_descriptions:NULL));
        assert(local.vi.pVertexAttributeDescriptions==(na?caller.attribute_descriptions:NULL));
        assert(local.ci.pStages==local.stages && local.ci.pVertexInputState==&local.vi);
        assert(local.ci.pColorBlendState==&local.cb && local.cb.pAttachments==&local.cba);
        /* Simulate a full queued job: the key is copied once, then all
         * driver pointers must be rebound before release publication. */
        pump_d=pump_w_local;
        PipelineKey retained;memcpy(&retained,&pump_jobs[pump_w_local%PUMP_RING].key,sizeof(retained));
        LibJob *queued=pump_slot();assert(queued);
        assert(!memcmp(&retained,&queued->key,sizeof(retained)));
        const unsigned char *clear=(const unsigned char *)queued;
        for(size_t i=0;i<sizeof(*queued);i++)
            if(i<offsetof(LibJob,key)||i>=offsetof(LibJob,key)+sizeof(queued->key)) assert(!clear[i]);
        *queued=local;queued->key=caller;pump_submit(queued);
        memset(&caller,0xa5,sizeof(caller));memset(&local,0xa5,sizeof(local));
        for(unsigned i=0;i<nb;i++) assert(queued->vi.pVertexBindingDescriptions[i].stride==generation+1+i);
        for(unsigned i=0;i<na;i++) assert(queued->vi.pVertexAttributeDescriptions[i].offset==generation+1+i);
        assert(queued->ci.pStages==queued->stages && queued->ci.pVertexInputState==&queued->vi);
        assert(queued->cb.pAttachments==&queued->cba);
        memset(queued,0xa5,sizeof(*queued));
        cases++;
    }
    pump_d=pump_w_local-PUMP_RING;assert(!pump_slot());
    for(unsigned frag=0;frag<2;frag++) {
        pump_d=pump_w_local;
        LibJob *library=pump_slot();assert(library);
        memset(&library->key,0xdc,sizeof(library->key));
        library->frag=frag;lib_job_link(library);
        assert(!library->full && !library->vertex_key && !library->vi.pVertexBindingDescriptions && !library->vi.pVertexAttributeDescriptions);
        assert(library->ci.pNext==&library->lib_info);
        assert(library->ci.pVertexInputState==(frag?NULL:&library->vi));
    }
    assert(starts==cases && wakes==cases);
    printf("Pipeline job vertex views: %u synchronous/queued/count/slot-reuse/publication cases and both library halves PASS\n",cases);
}
'''.replace('/* LIBRARY KEYS */',key_types).replace('/* JOB */',job).replace('/* PRODUCTION */','\n'.join(parts))
with tempfile.TemporaryDirectory(prefix='pipeline-job-view-') as tmp:
    d=Path(tmp);c=d/'test.c';c.write_text(code);exe=d/('test.exe' if os.name=='nt' else 'test')
    env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
    subprocess.run([cc,'-std=gnu11','-O2','-Wall','-Wextra','-Werror','-I'+str(r/'deps/vulkan-headers'),*shlex.split(os.environ.get('PIPELINE_JOB_CFLAGS','')),str(c),'-o',str(exe)],check=True,env=env)
    subprocess.run([str(exe)],check=True,env=env,timeout=30)
