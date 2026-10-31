/*
 * os-posix-lib.c
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2010 Red Hat, Inc.
 *
 * QEMU library functions on POSIX which are shared between QEMU and
 * the QEMU tools.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include <termios.h>

#include <glib/gprintf.h>

#include "system/system.h"
#include "trace.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/sockets.h"
#include <libgen.h>
#include "qemu/cutils.h"

#ifdef CONFIG_LINUX
#include <sys/syscall.h>
#endif
#ifdef __ANDROID__
#include <asm/unistd.h>
#endif

#ifdef __FreeBSD__
#include <sys/thr.h>
#include <sys/user.h>
#include <libutil.h>
#endif

#ifdef __NetBSD__
#include <lwp.h>
#endif

#include "qemu/memalign.h"
#include "qemu/mmap-alloc.h"

int qemu_get_thread_id(void)
{
#if defined(__ANDROID__) && defined(__NR_gettid)
    return syscall(__NR_gettid);
#elif defined(__linux__)
    return syscall(SYS_gettid);
#elif defined(__FreeBSD__)
    /* thread id is up to INT_MAX */
    long tid;
    thr_self(&tid);
    return (int)tid;
#elif defined(__NetBSD__)
    return _lwp_self();
#elif defined(__OpenBSD__)
    return getthrid();
#else
    return getpid();
#endif
}

/* alloc shared memory pages */
void *qemu_anon_ram_alloc(size_t size, uint64_t *alignment, bool shared,
                          bool noreserve)
{
    const uint32_t qemu_map_flags = (shared ? QEMU_MAP_SHARED : 0) |
                                    (noreserve ? QEMU_MAP_NORESERVE : 0);
    size_t align = QEMU_VMALLOC_ALIGN;
#ifndef EMSCRIPTEN
    void *ptr = qemu_ram_mmap(-1, size, align, qemu_map_flags, 0);

    if (ptr == MAP_FAILED) {
        return NULL;
    }
#else
    /*
     * qemu_ram_mmap is not implemented for Emscripten. Use qemu_memalign
     * for the anonymous allocation. noreserve is ignored as there is no swap
     * space on Emscripten, and shared is ignored as there is no other
     * processes on Emscripten.
     */
    void *ptr = qemu_memalign(align, size);
#endif

    if (alignment) {
        *alignment = align;
    }

    trace_qemu_anon_ram_alloc(size, ptr);
    return ptr;
}

void qemu_anon_ram_free(void *ptr, size_t size)
{
    trace_qemu_anon_ram_free(ptr, size);
#ifndef EMSCRIPTEN
    qemu_ram_munmap(-1, ptr, size);
#else
    /*
     * qemu_ram_munmap is not implemented for Emscripten and qemu_memalign
     * was used for the allocation. Use the corresponding freeing function
     * here.
     */
    qemu_vfree(ptr);
#endif
}

bool qemu_set_blocking(int fd, bool block, Error **errp)
{
    g_autoptr(GError) err = NULL;

    if (!g_unix_set_fd_nonblocking(fd, !block, &err)) {
        error_setg_errno(errp, errno,
                         "Can't set file descriptor %d %s: %s", fd,
                         block ? "blocking" : "non-blocking",
                         err->message);
        return false;
    }

    return true;
}

int socket_set_fast_reuse(int fd)
{
    int val = 1, ret;

    ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                     (const char *)&val, sizeof(val));

    assert(ret == 0);

    return ret;
}

void qemu_set_cloexec(int fd)
{
    int f;
    f = fcntl(fd, F_GETFD);
    assert(f != -1);
    f = fcntl(fd, F_SETFD, f | FD_CLOEXEC);
    assert(f != -1);
}

void qemu_clear_cloexec(int fd)
{
    int f;
    f = fcntl(fd, F_GETFD);
    assert(f != -1);
    f = fcntl(fd, F_SETFD, f & ~FD_CLOEXEC);
    assert(f != -1);
}

size_t qemu_get_host_physmem(void)
{
#ifdef _SC_PHYS_PAGES
    long pages = sysconf(_SC_PHYS_PAGES);
    if (pages > 0) {
        if (pages > SIZE_MAX / qemu_real_host_page_size()) {
            return SIZE_MAX;
        } else {
            return pages * qemu_real_host_page_size();
        }
    }
#endif
    return 0;
}

int qemu_shm_alloc(size_t size, Error **errp)
{
#ifdef __ANDROID__
    g_autofree char *path = g_strdup_printf("%s/qemu-shm-XXXXXX",
                                            g_get_tmp_dir());
    int fd = mkstemp(path);

    if (fd < 0) {
        error_setg_errno(errp, errno,
                         "failed to create temporary shared memory file");
        return -1;
    }

    unlink(path);

    if (ftruncate(fd, size) == -1) {
        error_setg_errno(errp, errno,
                         "failed to resize temporary shared memory to %zu",
                         size);
        close(fd);
        return -1;
    }

    return fd;
#else
    g_autoptr(GString) shm_name = g_string_new(NULL);
    int fd, oflag, cur_sequence;
    static retro_atomic_int_t sequence;
    mode_t mode;

    cur_sequence = retro_atomic_fetch_add_int(&sequence, 1);

    /*
     * Let's use `mode = 0` because we don't want other processes to open our
     * memory unless we share the file descriptor with them.
     */
    mode = 0;
    oflag = O_RDWR | O_CREAT | O_EXCL;

    /*
     * Some operating systems allow creating anonymous POSIX shared memory
     * objects (e.g. FreeBSD provides the SHM_ANON constant), but this is not
     * defined by POSIX, so let's create a unique name.
     *
     * From Linux's shm_open(3) man-page:
     *   For  portable  use,  a shared  memory  object should be identified
     *   by a name of the form /somename;"
     */
    g_string_printf(shm_name, "/qemu-" FMT_pid "-shm-%d", getpid(),
                    cur_sequence);

    fd = shm_open(shm_name->str, oflag, mode);
    if (fd < 0) {
        error_setg_errno(errp, errno,
                         "failed to create POSIX shared memory");
        return -1;
    }

    /*
     * We have the file descriptor, so we no longer need to expose the
     * POSIX shared memory object. However it will remain allocated as long as
     * there are file descriptors pointing to it.
     */
    shm_unlink(shm_name->str);

    if (ftruncate(fd, size) == -1) {
        error_setg_errno(errp, errno,
                         "failed to resize POSIX shared memory to %zu", size);
        close(fd);
        return -1;
    }

    return fd;
#endif
}
