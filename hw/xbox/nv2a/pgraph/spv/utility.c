/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "utility.h"
#include "utility-words.inc"
SpvStaticModule pgraph_spv_utility(enum SpvUtilityShader shader, bool spirv_1_6)
{
    static const SpvStaticModule modules[2][SPV_UTILITY_COUNT] = {
        {
            {utility_0_0, sizeof(utility_0_0)/sizeof(uint32_t)},
            {utility_0_1, sizeof(utility_0_1)/sizeof(uint32_t)},
            {utility_0_2, sizeof(utility_0_2)/sizeof(uint32_t)},
            {utility_0_3, sizeof(utility_0_3)/sizeof(uint32_t)},
            {utility_0_4, sizeof(utility_0_4)/sizeof(uint32_t)},
            {utility_0_5, sizeof(utility_0_5)/sizeof(uint32_t)},
        },
        {
            {utility_1_0, sizeof(utility_1_0)/sizeof(uint32_t)},
            {utility_1_1, sizeof(utility_1_1)/sizeof(uint32_t)},
            {utility_1_2, sizeof(utility_1_2)/sizeof(uint32_t)},
            {utility_1_3, sizeof(utility_1_3)/sizeof(uint32_t)},
            {utility_1_4, sizeof(utility_1_4)/sizeof(uint32_t)},
            {utility_1_5, sizeof(utility_1_5)/sizeof(uint32_t)},
        },
    };
    if ((unsigned)shader >= SPV_UTILITY_COUNT) { return (SpvStaticModule){0}; }
    return modules[spirv_1_6][shader];
}
