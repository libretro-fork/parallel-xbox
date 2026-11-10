/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PGRAPH_SPV_UTILITY_H
#define PGRAPH_SPV_UTILITY_H
#include <stdbool.h>
#include "module.h"
enum SpvUtilityShader {
    SPV_UTILITY_PVIDEO,
    SPV_UTILITY_PROPS_VERTEX,
    SPV_UTILITY_PROPS_GEOMETRY,
    SPV_UTILITY_PROPS_FRAGMENT,
    SPV_UTILITY_S3TC_ARRAY,
    SPV_UTILITY_S3TC_VOLUME,
    SPV_UTILITY_COUNT
};
/* Immutable core-image owner; words survive the synchronous module call. */
SpvStaticModule pgraph_spv_utility(enum SpvUtilityShader shader, bool spirv_1_6);
#endif
