/*
 * DSP56300 JIT (dspjit.c), on the interpreter's state.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef DSP_JIT_H
#define DSP_JIT_H

#include <stdint.h>

/* The hosts the JIT has code for; on the others it makes no context and
 * the interpreter stands in */
#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(_M_ARM64)
#define DSPJIT_HOST 1
#else
#define DSPJIT_HOST 0
#endif

typedef struct dsp_core_s dsp_core_t;
typedef struct DspJit DspJit;

/* NULL where there is no JIT for the host */
DspJit *dspjit_create(dsp_core_t *core);
void dspjit_destroy(DspJit *jit);
/* All translations dropped: the program changed (bootstrap, a state) */
void dspjit_flush(DspJit *jit);
/* The translations of the P word at address dropped: it was written */
void dspjit_invalidate(DspJit *jit, uint32_t address);
/* As dsp_c_run: cycles of the DSP, until they are spent or it halts */
void dspjit_run(DspJit *jit, int cycles);

#endif
