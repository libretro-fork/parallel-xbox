/*
 * The DSP without its JIT, in builds without the dsp56300 library the JIT
 * runs on (there is none for Android): the C interpreter stands in for it,
 * and the core has no DSP JIT option (CONFIG_DSP_JIT).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "dsp_internal.h"

const DSPOps jit_dsp_ops;

void dsp_jit_init(DSPState *dsp)
{
    dsp_c_init(dsp);
}
