/* SPDX-License-Identifier: GPL-2.0-or-later */
static unsigned cuts[256],cut_count;
static uint64_t expected[256];
static void draw_triangle(PGRAPHState *pg);
static void workload(NV2AState *d,bool mixed) {
    PGRAPHState *pg=&d->pgraph;PGRAPHVkState *r=pg->vk_renderer_state;
    unsigned count=mixed?96:237;uint64_t total=0;cut_count=0;
    if(cost_model) {r->cost_pending_slot=cost_slots++;r->cost_pending_samples=true;xemu_cost_q_open=true;}
    for(unsigned i=0;i<count;i++) {
        if(i && (!mixed || i%3==0)) {
            cuts[cut_count]=r->num_queries_in_flight;expected[cut_count++]=total;r->new_query_needed=true;
        }
        if(mixed && i%11==0) end_render_pass(r);
        pg->clearing=mixed && i%13==0;pg->zpass_pixel_count_enable=!mixed || i%7!=0;
        begin_draw(pg);draw_triangle(pg);end_draw(pg);
        if(!pg->clearing && pg->zpass_pixel_count_enable) total+=64;
        r->pipeline_binding_changed=false;
    }
    end_render_pass(r);if(r->query_in_flight) end_query(r);
    assert(!r->query_in_flight && !r->query_in_render_pass && !r->cost_recording);
    cuts[cut_count]=r->num_queries_in_flight;expected[cut_count++]=total;
    if(!mixed) {
        assert(r->num_queries_in_flight==237);
        assert(counters[NV2A_PROF_PIPELINE_RENDERPASSES]==(r->host_query_reset?1:237));
        assert(counters[NV2A_PROF_PIPELINE_BIND]==(r->host_query_reset?1:237));
    }
}
static void check_results(const uint64_t *results) {
    unsigned consumed=0;uint64_t total=0;
    for(unsigned c=0;c<cut_count;c++) {
        while(consumed<cuts[c]) total+=results[consumed++];
        assert(total==expected[c]);
    }
}
