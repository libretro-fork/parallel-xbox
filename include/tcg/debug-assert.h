/* SPDX-License-Identifier: MIT */
/*
 * Define tcg_debug_assert
 * Copyright (c) 2008 Fabrice Bellard
 */

#ifndef TCG_DEBUG_ASSERT_H
#define TCG_DEBUG_ASSERT_H

#if defined CONFIG_DEBUG_TCG || defined QEMU_STATIC_ANALYSIS
# define tcg_debug_assert(X) do { assert(X); } while (0)
# define tcg_debug_check(X) tcg_debug_assert(X)
#else
# define tcg_debug_assert(X) \
    do { if (!(X)) { __builtin_unreachable(); } } while (0)
/* Diagnostic predicates can fetch contexts or call capability queries.
 * Type-check them without evaluating them in release builds. Unlike the
 * assertions above, these do not provide optimizer assumptions. */
# define tcg_debug_check(X) do { (void)sizeof(!!(X)); } while (0)
#endif

#endif
