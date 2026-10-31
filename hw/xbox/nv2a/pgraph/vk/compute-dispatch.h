/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PGRAPH_VK_COMPUTE_DISPATCH_H
#define PGRAPH_VK_COMPUTE_DISPATCH_H
#include <stdbool.h>
#include <stdint.h>
typedef struct ComputeGrid { uint32_t x,y; } ComputeGrid;
/* Balance a two-dimensional grid without an almost-empty final row. The
 * shader flattens y using NumWorkgroups.x and bounds-checks every invocation. */
static inline bool pgraph_compute_grid(uint32_t pixels,uint32_t group,
                                       uint32_t max_x,uint32_t max_y,ComputeGrid*out)
{
    if(!group||!max_x||!max_y)return false;
    uint64_t groups=((uint64_t)pixels+group-1)/group;
    if(!groups){*out=(ComputeGrid){0,1};return true;}
    uint64_t y=(groups+max_x-1)/max_x;
    if(y>max_y)return false;
    uint64_t x=(groups+y-1)/y;
    if(x*y*group>UINT32_MAX)return false;
    *out=(ComputeGrid){(uint32_t)x,(uint32_t)y};return true;
}
#endif
