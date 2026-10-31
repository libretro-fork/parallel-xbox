/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef TEST_VULKAN
static void cpu_edge_positions(const float*a,const float*b,float p[3][4]){
 memcpy(p[0],a,16);memcpy(p[1],b,16);p[2][0]=-(b[1]-a[1])+a[0];p[2][1]=(b[0]-a[0])+a[1];p[2][2]=a[2];p[2][3]=a[3];
}
static void modes_gpu(const char*folder,unsigned serial,bool quad,const SpvTriangleGeomOptions*tri,const SpvQuadGeomOptions*q,GlslcResult*ref,SpvDirectModule*direct){
 bool smooth=quad?q->smooth_shading:tri->smooth_shading,version=quad?q->spirv_1_6:tri->spirv_1_6,perspective=quad?q->z_perspective:tri->z_perspective;
 bool wire=(quad?q->polygon_mode:tri->polygon_mode)==SPV_GEOM_LINE;
 geometry_outputs=quad?(wire?8:4):(wire?6:3);
 Capture r=instrument(ref->words,ref->num_words),d=instrument(direct->words,direct->num_words);
 save(folder,quad?"mode-quad-capture-reference":"mode-tri-capture-reference",version,serial,0,r.words,r.n);save(folder,quad?"mode-quad-capture-direct":"mode-tri-capture-direct",version,serial,0,d.words,d.n);
 GlslcResult vertex=test_vertex_shader(smooth,version);VkShaderModule vs=gpu_module(vertex.words,vertex.num_words),rs=gpu_module(r.words,r.n),ds=gpu_module(d.words,d.n);
 float reference[GEOM_CASES*8*RECORD_FLOATS],actual[GEOM_CASES*8*RECORD_FLOATS];run_geometry(vs,rs,reference);run_geometry(vs,ds,actual);unsigned count=GEOM_CASES*geometry_outputs*RECORD_FLOATS;
 for(unsigned i=0;i<count;i++)if(!same_float(reference[i],actual[i])){fprintf(stderr,"Mode GPU mismatch quad=%u case=%u field=%u ref=%a direct=%a\n",quad,serial,i,reference[i],actual[i]);abort();}
 const unsigned rotations[3][3]={{0,1,2},{2,0,1},{1,2,0}};
 const unsigned planes[2][2][3]={{{0,1,2},{0,2,3}},{{2,0,1},{2,1,3}}};
 const unsigned edges[2][8]={{0,1,1,2,2,3,3,0},{0,1,1,3,3,2,2,0}},strip_planes[4]={0,1,1,0};
 for(unsigned primitive=0;primitive<GEOM_CASES;primitive++){
  TestVertex*base=vertices+primitive*geometry_vertices;
  if(quad&&q->strip&&(primitive&1)){
   const unsigned char*bytes=(const unsigned char*)(actual+primitive*geometry_outputs*RECORD_FLOATS);for(unsigned i=0;i<geometry_outputs*RECORD_FLOATS*sizeof(float);i++)assert(bytes[i]==0xcd);continue;
  }
  for(unsigned emission=0;emission<geometry_outputs;emission++){
   float positions[3][4],dz;unsigned index,prov;
   if(quad){
    unsigned edge=emission/2;index=wire?edges[q->strip][emission]:emission;prov=3;
    unsigned plane=wire?(q->strip?strip_planes[edge]:edge/2):(emission==3?1:0);
    const unsigned*v=planes[q->strip][plane];dz=quad_cpu_slope(base[v[0]].attr[9],base[v[1]].attr[9],base[v[2]].attr[9],perspective);
    if(wire)cpu_edge_positions(base[edges[q->strip][edge*2]].attr[9],base[edges[q->strip][edge*2+1]].attr[9],positions);
    else for(unsigned p=0;p<3;p++)memcpy(positions[p],base[index].attr[9],16);
   }else{
    const unsigned*v=rotations[(primitive&1)?tri->rotation_odd:tri->rotation_even];unsigned edge=emission/2;index=v[wire?(edge+(emission&1))%3:emission];prov=v[(primitive&1)?tri->provoking_odd:tri->provoking_even];
    dz=quad_cpu_slope(base[v[0]].attr[9],base[v[1]].attr[9],base[v[2]].attr[9],perspective);
    if(wire)cpu_edge_positions(base[v[edge]].attr[9],base[v[(edge+1)%3]].attr[9],positions);
    else for(unsigned p=0;p<3;p++)memcpy(positions[p],base[index].attr[9],16);
   }
   float oracle[RECORD_FLOATS]={0};TestVertex*cur=&base[index];
   for(unsigned slot=0;slot<9;slot++){if(slot==4)oracle[slot*4]=cur->attr[4][1];else memcpy(oracle+slot*4,base[slot<4&&!smooth?prov:index].attr[slot],16);}
   for(unsigned p=0;p<3;p++)memcpy(oracle+(9+p)*4,positions[p],16);oracle[12*4]=dz;memcpy(oracle+13*4,cur->position,16);oracle[14*4]=cur->attr[4][0];
   for(unsigned f=0;f<RECORD_FLOATS;f++){float got=actual[(primitive*geometry_outputs+emission)*RECORD_FLOATS+f];if(!same_float(got,oracle[f])&&!(got==0&&oracle[f]==0)&&!(f==48&&isfinite(got)&&isfinite(oracle[f])&&fabsf(got-oracle[f])<=fabsf(oracle[f])*2e-6f)){fprintf(stderr,"Mode CPU mismatch quad=%u case=%u primitive=%u vertex=%u field=%u got=%a expected=%a\n",quad,serial,primitive,emission,f,got,oracle[f]);abort();}}
  }
 }
 assert(!validation_errors);gpu_cases++;vkDestroyShaderModule(gd,vs,NULL);vkDestroyShaderModule(gd,rs,NULL);vkDestroyShaderModule(gd,ds,NULL);free(vertex.words);free(r.words);free(d.words);
}
#endif
static void modes_tests(const char*folder){
 for(unsigned quad=0;quad<2;quad++){
  unsigned serial=0;
#ifdef TEST_VULKAN
  if(quad)quad_data();else triangle_data();
#endif
  for(unsigned mode=1;mode<3;mode++)for(unsigned version=0;version<2;version++)for(unsigned smooth=0;smooth<2;smooth++)for(unsigned z=0;z<2;z++)
  for(unsigned r0=0;r0<(quad?1u:3u);r0++)for(unsigned r1=0;r1<(quad?1u:3u);r1++)for(unsigned prov=0;prov<(quad?2u:4u);prov++){
   unsigned primitive=quad?(prov?PRIM_TYPE_QUAD_STRIP:PRIM_TYPE_QUADS):prov==2?PRIM_TYPE_TRIANGLE_STRIP:prov==3?PRIM_TYPE_TRIANGLE_FAN:PRIM_TYPE_TRIANGLES;
   unsigned polygon=mode==1?POLY_MODE_LINE:POLY_MODE_POINT;
   GeomState state={.primitive_mode=primitive,.polygon_front_mode=polygon,.polygon_back_mode=polygon,.smooth_shading=smooth,.first_vertex_is_provoking=prov==0,.z_perspective=z,.tri_rot0=r0,.tri_rot1=r1};
   ShaderModuleCacheKey key={.kind=8,.geom={.state=state,.glsl_opts={.vulkan=true}}};pgraph_vk_spirv_1_6=version;
   SpvTriangleGeomOptions tri={0};SpvQuadGeomOptions q={0};if(quad)assert(vk_module_quad_options(&key,&q));else assert(vk_module_triangle_options(&key,&tri));
   MString*source=pgraph_glsl_gen_geom(&state,(GenGeomGlslOptions){.vulkan=true});allocations=0;GlslcResult ref=compile(GLSLC_STAGE_GEOMETRY,source->str,version);unsigned before=allocations;
   allocations=0;SpvDirectModule direct;assert(quad?pgraph_spv_quad_geom(q,&direct):pgraph_spv_triangle_geom(tri,&direct));assert(allocations==1);SpvDirectModule repeat;assert(quad?pgraph_spv_quad_geom(q,&repeat):pgraph_spv_triangle_geom(tri,&repeat));assert(repeat.num_words==direct.num_words&&!memcmp(repeat.words,direct.words,direct.num_words*4));free(repeat.words);
   save(folder,quad?"mode-quad-reference":"mode-tri-reference",version,serial,0,ref.words,ref.num_words);save(folder,quad?"mode-quad-direct":"mode-tri-direct",version,serial,0,direct.words,direct.num_words);
#ifdef TEST_VULKAN
   modes_gpu(folder,serial,quad,&tri,&q,&ref,&direct);
#endif
   if(!r0&&!r1&&!prov){const unsigned iterations=128;double start=now();for(unsigned i=0;i<iterations;i++){GlslcResult m=compile(GLSLC_STAGE_GEOMETRY,source->str,version);free(m.words);}double old=(now()-start)*1e6/iterations;start=now();for(unsigned i=0;i<iterations;i++){SpvDirectModule m;assert(quad?pgraph_spv_quad_geom(q,&m):pgraph_spv_triangle_geom(tri,&m));free(m.words);}printf("Mode quad=%u mode=%u SPV=%u smooth=%u perspective=%u: reference=%zu words/%u allocations/%.2fus; direct=%zu words/1 allocation/%.2fus\n",quad,mode,version,smooth,z,ref.num_words,before,old,direct.num_words,(now()-start)*1e6/iterations);}
   free(ref.words);free(direct.words);mstring_unref(source);serial++;
  }
  printf("Polygon geometry: quad=%u, %u wire/point variants and one payload allocation passed\n",quad,serial);
#ifdef TEST_VULKAN
  assert(gpu_cases==serial);finish_gpu();
#endif
 }
}
