/* SPDX-License-Identifier: GPL-2.0-or-later */
static void quad_selector_tests(void)
{
 unsigned count=0;
 for(unsigned primitive=0;primitive<=PRIM_TYPE_POLYGON;primitive++)
 for(unsigned front=0;front<3;front++)for(unsigned back=0;back<3;back++)
 for(unsigned smooth=0;smooth<2;smooth++)for(unsigned first=0;first<2;first++)
 for(unsigned wide=0;wide<2;wide++)for(unsigned vk=0;vk<2;vk++)
 for(unsigned stage=0;stage<4;stage++)for(unsigned z=0;z<2;z++){
  unsigned stages[]={1,8,16,32};
  ShaderModuleCacheKey key={.kind=stages[stage],.geom={.state={.primitive_mode=primitive,.polygon_front_mode=front,.polygon_back_mode=back,.smooth_shading=smooth,.first_vertex_is_provoking=first,.wide_lines=wide,.z_perspective=z,.tri_rot0=1,.tri_rot1=2},.glsl_opts={.vulkan=vk}}};
  bool expected=stages[stage]==8&&vk&&!wide&&front==back&&(primitive==PRIM_TYPE_QUADS||primitive==PRIM_TYPE_QUAD_STRIP);
  pgraph_vk_spirv_1_6=true;SpvQuadGeomOptions o={0};assert(vk_module_quad_options(&key,&o)==expected);
  if(expected){assert(o.smooth_shading==smooth&&o.z_perspective==z&&o.spirv_1_6&&o.strip==(primitive==PRIM_TYPE_QUAD_STRIP));assert(o.polygon_mode==(front==POLY_MODE_LINE?SPV_GEOM_LINE:front==POLY_MODE_POINT?SPV_GEOM_POINT:SPV_GEOM_FILL));}count++;
 }
 SpvDirectModule invalid;assert(!pgraph_spv_quad_geom((SpvQuadGeomOptions){.polygon_mode=3},&invalid));assert(!invalid.words&&!invalid.num_words);
 printf("Quad selector: %u cases passed\n",count);
}
#ifdef TEST_VULKAN
static float quad_cpu_slope(const float*p0,const float*p1,const float*p2,bool perspective){
 float ax=p1[0]-p0[0],ay=p1[1]-p0[1],bx=p2[0]-p0[0],by=p2[1]-p0[1];
 float u=perspective?p0[3]-p1[3]:p1[2]-p0[2],v=perspective?p0[3]-p2[3]:p2[2]-p0[2];
 if(perspective){u/=p1[3]*p0[3];v/=p2[3]*p0[3];}
 float det=cpu_det(ax,by,bx,ay),dz=fmaxf(fabsf(cpu_det(u,by,v,ay)/det),fabsf(cpu_det(v,ax,u,bx)/det));return isfinite(dz)?dz:0;
}
static void quad_data(void){
 if(!gd)init_gpu();geometry_vertices=4;geometry_outputs=6;
 for(unsigned i=0;i<GEOM_CASES*4;i++){
  uint32_t h=(i+1)*747796405u+2891336453u;
  for(unsigned c=0;c<4;c++){h=h*1664525u+1013904223u;vertices[i].attr[9][c]=(float)((int)(h>>24)-128)*0.0625f;}
 }
 /* Both planes see degenerate and non-finite inputs; flat colours and all
  * unrelated vertex outputs must remain intact even when slopes sanitize. */
 for(unsigned i=1;i<4;i++)memcpy(vertices[i].attr[9],vertices[0].attr[9],16);
 vertices[8].attr[9][0]=INFINITY;vertices[9].attr[9][0]=INFINITY;vertices[10].attr[9][1]=NAN;
 vertices[16].attr[9][3]=0;vertices[17].attr[9][3]=-0.0f;
 vertices[24].attr[9][0]=1e30f;vertices[25].attr[9][0]=-1e30f;vertices[26].attr[9][1]=1e30f;
 float thin[4][4]={{0,0,1,1},{1,1,2,2},{1.00000011920928955078125f,1,3,4},{0,1,4,8}};
 for(unsigned i=0;i<4;i++)memcpy(vertices[32+i].attr[9],thin[i],16);
 memcpy(input.mapped,vertices,sizeof(vertices));
}
static void quad_gpu(const char*folder,unsigned serial,SpvQuadGeomOptions o,GlslcResult*ref,SpvDirectModule*direct){
 Capture r=instrument(ref->words,ref->num_words),d=instrument(direct->words,direct->num_words);
 save(folder,"quad-capture-reference",o.spirv_1_6,serial,0,r.words,r.n);save(folder,"quad-capture-direct",o.spirv_1_6,serial,0,d.words,d.n);
 GlslcResult vertex=test_vertex_shader(o.smooth_shading,o.spirv_1_6);VkShaderModule vs=gpu_module(vertex.words,vertex.num_words),rs=gpu_module(r.words,r.n),ds=gpu_module(d.words,d.n);
 float reference[GEOM_CASES*6*RECORD_FLOATS],actual[GEOM_CASES*6*RECORD_FLOATS];run_geometry(vs,rs,reference);run_geometry(vs,ds,actual);
 for(unsigned i=0;i<ARRAY_SIZE(actual);i++)if(!same_float(reference[i],actual[i])){fprintf(stderr,"Quad GPU mismatch case=%u field=%u ref=%a direct=%a\n",serial,i,reference[i],actual[i]);abort();}
 const unsigned planes[2][2][3]={{{0,1,2},{0,2,3}},{{2,0,1},{2,1,3}}};
 const unsigned emit[2][6]={{1,2,0,2,3,0},{0,1,2,2,1,3}};
 for(unsigned q=0;q<GEOM_CASES;q++){
  if(o.strip&&(q&1)){
   const unsigned char*bytes=(const unsigned char*)(actual+q*6*RECORD_FLOATS);for(unsigned i=0;i<6*RECORD_FLOATS*sizeof(float);i++)assert(bytes[i]==0xcd);continue;
  }
  for(unsigned plane=0;plane<2;plane++){
   const unsigned*v=planes[o.strip][plane];float*p0=vertices[q*4+v[0]].attr[9],*p1=vertices[q*4+v[1]].attr[9],*p2=vertices[q*4+v[2]].attr[9];float dz=quad_cpu_slope(p0,p1,p2,o.z_perspective);
   for(unsigned out=0;out<3;out++){
    unsigned index=emit[o.strip][plane*3+out];float oracle[RECORD_FLOATS]={0};TestVertex*cur=&vertices[q*4+index];
    for(unsigned slot=0;slot<9;slot++){if(slot==4)oracle[slot*4]=cur->attr[4][1];else memcpy(oracle+slot*4,vertices[q*4+(slot<4&&!o.smooth_shading?3:index)].attr[slot],16);}
    memcpy(oracle+9*4,p0,16);memcpy(oracle+10*4,p1,16);memcpy(oracle+11*4,p2,16);oracle[12*4]=dz;memcpy(oracle+13*4,cur->position,16);oracle[14*4]=cur->attr[4][0];
    /* Only the CPU division oracle has tolerance; old/new GPU data is exact. */
    for(unsigned f=0;f<RECORD_FLOATS;f++){float got=actual[(q*6+plane*3+out)*RECORD_FLOATS+f];if(!same_float(got,oracle[f])&&!(got==0&&oracle[f]==0)&&!(f==48&&isfinite(got)&&isfinite(oracle[f])&&fabsf(got-oracle[f])<=fabsf(oracle[f])*2e-6f)){fprintf(stderr,"Quad CPU mismatch case=%u quad=%u plane=%u vertex=%u field=%u got=%a expected=%a\n",serial,q,plane,out,f,got,oracle[f]);abort();}}
   }
  }
 }
 assert(!validation_errors);gpu_cases++;vkDestroyShaderModule(gd,vs,NULL);vkDestroyShaderModule(gd,rs,NULL);vkDestroyShaderModule(gd,ds,NULL);free(vertex.words);free(r.words);free(d.words);
}
#endif
static void quad_tests(const char*folder){
 quad_selector_tests();unsigned serial=0;
#ifdef TEST_VULKAN
 quad_data();
#endif
 for(unsigned version=0;version<2;version++)for(unsigned smooth=0;smooth<2;smooth++)for(unsigned z=0;z<2;z++)for(unsigned strip=0;strip<2;strip++){
  GeomState state={.primitive_mode=strip?PRIM_TYPE_QUAD_STRIP:PRIM_TYPE_QUADS,.polygon_front_mode=POLY_MODE_FILL,.polygon_back_mode=POLY_MODE_FILL,.smooth_shading=smooth,.first_vertex_is_provoking=true,.z_perspective=z,.tri_rot0=2,.tri_rot1=1};
  ShaderModuleCacheKey key={.kind=8,.geom={.state=state,.glsl_opts={.vulkan=true}}};pgraph_vk_spirv_1_6=version;SpvQuadGeomOptions o;assert(vk_module_quad_options(&key,&o));
  MString*source=pgraph_glsl_gen_geom(&state,(GenGeomGlslOptions){.vulkan=true});allocations=0;GlslcResult ref=compile(GLSLC_STAGE_GEOMETRY,source->str,version);unsigned before=allocations;
  allocations=0;SpvDirectModule direct;assert(pgraph_spv_quad_geom(o,&direct));assert(allocations==1);SpvDirectModule repeat;assert(pgraph_spv_quad_geom(o,&repeat));assert(repeat.num_words==direct.num_words&&!memcmp(repeat.words,direct.words,direct.num_words*4));free(repeat.words);
  save(folder,"quad-reference",version,serial,0,ref.words,ref.num_words);save(folder,"quad-direct",version,serial,0,direct.words,direct.num_words);
#ifdef TEST_VULKAN
  quad_gpu(folder,serial,o,&ref,&direct);
#endif
  const unsigned iterations=128;double start=now();for(unsigned i=0;i<iterations;i++){GlslcResult m=compile(GLSLC_STAGE_GEOMETRY,source->str,version);free(m.words);}double old=(now()-start)*1e6/iterations;start=now();for(unsigned i=0;i<iterations;i++){SpvDirectModule m;assert(pgraph_spv_quad_geom(o,&m));free(m.words);}printf("Quad SPV=%u smooth=%u perspective=%u strip=%u: reference=%zu words/%u allocations/%.2fus; direct=%zu words/1 allocation/%.2fus\n",version,smooth,z,strip,ref.num_words,before,old,direct.num_words,(now()-start)*1e6/iterations);
  free(ref.words);free(direct.words);mstring_unref(source);serial++;
 }
 printf("Quad geometry: %u variants, independent planes, deterministic binary emission and one payload allocation passed\n",serial);
#ifdef TEST_VULKAN
 assert(gpu_cases==serial);finish_gpu();
#endif
}
