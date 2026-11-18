/* SPDX-License-Identifier: GPL-2.0-or-later */
/* New registry cases supplement, rather than replace, the original corpus. */
static void release_registry(PGRAPHVkState *r)
{
 /* RELEASE */
}
static void corpus_step(PGRAPHState *pg)
{
 /* The original corpus's owners represent ordinary pass observations. Keep
  * its empty-bank call unchanged; registration itself is tested separately. */
 if(bank_n)for(unsigned i=0;i<pg->vk_renderer_state->render_passes->len;i++){
  RenderPass *pass=&g_array_index(pg->vk_renderer_state->render_passes,RenderPass,i);
  pump_register_render_pass(pg->vk_renderer_state,&pass->state);
 }
 pgraph_vk_libpump_step(pg);
}
static void registry_cases(void)
{
 PGRAPHVkState r={.device=true,.dyn_cull_front=true,.dyn_blend=true,.dyn_vertex_input=true,.color_binding=true};PGRAPHState pg={&r};
 r.render_passes=g_array_new(false,false,sizeof(RenderPass));r.live=(RenderPassState){VK_FORMAT_R32_SFLOAT,VK_FORMAT_D32_SFLOAT};
 RenderPass utility={.state={VK_FORMAT_UNDEFINED,VK_FORMAT_D32_SFLOAT}};g_array_append_val(r.render_passes,utility);
 g_config.perf.shader_seeding=true;bank_n=40;pipe_started=true;pump_d=0;pump_w_local=1;drains=0;pressure=false;probes=submits=0;
 /* Admission must be a zero-write, zero-allocation no-op when disabled or
  * any required dynamic capability is absent, even with a valid real pass. */
 for(unsigned disabled=0;disabled<4;disabled++){
  PumpProgress before=r.pump;unsigned alloc_before=array_allocations;
  g_config.perf.shader_seeding=disabled!=0;r.dyn_cull_front=disabled!=1;r.dyn_blend=disabled!=2;r.dyn_vertex_input=disabled!=3;
  pump_register_render_pass(&r,&utility.state);
  assert(!memcmp(&before,&r.pump,sizeof(before))&&array_allocations==alloc_before);
 }
 g_config.perf.shader_seeding=true;r.dyn_cull_front=r.dyn_blend=r.dyn_vertex_input=true;
 /* A utility native owner alone must never become a preparation recipe. */
 pgraph_vk_libpump_step(&pg);assert(r.pump.n==0&&!probes&&!submits&&drains==1);
 RenderPass passes[5]={0};
 for(unsigned i=0;i<5;i++){
  passes[i].state=(RenderPassState){i==0?VK_FORMAT_UNDEFINED:(VkFormat)(VK_FORMAT_R8G8B8A8_UNORM+i),i==0?VK_FORMAT_D16_UNORM:VK_FORMAT_UNDEFINED};
  g_array_append_val(r.render_passes,passes[i]);
 }
 /* An ordinary depth-only draw is legitimate, even before any seed exists. */
 bank_n=0;pump_register_render_pass(&r,&passes[0].state);assert(r.pump.n==1&&!r.pump.retry[0]);
 probes=submits=0;pgraph_vk_libpump_step(&pg);assert(!probes&&!submits&&!r.pump.next_pass&&!r.pump.state_hash);
 /* Explicit prewarm/ordinary duplicate observations share the same owner. */
 pump_register_render_pass(&r,&passes[0].state);assert(r.pump.n==1);
 for(unsigned i=1;i<5;i++)pump_register_render_pass(&r,&passes[i].state);
 assert(r.pump.n==4);for(unsigned i=0;i<4;i++){assert(!memcmp(&r.pump.rp[i],&passes[i].state,sizeof(RenderPassState)));assert(!r.pump.retry[i]);}
 bank_n=40;for(unsigned i=0;i<bank_n;i++){memset(&states[i],0,sizeof(states[i]));states[i].psh.final_inputs_0=i+1;hashes[i]=xxh3_64((const uint8_t *)&states[i],sizeof(states[i]),0);ready[i]=true;}
 memset(observed,0,sizeof(observed));
 /* Ready work needs no retry allocation, including across all four slots. */
 for(unsigned flip=0;flip<4;flip++){probes=submits=0;pgraph_vk_libpump_step(&pg);assert(submits==4&&probes==4);}
 for(unsigned i=0;i<4;i++)assert(!r.pump.retry[i]);
 /* A late unready slice allocates only the selected retry owner; bounded
  * pressure and eventual readiness preserve all four recipe identities. */
 for(unsigned i=4;i<bank_n;i++)ready[i]=false;
 probes=submits=0;pgraph_vk_libpump_step(&pg);assert(probes==32&&!submits&&r.pump.retry[0]&&r.pump.retry[0]->len==32);
 for(unsigned i=1;i<4;i++)assert(!r.pump.retry[i]);
 pressure=true;for(unsigned i=0;i<bank_n;i++)ready[i]=true;
 for(unsigned flip=0;flip<4;flip++){probes=submits=0;pgraph_vk_libpump_step(&pg);assert(submits<=1&&probes<=32);}
 pressure=false;
 for(unsigned flip=0;flip<160;flip++){probes=submits=0;pgraph_vk_libpump_step(&pg);assert(submits<=4&&probes<=32&&r.pump.n==4);}
 for(unsigned i=0;i<6;i++)for(unsigned j=0;j<bank_n;j++)assert(observed[i][j]==(unsigned)(i>=1&&i<=4));
 /* Registry copied formats survive native-array relocation/removal. */
 array_free(r.render_passes);r.render_passes=g_array_new(false,false,sizeof(RenderPass));
 r.pump.next_pass=999;RenderPassState out;assert(pump_next_render_pass(&r,&out));assert(!memcmp(&out,&passes[0].state,sizeof(out)));
 release_registry(&r);assert(!r.pump.n&&!r.pump.state_hash&&!r.pump.state_hash_base);array_free(r.render_passes);
 /* Fresh ready-only lifecycle has NULL retries and teardown remains safe. */
 memset(&r,0,sizeof(r));r.dyn_cull_front=r.dyn_blend=r.dyn_vertex_input=true;
 pump_register_render_pass(&r,&passes[0].state);assert(r.pump.n==1&&!r.pump.retry[0]);release_registry(&r);assert(!r.pump.n);
 puts("Observed-pass registry: utility exclusion, genuine depth-only, prewarm dedup, saturation, empty bank, ready/no-retry, late-unready, pressure, relocation and NULL lifecycle PASS");
}
static bool corpus_next_pass(PGRAPHVkState *r,RenderPassState *out)
{
 /* The old corpus directly tests cursor normalization even with seeding
  * disabled. Supply its ordinary observations for this isolated helper call. */
 bool saved=g_config.perf.shader_seeding;g_config.perf.shader_seeding=true;
 for(unsigned i=0;i<r->render_passes->len;i++)pump_register_render_pass(r,&g_array_index(r->render_passes,RenderPass,i).state);
 g_config.perf.shader_seeding=saved;return pump_next_render_pass(r,out);
}
#define pump_next_render_pass corpus_next_pass
#define pgraph_vk_libpump_step corpus_step
