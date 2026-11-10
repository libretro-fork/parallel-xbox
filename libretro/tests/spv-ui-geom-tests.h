/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef TEST_VULKAN
static void ui_geometry_data(unsigned count, bool strip, unsigned stage)
{
    if (!gd) { init_gpu(); }
    geometry_vertices=count;
    const unsigned corners[2][4]={{0,1,3,2},{0,1,2,3}};
    for (unsigned i=0; i<GEOM_CASES; i++) {
        unsigned variant=i%16;
        for (unsigned v=0; v<count; v++) {
            TestVertex *vertex=&vertices[i*count+v];
            unsigned corner=count==3 ? (v==2?2:v) : corners[strip][v];
            float x=(corner&1)?8.375f:0.375f;
            float y=(corner&2)?10.625f:2.625f;
            float *p=vertex->attr[9], *t=vertex->attr[4+stage];
            p[0]=x;p[1]=y;p[2]=0.25f;p[3]=1;
            t[0]=(variant&1)?-x*0.125f:x*0.125f;
            t[1]=y*0.25f;t[2]=0.5f;t[3]=1;
            if(variant==2){float tmp=t[0];t[0]=t[1];t[1]=tmp;}
            if(variant==3)t[3]=2;
            if(variant==4 && v==1)p[0]+=1;
            if(variant==5 && v==1)t[3]=2;
            if(variant==6 && v==1)p[2]=0.5f;
            if(variant==7 && v==1)p[3]=2;
            if(variant==8 && v==1)t[0]=NAN;
            if(variant==9 && v==1)p[1]=INFINITY;
            if(variant==10)t[3]=0;
            if(variant==11)t[3]=-1;
            if(variant==12 && v==1)t[0]+=0.25f;
            if(variant==13){p[0]=x*0.03125f;t[0]=p[0]*0.125f;}
            if(variant==14){p[0]+=65536;t[0]=p[0]*0.125f;}
            if(variant==15 && v==count-1)t[0]+=0.0000001f;
        }
    }
    memcpy(input.mapped,vertices,sizeof(vertices));
}

static void ui_geometry_gpu_tests(const char *folder)
{
    unsigned serial=0, marked=0, unmarked=0;
    for(unsigned version=0;version<2;version++)
    for(unsigned smooth=0;smooth<2;smooth++)for(unsigned z=0;z<2;z++)
    for(unsigned stage=1;stage<=4;stage++)for(unsigned kind=0;kind<3;kind++)
    for(unsigned mode=0;mode<3;mode++) {
        bool quad=kind!=0,strip=kind==2;
        ui_geometry_data(quad?4:3,strip,stage);
        geometry_outputs=quad?(mode==1?8:mode==2?4:6):(mode==1?6:3);
        GeomState state={.primitive_mode=quad?(strip?PRIM_TYPE_QUAD_STRIP:PRIM_TYPE_QUADS):PRIM_TYPE_TRIANGLE_STRIP,
            .polygon_front_mode=mode==1?POLY_MODE_LINE:mode==2?POLY_MODE_POINT:POLY_MODE_FILL,
            .polygon_back_mode=mode==1?POLY_MODE_LINE:mode==2?POLY_MODE_POINT:POLY_MODE_FILL,
            .smooth_shading=smooth,.z_perspective=z,.first_vertex_is_provoking=!!(mode&1),.tri_rot0=stage%3,.tri_rot1=(stage+1)%3};
        ShaderModuleCacheKey key={.kind=8,.geom={.state=state,.glsl_opts={.vulkan=true,.ui_clamp_stage=stage}}};
        pgraph_vk_spirv_1_6=version;
        SpvDirectModule direct;SpvTriangleGeomOptions tri;SpvQuadGeomOptions q;
        allocations=0;
        if(quad){assert(vk_module_quad_options(&key,&q));assert(pgraph_spv_quad_geom(q,&direct));}
        else{assert(vk_module_triangle_options(&key,&tri));assert(pgraph_spv_triangle_geom(tri,&direct));}
        assert(allocations==1);
        MString *source=pgraph_glsl_gen_geom(&state,key.geom.glsl_opts);
        GlslcResult reference=compile(GLSLC_STAGE_GEOMETRY,source->str,version);
        Capture r=instrument(reference.words,reference.num_words),d=instrument(direct.words,direct.num_words);
        save(folder,"ui-capture-reference",version,serial,0,r.words,r.n);
        save(folder,"ui-capture-direct",version,serial,0,d.words,d.n);
        GlslcResult vertex=test_vertex_shader(state.smooth_shading,version);
        VkShaderModule vs=gpu_module(vertex.words,vertex.num_words),rs=gpu_module(r.words,r.n),ds=gpu_module(d.words,d.n);
        float expected[GEOM_CASES*8*RECORD_FLOATS],actual[GEOM_CASES*8*RECORD_FLOATS];
        run_geometry(vs,rs,expected);run_geometry(vs,ds,actual);
        unsigned count=GEOM_CASES*geometry_outputs*RECORD_FLOATS;
        for(unsigned i=0;i<count;i++)if(!same_float(expected[i],actual[i])){
            fprintf(stderr,"UI GPU mismatch serial=%u stage=%u kind=%u mode=%u field=%u ref=%a direct=%a\n",serial,stage,kind,mode,i,expected[i],actual[i]);abort();
        }
        for(unsigned i=0;i<GEOM_CASES;i++){
            if(strip&&(i&1))continue;
            uint32_t bits;memcpy(&bits,actual+(i*geometry_outputs)*RECORD_FLOATS+48,4);
            if(bits==0x80000000u)marked++;else unmarked++;
        }
        assert(!validation_errors);gpu_cases++;serial++;
        vkDestroyShaderModule(gd,vs,NULL);vkDestroyShaderModule(gd,rs,NULL);vkDestroyShaderModule(gd,ds,NULL);
        free(reference.words);free(direct.words);free(vertex.words);free(r.words);free(d.words);mstring_unref(source);
        printf("UI GPU pair %u: all %u output scalars equal\n",serial,count);fflush(stdout);
    }
    assert(marked && unmarked);
    printf("UI GPU: %u pairs, marked=%u unmarked=%u, exact signed-marker/output comparison passed\n",serial,marked,unmarked);
    finish_gpu();
}
#endif
