/* Frozen from 145d202; LGPL-2.1-or-later. */
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    if (pfifo_reg_r(d, NV_PFIFO_CACHE1_DMA_GET) ==
            pfifo_reg_r(d, NV_PFIFO_CACHE1_DMA_PUT) &&
        r->in_command_buffer &&
        !QSIMPLEQ_EMPTY(&r->report_queue)) {
        pgraph_vk_finish(pg, VK_FINISH_REASON_STALLED);
        return; /* the finish served what was available */
    }
    /* Serve what is available while a report waits on submitted slots,
     * and one whose window is already counted (a clear issued after the
     * last draw). */
    QueryReport *head = QSIMPLEQ_FIRST(&r->report_queue);
    if (head && (r->queries_read < r->queries_submitted ||
                 head->query_count <= r->queries_read)) {
        pgraph_vk_process_pending_reports_internal(d);
    }
}
