/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PGRAPH_SPV_PSH_H
#define PGRAPH_SPV_PSH_H
#include "../glsl/psh.h"
#include "glslc.h"
typedef struct SpvPshOptions { bool spirv_1_6; unsigned ubo_binding, tex_binding; } SpvPshOptions;
/* Immutable cache/job state is borrowed through count/write only. Result
 * words and metadata have separate final owners; unsupported states allocate nothing. */
bool pgraph_spv_psh_supported(const PshState *state, SpvPshOptions opts);
bool pgraph_spv_psh(const PshState *state, SpvPshOptions opts, GlslcResult *out);
#endif
