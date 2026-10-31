/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PGRAPH_SPV_MODULE_H
#define PGRAPH_SPV_MODULE_H
#include <stddef.h>
#include <stdint.h>
typedef struct SpvStaticModule {
    const uint32_t *words;
    size_t num_words;
} SpvStaticModule;
#endif
