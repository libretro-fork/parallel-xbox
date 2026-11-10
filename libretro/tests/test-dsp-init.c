/* SPDX-License-Identifier: GPL-2.0-or-later */
int dspdiff_main(int argc, char **argv);
#define main dspdiff_main
#include "dspdiff.c"
#undef main

int main(void)
{
    unsigned cases = 0;
    for (unsigned repeat = 0; repeat < 16; repeat++) {
        for (unsigned gp = 0; gp < 2; gp++) {
            for (unsigned jit = 0; jit < 2; jit++) {
                DSPState *dsp = calloc(1, sizeof(*dsp));
                dsp_core_t *expected = calloc(1, sizeof(*expected));
                assert(dsp && expected);
                dsp->is_gp = gp;
                dsp->core.is_gp = gp;
                if (jit) dsp_jit_init(dsp); else dsp_c_init(dsp);
                dsp_core_t *actual = dsp->backend;

                /* Independent oracle: the previous zero-allocation/reset,
                 * followed by three separate RAM pattern fills. Compare all
                 * bytes, including padding; test allocations are poisoned. */
                expected->opaque = dsp;
                expected->read_peripheral = actual->read_peripheral;
                expected->write_peripheral = actual->write_peripheral;
                expected->jit = actual->jit;
                assert(expected->read_peripheral && expected->write_peripheral);
                assert(!!expected->jit == !!jit);
                dsp56k_reset_cpu(expected);
                memset(expected->xram, 0xCA, sizeof(expected->xram));
                memset(expected->yram, 0xCA, sizeof(expected->yram));
                memset(expected->pram, 0xCA, sizeof(expected->pram));
                for (size_t i = 0; i < sizeof(*actual); i++) {
                    if (((uint8_t *)actual)[i] != ((uint8_t *)expected)[i]) {
                        fprintf(stderr, "DSP init gp=%u jit=%u byte=%zu: %02x != %02x\n",
                                gp, jit, i, ((uint8_t *)actual)[i], ((uint8_t *)expected)[i]);
                        return 1;
                    }
                }
                assert(dsp->dma.mem_opaque == actual);
                assert(dsp->dma.mem_read && dsp->dma.mem_write &&
                       dsp->dma.mem_ptr && dsp->dma.mem_descriptor);
                actual->pram[0] = 0x200013; /* CLR A */
                actual->pram[1] = 0x08f484; /* MOVEP #1, X:HALT */
                actual->pram[2] = 1;
                actual->pc = 0;
                actual->registers[DSP_REG_A1] = 0x123456;
                actual->is_idle = false;
                dsp->ops->run(dsp, 10000);
                assert(actual->is_idle && actual->registers[DSP_REG_A1] == 0);
                dsp->ops->finalize(dsp);
                free(expected);
                free(dsp);
                cases++;
            }
        }
    }
    printf("DSP initialization: %u complete-state and execution comparisons passed\n", cases);
    return 0;
}
