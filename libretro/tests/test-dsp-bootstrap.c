/* SPDX-License-Identifier: GPL-2.0-or-later */
int dspdiff_main(int argc, char **argv);
#define main dspdiff_main
#include "dspdiff.c"
#undef main

static unsigned first_split, stride, alignment;
static void fragmented(void *opaque, uint32_t addr, size_t len, bool dir,
                       dsp_dma_run_func fn, void *user)
{
    Machine *m = opaque;
    assert(addr == 0 && len == 8192 && !dir);
    size_t done = 0;
    if (first_split) {
        fn(user, m->scratch + alignment, first_split);
        done = first_split;
    }
    while (done < len) {
        size_t n = MIN(len - done, stride);
        fn(user, m->scratch + alignment + done, n);
        done += n;
        fn(user, m->scratch + alignment + done, 0);
    }
}

int main(void)
{
    unsigned cases = 0;
    for (unsigned jit = 0; jit < 2; jit++) {
        Machine *m = machine_new(jit);
        dsp_core_t *c = m->dsp->backend;
        m->dsp->dma.scratch_walk = fragmented;
        for (alignment = 0; alignment < 4; alignment++) {
            for (unsigned i = 0; i < 8192; i++) m->scratch[alignment + i] = (i * 37) ^ (i >> 4);
            for (first_split = 0; first_split <= 8192; first_split++) {
                stride = 4096;
                memset(c->pram, 0xa5, sizeof(c->pram));
                memset(c->pram_opcache, 0xff, sizeof(c->pram_opcache));
                m->dsp->ops->bootstrap(m->dsp);
                for (unsigned i = 0; i < DSP_PRAM_SIZE; i++) {
                    uint32_t expected = 0xa5a5a5a5;
                    if (i < 0x800) {
                        memcpy(&expected, m->scratch + alignment + i * 4, 4);
                        expected &= 0xffffff;
                    }
                    if (c->pram[i] != expected) return 1;
                }
                for (unsigned i = 0; i < sizeof(c->pram_opcache); i++) {
                    if (((uint8_t *)c->pram_opcache)[i]) return 1;
                }
                cases++;
            }
        }
        /* Repeated sub-word fragments, including a partially filled word. */
        alignment = 1;
        first_split = 1;
        for (stride = 1; stride <= 9; stride++) {
            m->dsp->ops->bootstrap(m->dsp);
            for (unsigned i = 0; i < 0x800; i++) {
                uint32_t expected;
                memcpy(&expected, m->scratch + alignment + i * 4, 4);
                if (c->pram[i] != (expected & 0xffffff)) return 1;
            }
            cases++;
        }
        /* Bootstrap must retire compiled CLR A before loading NOP at its PC. */
        c->pram[0] = 0x200013;
        c->pram[1] = 0x08f484;
        c->pram[2] = 1;
        m->dsp->ops->invalidate_opcache(m->dsp);
        c->pc = 0;
        c->registers[DSP_REG_A1] = 0x123456;
        if (run_frame(m, 10000, false) || c->registers[DSP_REG_A1]) return 1;
        uint32_t program[3] = { 0, 0x08f484, 1 };
        memcpy(m->scratch + alignment, program, sizeof(program));
        stride = 3;
        m->dsp->ops->bootstrap(m->dsp);
        c->pc = 0;
        c->registers[DSP_REG_A1] = 0x123456;
        if (run_frame(m, 10000, false) || c->registers[DSP_REG_A1] != 0x123456) return 1;
        cases++;
        m->dsp->ops->finalize(m->dsp);
        free(m->dsp);
        free(m);
    }
    printf("DSP bootstrap: %u mask/fragment/cache cases pass\n", cases);
    return 0;
}
