/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Run real decoders concurrently on independent DSP machines. */
int dspdiff_original_main(int argc, char **argv);
#define main dspdiff_original_main
#include "dspdiff.c"
#undef main
#include <pthread.h>
#include <stdatomic.h>

static atomic_uint ready;
static atomic_bool start;

typedef struct DecodeWorker {
    Machine *machine;
    DspDecoded expected;
    unsigned failures;
} DecodeWorker;

static void *decode_worker(void *opaque)
{
    DecodeWorker *w = opaque;
    dsp_core_t *core = w->machine->dsp->backend;
    atomic_fetch_add(&ready, 1);
    while (!atomic_load(&start)) { }
    for (unsigned i = 0; i < 1000000; i++) {
        DspDecoded got;
        dsp56k_decode(core, 0, &got);
        if (got.inst != w->expected.inst || got.emu != w->expected.emu ||
            got.len != w->expected.len || got.control != w->expected.control ||
            got.name != w->expected.name || core->pc != 0x22) {
            w->failures++;
            break;
        }
    }
    return NULL;
}

int main(void)
{
    /* Both hash to 0xc1 but have different handlers and names. Initialize
     * opcode match tables serially, as core creation does, before decoding. */
    static const uint32_t ops[] = {0x0079B8, 0x018040};
    DecodeWorker workers[2] = {0};
    pthread_t threads[2];
    for (unsigned i = 0; i < 2; i++) {
        workers[i].machine = machine_new(false);
        dsp_core_t *core = workers[i].machine->dsp->backend;
        core->pram[0] = ops[i];
        core->pc = 0x22;
        dsp56k_decode(core, 0, &workers[i].expected);
        assert(workers[i].expected.emu && workers[i].expected.len == 1);
    }
    assert(workers[0].expected.emu != workers[1].expected.emu);
    for (unsigned i = 0; i < 2; i++) {
        assert(pthread_create(&threads[i], NULL, decode_worker, &workers[i]) == 0);
    }
    while (atomic_load(&ready) != 2) { }
    atomic_store(&start, true);
    unsigned failures = 0;
    for (unsigned i = 0; i < 2; i++) {
        assert(pthread_join(threads[i], NULL) == 0);
        failures += workers[i].failures;
        Machine *m = workers[i].machine;
        m->dsp->ops->finalize(m->dsp);
        free(m->dsp);
        free(m);
    }
    if (failures) {
        fprintf(stderr, "concurrent DSP decode returned mismatched handlers: %u workers\n", failures);
        return 1;
    }
    puts("dspdecode: 2,000,000 concurrent decodes agree with serial decoding");
    return 0;
}
