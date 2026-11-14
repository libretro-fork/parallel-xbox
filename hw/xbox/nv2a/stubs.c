/*
 * QEMU Geforce NV2A implementation
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2018-2021 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "nv2a_int.h"

#define DEFINE_STUB(name) \
    uint64_t name ## _read(void *opaque, \
                           hwaddr addr, \
                           unsigned int size) \
    { \
        return 0; \
    } \
    void name ## _write(void *opaque, \
                        hwaddr addr, \
                        uint64_t val, \
                        unsigned int size) \
    { \
    } \

DEFINE_STUB(prma)
DEFINE_STUB(pcounter)
DEFINE_STUB(pvpe)
DEFINE_STUB(ptv)
DEFINE_STUB(prmfb)
DEFINE_STUB(pstraps)
// DEFINE_STUB(pramin)

#undef DEFINE_STUB
