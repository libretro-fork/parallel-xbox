/*
 * CPU boost: xemu's accelerations of the CPU emulation
 *
 * Copyright (c) 2026 Réda Chérif-Touil
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

#ifndef QEMU_CPU_BOOST_H
#define QEMU_CPU_BOOST_H

/*
 * The "CPU boost" setting (perf.native_sse), read once at start-up by
 * tcg_x86_init(). Each of its accelerations sits next to the emulator's own
 * path and runs only when it is set: off, the stock code runs.
 */
extern int xemu_cpu_boost;

/*
 * The x86 TB flag bits that carry the class of the carry flag a block starts
 * with (TB_CF_*, target/i386/cpu.h), all set: TB_CF_ANY, a block whose code
 * is the same whatever that class is. One translation serves every class;
 * the TB lookups (cpu-exec.c, translator.c) fall back to it.
 */
#define XEMU_TB_CF_ANY ((3u << 30) | (1u << 2))

#endif
