/* SPDX-License-Identifier: GPL-2.0-or-later */
static void triangle_selector_tests(void)
{
 unsigned count=0;
 for(unsigned primitive=0;primitive<=PRIM_TYPE_POLYGON;primitive++)
 for(unsigned front=0;front<3;front++)for(unsigned back=0;back<3;back++)
 for(unsigned smooth=0;smooth<2;smooth++)for(unsigned first=0;first<2;first++)
 for(unsigned wide=0;wide<2;wide++)for(unsigned vk=0;vk<2;vk++)
 for(unsigned stage=0;stage<4;stage++)for(unsigned z=0;z<2;z++){
  unsigned stages[]={1,8,16,32};
  ShaderModuleCacheKey key={.kind=stages[stage],.geom={.state={.primitive_mode=primitive,.polygon_front_mode=front,.polygon_back_mode=back,.smooth_shading=smooth,.first_vertex_is_provoking=first,.wide_lines=wide,.z_perspective=z,.tri_rot0=1,.tri_rot1=2},.glsl_opts={.vulkan=vk}}};
  bool expected=stages[stage]==8&&vk&&!wide&&front==POLY_MODE_FILL&&back==POLY_MODE_FILL&&((primitive>=PRIM_TYPE_TRIANGLES&&primitive<=PRIM_TYPE_TRIANGLE_FAN)||primitive==PRIM_TYPE_POLYGON);
  pgraph_vk_spirv_1_6=true;SpvTriangleGeomOptions o={0};assert(vk_module_triangle_options(&key,&o)==expected);
  if(expected){unsigned even=primitive==PRIM_TYPE_POLYGON?2:first?0:primitive==PRIM_TYPE_TRIANGLE_FAN?1:2;assert(o.smooth_shading==smooth&&o.z_perspective==z&&o.spirv_1_6&&o.rotation_even==1&&o.rotation_odd==2);assert(o.provoking_even==even&&o.provoking_odd==((primitive==PRIM_TYPE_TRIANGLE_STRIP&&!first)?1:even));}count++;
 }
 for(int rotation=-1;rotation<5;rotation++){
  ShaderModuleCacheKey key={.kind=8,.geom={.state={.primitive_mode=PRIM_TYPE_TRIANGLES,.polygon_front_mode=POLY_MODE_FILL,.polygon_back_mode=POLY_MODE_FILL,.tri_rot0=rotation,.tri_rot1=rotation},.glsl_opts={.vulkan=true}}};SpvTriangleGeomOptions o;assert(vk_module_triangle_options(&key,&o));assert(o.rotation_even==(rotation==0?0u:rotation==1?1u:2u)&&o.rotation_odd==o.rotation_even);
 }
 SpvDirectModule invalid;allocations=0;assert(!pgraph_spv_triangle_geom((SpvTriangleGeomOptions){.rotation_even=3},&invalid));assert(!allocations&&!invalid.words&&!invalid.num_words);
 assert(!pgraph_spv_triangle_geom((SpvTriangleGeomOptions){.provoking_odd=3},&invalid));
 printf("Triangle selector: %u cases passed\n",count);
}
#ifdef TEST_VULKAN
static float cpu_det(float a,float b,float c,float d){volatile float cd=c*d;float err=fmaf(-c,d,cd);return fmaf(a,b,-cd)+err;}
static void triangle_data(void){
 if(!gd)init_gpu();geometry_vertices=geometry_outputs=3;
 for(unsigned i=0;i<GEOM_CASES*3;i++){
  uint32_t h=(i+1)*747796405u+2891336453u;
  for(unsigned c=0;c<4;c++){h=h*1664525u+1013904223u;vertices[i].attr[9][c]=(float)((int)(h>>24)-128)*0.0625f;}
 }
 memcpy(vertices[1].attr[9],vertices[0].attr[9],16);memcpy(vertices[2].attr[9],vertices[0].attr[9],16);
 vertices[3].attr[9][0]=INFINITY;vertices[4].attr[9][0]=INFINITY;vertices[6].attr[9][1]=NAN;
 vertices[9].attr[9][3]=0;vertices[10].attr[9][3]=-0.0f;
 vertices[12].attr[9][0]=1e30f;vertices[13].attr[9][0]=-1e30f;vertices[14].attr[9][1]=1e30f;
 /* A thin finite triangle exercises cancellation in the determinant. */
 float thin[3][4]={{0,0,1,1},{1,1,2,2},{1.00000011920928955078125f,1,3,4}};
 for(unsigned i=0;i<3;i++)memcpy(vertices[15+i].attr[9],thin[i],16);
 memcpy(input.mapped,vertices,sizeof(vertices));
}
static void triangle_gpu(const char*folder,unsigned serial,SpvTriangleGeomOptions o,GlslcResult*ref,SpvDirectModule*direct){
 Capture r=instrument(ref->words,ref->num_words),d=instrument(direct->words,direct->num_words);
 save(folder,"triangle-capture-reference",o.spirv_1_6,serial,0,r.words,r.n);save(folder,"triangle-capture-direct",o.spirv_1_6,serial,0,d.words,d.n);
 GlslcResult vertex=test_vertex_shader(o.smooth_shading,o.spirv_1_6);VkShaderModule vs=gpu_module(vertex.words,vertex.num_words),rs=gpu_module(r.words,r.n),ds=gpu_module(d.words,d.n);
 float reference[GEOM_CASES*3*RECORD_FLOATS],actual[GEOM_CASES*3*RECORD_FLOATS];run_geometry(vs,rs,reference);run_geometry(vs,ds,actual);
 for(unsigned i=0;i<ARRAY_SIZE(actual);i++)if(!same_float(reference[i],actual[i])){fprintf(stderr,"Triangle GPU mismatch case=%u field=%u ref=%a direct=%a\n",serial,i,reference[i],actual[i]);abort();}
 const unsigned rotations[3][3]={{0,1,2},{2,0,1},{1,2,0}};
 for(unsigned t=0;t<GEOM_CASES;t++){
  const unsigned*v=rotations[(t&1)?o.rotation_odd:o.rotation_even];
  float *p0=vertices[t*3+v[0]].attr[9],*p1=vertices[t*3+v[1]].attr[9],*p2=vertices[t*3+v[2]].attr[9];
  float ax=p1[0]-p0[0],ay=p1[1]-p0[1],bx=p2[0]-p0[0],by=p2[1]-p0[1];
  float u=o.z_perspective?p0[3]-p1[3]:p1[2]-p0[2],b=o.z_perspective?p0[3]-p2[3]:p2[2]-p0[2];
  if(o.z_perspective){u/=p1[3]*p0[3];b/=p2[3]*p0[3];}
  float det=cpu_det(ax,by,bx,ay),dz=fmaxf(fabsf(cpu_det(u,by,b,ay)/det),fabsf(cpu_det(b,ax,u,bx)/det));if(!isfinite(dz))dz=0;
  unsigned prov=v[(t&1)?o.provoking_odd:o.provoking_even];
  for(unsigned out=0;out<3;out++){
   float oracle[RECORD_FLOATS]={0};TestVertex*cur=&vertices[t*3+v[out]];
   for(unsigned slot=0;slot<9;slot++){if(slot==4)oracle[slot*4]=cur->attr[4][1];else memcpy(oracle+slot*4,vertices[t*3+(slot<4&&!o.smooth_shading?prov:v[out])].attr[slot],16);}
   memcpy(oracle+9*4,p0,16);memcpy(oracle+10*4,p1,16);memcpy(oracle+11*4,p2,16);oracle[12*4]=dz;memcpy(oracle+13*4,cur->position,16);oracle[14*4]=cur->attr[4][0];
   /* GPU division need not match CPU division rounding. Only the CPU slope
    * oracle has tolerance; old/new GPU output still compares bit for bit. */
   for(unsigned f=0;f<RECORD_FLOATS;f++){float got=actual[(t*3+out)*RECORD_FLOATS+f];if(!same_float(got,oracle[f])&&!(got==0&&oracle[f]==0)&&!(f==48&&isfinite(got)&&isfinite(oracle[f])&&fabsf(got-oracle[f])<=fabsf(oracle[f])*2e-6f)){fprintf(stderr,"Triangle CPU mismatch case=%u triangle=%u vertex=%u field=%u got=%a expected=%a\n",serial,t,out,f,got,oracle[f]);abort();}}
  }
 }
 assert(!validation_errors);gpu_cases++;vkDestroyShaderModule(gd,vs,NULL);vkDestroyShaderModule(gd,rs,NULL);vkDestroyShaderModule(gd,ds,NULL);free(vertex.words);free(r.words);free(d.words);
}
#endif
static void triangle_tests(const char*folder){
 triangle_selector_tests();unsigned serial=0;
#ifdef TEST_VULKAN
 triangle_data();
#endif
 for(unsigned version=0;version<2;version++)for(unsigned smooth=0;smooth<2;smooth++)for(unsigned z=0;z<2;z++)
 for(unsigned r0=0;r0<3;r0++)for(unsigned r1=0;r1<3;r1++)for(unsigned prov=0;prov<4;prov++){
  GeomState state={.primitive_mode=prov==2?PRIM_TYPE_TRIANGLE_STRIP:prov==3?PRIM_TYPE_TRIANGLE_FAN:PRIM_TYPE_TRIANGLES,.polygon_front_mode=POLY_MODE_FILL,.polygon_back_mode=POLY_MODE_FILL,.smooth_shading=smooth,.first_vertex_is_provoking=prov==0,.z_perspective=z,.tri_rot0=r0,.tri_rot1=r1};
  ShaderModuleCacheKey key={.kind=8,.geom={.state=state,.glsl_opts={.vulkan=true}}};pgraph_vk_spirv_1_6=version;SpvTriangleGeomOptions o;assert(vk_module_triangle_options(&key,&o));
  MString*source=pgraph_glsl_gen_geom(&state,(GenGeomGlslOptions){.vulkan=true});allocations=0;GlslcResult ref=compile(GLSLC_STAGE_GEOMETRY,source->str,version);unsigned before=allocations;
  allocations=0;SpvDirectModule direct;assert(pgraph_spv_triangle_geom(o,&direct));assert(allocations==1);SpvDirectModule repeat;assert(pgraph_spv_triangle_geom(o,&repeat));assert(repeat.num_words==direct.num_words&&!memcmp(repeat.words,direct.words,direct.num_words*4));free(repeat.words);
  save(folder,"triangle-reference",version,serial,0,ref.words,ref.num_words);save(folder,"triangle-direct",version,serial,0,direct.words,direct.num_words);
#ifdef TEST_VULKAN
  triangle_gpu(folder,serial,o,&ref,&direct);
#endif
  if(!r0&&!r1&&!prov){const unsigned iterations=128;double start=now();for(unsigned i=0;i<iterations;i++){GlslcResult m=compile(GLSLC_STAGE_GEOMETRY,source->str,version);free(m.words);}double old=(now()-start)*1e6/iterations;start=now();for(unsigned i=0;i<iterations;i++){SpvDirectModule m;assert(pgraph_spv_triangle_geom(o,&m));free(m.words);}printf("Triangle SPV=%u smooth=%u perspective=%u: reference=%zu words/%u allocations/%.2fus; direct=%zu words/1 allocation/%.2fus\n",version,smooth,z,ref.num_words,before,old,direct.num_words,(now()-start)*1e6/iterations);}
  free(ref.words);free(direct.words);mstring_unref(source);serial++;
 }
 printf("Triangle geometry: %u variants, deterministic binary emission and one payload allocation passed\n",serial);
#ifdef TEST_VULKAN
 assert(gpu_cases==serial);finish_gpu();
#endif
}
