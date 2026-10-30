/*
 * Get user helper pc for memory unwinding.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef ACCEL_TCG_HELPER_RETADDR_H
#define ACCEL_TCG_HELPER_RETADDR_H

/*
 * For user-only, helpers that use guest to host address translation
 * must protect the actual host memory access by recording 'retaddr'
 * for the signal handler.  This is required for a race condition in
 * which another thread unmaps the page between a probe and the
 * actual access.
 */
#define set_helper_retaddr(ra)   do { } while (0)
#define clear_helper_retaddr()   do { } while (0)

#endif /* ACCEL_TCG_HELPER_RETADDR_H */
