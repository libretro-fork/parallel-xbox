/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef HW_XBOX_NV2A_VSH_STATE_H
#define HW_XBOX_NV2A_VSH_STATE_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "hw/xbox/nv2a/nv2a_regs.h"
#include "nv2a_vsh_disassembler.h"

/* Derived host state, deliberately absent from savestates. Exact source
 * comparisons also detect restored programs and partial instruction uploads,
 * without relying on the renderer's independent program_data_dirty flag. */
typedef struct PGRAPHVshStateCache {
    Nv2aVshStep steps[NV2A_MAX_TRANSFORM_PROGRAM_LENGTH];
    uint32_t words[NV2A_MAX_TRANSFORM_PROGRAM_LENGTH][4];
    bool valid[NV2A_MAX_TRANSFORM_PROGRAM_LENGTH];
} PGRAPHVshStateCache;

/* The returned steps are borrowed until the next decode. Walk only the
 * active program; bytes after FINAL can belong to a different program. */
static inline Nv2aVshParseResult pgraph_vsh_state_program(
    PGRAPHVshStateCache *cache, const uint32_t words[][4], unsigned start,
    Nv2aVshProgram *program)
{
    for (unsigned i = start; i < NV2A_MAX_TRANSFORM_PROGRAM_LENGTH; i++) {
        if (!cache->valid[i] ||
            memcmp(cache->words[i], words[i], sizeof(cache->words[i]))) {
            cache->valid[i] = false;
            Nv2aVshParseResult result = nv2a_vsh_parse_step(&cache->steps[i],
                                                          words[i]);
            if (result != NV2AVPR_SUCCESS) {
                return result;
            }
            memcpy(cache->words[i], words[i], sizeof(cache->words[i]));
            cache->valid[i] = true;
        }
        if (cache->steps[i].is_final) {
            program->steps = &cache->steps[start];
            return NV2AVPR_SUCCESS;
        }
    }
    return NV2AVPR_BAD_PROGRAM;
}

#endif
