/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <zlib.h>
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#include "hw/xbox/nv2a/pgraph/spv/vsh.h"
#include "hw/xbox/nv2a/pgraph/spv/psh.h"
#include "hw/xbox/nv2a/pgraph/spv/geom.h"
#include <assert.h>
#define VK_SHADER_STAGE_GEOMETRY_BIT 8
static bool pgraph_vk_spirv_1_6;
typedef struct { unsigned kind;struct { GeomState state;GenGeomGlslOptions glsl_opts; } geom; } ShaderModuleCacheKey;
/* SELECTORS */
#define CHECK(x,message) do { if(!(x)){fprintf(stderr,"seed dictionary: %s\n",message);return false;} } while(0)
static uint32_t u32(const uint8_t *p){uint32_t n;memcpy(&n,p,sizeof(n));return n;}
typedef struct Counts {size_t games,states,geometry,migrated,without_vk,peak_decoded;} Counts;
static bool inspect(uint8_t *raw,size_t length,bool migrate,Counts *counts)
{
 CHECK(length>=16,"seed header truncated");
 CHECK(u32(raw)==0x44455358 && u32(raw+4)==1,"seed header incompatible");
 size_t offset=16,states=0;
 for(unsigned section=0;section<u32(raw+8);section++){
  CHECK(length-offset>=16,"section header truncated");
  uint32_t kind=u32(raw+offset),size=u32(raw+offset+4),n=u32(raw+offset+8);offset+=16;
  CHECK(n<=65536 && (!n || (size && n<=(length-offset)/size)),"section payload truncated");
  if(kind==3){
   CHECK(size==sizeof(ShaderState),"Vulkan state ABI mismatch");
   CHECK(offset%_Alignof(ShaderState)==0,"Vulkan state alignment");
   for(unsigned i=0;i<n;i++){
    ShaderState *s=(void*)(raw+offset+(size_t)i*size);
    unsigned old[4];for(unsigned j=0;j<4;j++)old[j]=s->psh.conv_tex[j];
    /* Final decoded owner remains live through every synchronous consumer. */
    if(migrate)pgraph_glsl_psh_migrate_seed(&s->psh);
    bool changed=false;for(unsigned j=0;j<4;j++)changed|=old[j]!=(unsigned)s->psh.conv_tex[j];
    counts->migrated+=changed;
    CHECK(pgraph_spv_vsh_supported(&s->vsh,(SpvVshOptions){.preserve_float32=true,.ubo_binding=0}),"vertex eligibility");
    CHECK(pgraph_spv_psh_supported(&s->psh,(SpvPshOptions){.ubo_binding=2,.tex_binding=3}),"fragment eligibility");
    if(pgraph_glsl_need_geom(&s->geom)){
     ShaderModuleCacheKey key={.kind=VK_SHADER_STAGE_GEOMETRY_BIT,.geom={.state=s->geom,.glsl_opts={.vulkan=true,.ui_clamp_stage=pgraph_glsl_ui_clamp_stage(&s->psh)}}};
     SpvLineGeomOptions line;SpvTriangleGeomOptions triangle;SpvQuadGeomOptions quad;
     CHECK(vk_module_line_options(&key,&line)||vk_module_triangle_options(&key,&triangle)||vk_module_quad_options(&key,&quad),"geometry eligibility");
     counts->geometry++;
    }
    states++;
   }
  }
  offset+=(size_t)n*size;
 }
 CHECK(offset==length,"seed trailing bytes");
 counts->states+=states;counts->without_vk+=!states;return true;
}
static bool dictionary(const uint8_t *map,size_t length,bool migrate,Counts *counts)
{
 CHECK(length>=12,"dictionary header truncated");
 CHECK(u32(map)==0x44445358 && u32(map+4)==1,"dictionary header incompatible");
 unsigned games=u32(map+8);CHECK(games<=(length-12)/40,"dictionary entries truncated");
 size_t offset=12+(size_t)games*40;
 for(unsigned i=0;i<games;i++){
  const uint8_t *entry=map+12+(size_t)i*40;
  size_t raw_size=u32(entry+32),compressed=u32(entry+36);
  CHECK(raw_size && raw_size<=(64u<<20),"decoded size bound");
  CHECK(compressed<=length-offset,"compressed payload truncated");
  uint8_t *raw=malloc(raw_size);CHECK(raw,"decoded owner allocation");
  uLongf produced=raw_size;
  int result=uncompress(raw,&produced,map+offset,compressed);
  bool ok=result==Z_OK && produced==raw_size;
  if(ok)ok=inspect(raw,raw_size,migrate,counts);
  else fprintf(stderr,"seed dictionary: decompression size mismatch\n");
  free(raw); /* No lend survives the last predicate/selector above. */
  if(!ok)return false;
  if(raw_size>counts->peak_decoded)counts->peak_decoded=raw_size;
  counts->games++;offset+=compressed;
 }
 CHECK(offset==length,"dictionary trailing bytes");
 CHECK(counts->states>=9334,"Vulkan fixture coverage decreased or absent");
 return true;
}
int main(int argc,char **argv)
{
 if(argc<2||argc>3)return 2;
 int fd=open(argv[1],O_RDONLY);if(fd<0){perror("seed dictionary open");return 1;}
 struct stat st;if(fstat(fd,&st)||st.st_size<12){close(fd);return 1;}
 void *map=mmap(NULL,st.st_size,PROT_READ,MAP_PRIVATE,fd,0);
 if(map==MAP_FAILED){close(fd);return 1;}
 Counts counts={0};bool ok=dictionary(map,st.st_size,argc==2,&counts);
 munmap(map,st.st_size);close(fd);
 if(ok)printf("Bundled Vulkan seed eligibility: %zu games, %zu states, %zu geometry, %zu migrated records; %zu games without Vulkan records; peak decoded owner=%zu bytes PASS (eligibility only, not PSO readiness)\n",counts.games,counts.states,counts.geometry,counts.migrated,counts.without_vk,counts.peak_decoded);
 return ok?0:1;
}
