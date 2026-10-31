/*
 * Get host pc for helper unwinding.
 *
 * Copyright (c) 2003 Fabrice Bellard
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef ACCEL_TCG_GETPC_H
#define ACCEL_TCG_GETPC_H

/* GETPC is the true target of the return instruction that we'll execute.  */
# define GETPC() \
    ((uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0)))

#endif /* ACCEL_TCG_GETPC_H */
