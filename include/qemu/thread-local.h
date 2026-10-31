/* Copyright Red Hat
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * Ordinary thread-local storage. Windows keys belong to the loaded module;
 * per-thread addresses remain stable for registered RCU readers.
 */
#ifndef QEMU_THREAD_LOCAL_H
#define QEMU_THREAD_LOCAL_H
#ifdef _WIN32
#include "qemu/win32-tls.h"
#endif

#define QEMU_DECLARE_THREAD_LOCAL(type, var)                                \
    type get_##var(void);                                                   \
    void set_##var(type v);                                                 \
    type *get_ptr_##var(void);

#if defined(_WIN32)
#define QEMU_DEFINE_THREAD_LOCAL(type, var)                                 \
    static QemuWin32TLS thread_local_key_##var = QEMU_WIN32_TLS_INITIALIZER;\
    static inline type *thread_local_getptr_##var(void) {                   \
        type *p = (type *)qemu_win32_tls_get(&thread_local_key_##var);      \
        if (!p) {                                                           \
            p = g_malloc0(sizeof(type));                                    \
            qemu_win32_tls_set(&thread_local_key_##var, p);                 \
        }                                                                   \
        return p;                                                           \
    }                                                                       \
    type get_##var(void) { return *thread_local_getptr_##var(); }           \
    void set_##var(type v) { *thread_local_getptr_##var() = v; }            \
    type *get_ptr_##var(void) { return thread_local_getptr_##var(); }
#else
#define QEMU_DEFINE_THREAD_LOCAL(type, var)                                 \
    static __thread type thread_local_##var;                                \
    type get_##var(void) { return thread_local_##var; }                     \
    void set_##var(type v) { thread_local_##var = v; }                      \
    type *get_ptr_##var(void)                                               \
    { type *ptr = &thread_local_##var; return ptr; }
#endif

#if defined(_WIN32)
#define QEMU_DEFINE_STATIC_THREAD_LOCAL(type, var)                          \
    static QemuWin32TLS thread_local_key_s_##var = QEMU_WIN32_TLS_INITIALIZER;\
    static inline type *thread_local_getptr_s_##var(void) {                 \
        type *p = (type *)qemu_win32_tls_get(&thread_local_key_s_##var);    \
        if (!p) {                                                           \
            p = g_malloc0(sizeof(type));                                    \
            qemu_win32_tls_set(&thread_local_key_s_##var, p);               \
        }                                                                   \
        return p;                                                           \
    }                                                                       \
    static __attribute__((unused))                                          \
    type get_##var(void) { return *thread_local_getptr_s_##var(); }         \
    static __attribute__((unused))                                          \
    void set_##var(type v) { *thread_local_getptr_s_##var() = v; }          \
    static __attribute__((unused))                                          \
    type *get_ptr_##var(void) { return thread_local_getptr_s_##var(); }
#else
#define QEMU_DEFINE_STATIC_THREAD_LOCAL(type, var)                          \
    static __thread type thread_local_##var;                                \
    static __attribute__((unused))                                          \
    type get_##var(void)                                                    \
    { return thread_local_##var; }                                          \
    static __attribute__((unused))                                          \
    void set_##var(type v)                                                  \
    { thread_local_##var = v; }                                             \
    static __attribute__((unused))                                          \
    type *get_ptr_##var(void)                                               \
    { type *ptr = &thread_local_##var; return ptr; }
#endif

#endif /* QEMU_THREAD_LOCAL_H */
