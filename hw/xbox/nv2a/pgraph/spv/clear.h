/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PGRAPH_SPV_CLEAR_H
#define PGRAPH_SPV_CLEAR_H
#include <stddef.h>
#include <stdint.h>

typedef struct SpvStaticModule {
    const uint32_t *words;
    size_t num_words;
} SpvStaticModule;

/* Immutable SPIR-V 1.0: no payload allocation, parser or mutable state.
 * Neither program declares descriptors or push constants. */
SpvStaticModule pgraph_spv_fullscreen_vertex(void);
SpvStaticModule pgraph_spv_solid_fragment(void);
#endif
