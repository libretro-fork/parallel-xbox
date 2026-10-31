/*
 * Asynchronous teardown
 *
 * Copyright IBM, Corp. 2022
 *
 * Authors:
 *  Claudio Imbrenda <imbrenda@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version.  See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include <dirent.h>
#include <sys/prctl.h>
#include <sched.h>

#include "qemu/async-teardown.h"

#ifdef _SC_THREAD_STACK_MIN
#define CLONE_STACK_SIZE sysconf(_SC_THREAD_STACK_MIN)
#else
#define CLONE_STACK_SIZE 16384
#endif

