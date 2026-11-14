/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif
typedef struct Error Error;
#include "qemu/notify.h"
/* NOTIFY */
/* EXIT */
static retro_atomic_int_t ready,total;
static __thread struct { Notifier notifier; uintptr_t owner; unsigned calls; } local;
static void callback(Notifier *n,void *unused) {
 (void)unused;
 assert(n==&local.notifier);
 assert(local.owner==sthread_get_current_thread_id());
 assert(!n->node.le_prev && !n->node.le_next);
 local.calls++;retro_atomic_inc_int(&total);
}
static void exercise(void *unused) {
 (void)unused;
 local.owner=sthread_get_current_thread_id();local.notifier.notify=callback;
 qemu_thread_atexit_add(&local.notifier);
 retro_atomic_inc_int(&ready);
 while(retro_atomic_load_acquire_int(&ready)<16)sthread_yield();
 qemu_thread_run_atexit();qemu_thread_run_atexit();assert(local.calls==1);
 qemu_thread_atexit_add(&local.notifier);qemu_thread_run_atexit();assert(local.calls==2);
 assert(!QLIST_FIRST(&thread_exit.notifiers));
}
#ifdef _WIN32
static DWORD WINAPI foreign(void *p) { exercise(p);return 0; }
#else
static void *foreign(void *p) { exercise(p);return NULL; }
#endif
int main(void) {
 sthread_t *workers[16];
 for(unsigned i=0;i<16;i++){workers[i]=sthread_create(exercise,NULL);assert(workers[i]);}
 for(unsigned i=0;i<16;i++)sthread_join(workers[i]);
 assert(retro_atomic_load_acquire_int(&total)==32);
 retro_atomic_store_relaxed_int(&ready,0);
#ifdef _WIN32
 HANDLE native[16];
 for(unsigned i=0;i<16;i++){native[i]=CreateThread(NULL,0,foreign,NULL,0,NULL);assert(native[i]);}
 assert(WaitForMultipleObjects(16,native,TRUE,10000)==WAIT_OBJECT_0);
 for(unsigned i=0;i<16;i++){DWORD code;assert(GetExitCodeThread(native[i],&code));assert(!code);CloseHandle(native[i]);}
#else
 pthread_t native[16];
 for(unsigned i=0;i<16;i++)assert(!pthread_create(&native[i],NULL,foreign,NULL));
 for(unsigned i=0;i<16;i++)assert(!pthread_join(native[i],NULL));
#endif
 assert(retro_atomic_load_acquire_int(&total)==64);
 local.owner=sthread_get_current_thread_id();local.notifier.notify=callback;
 qemu_thread_atexit_add(&local.notifier);qemu_thread_run_atexit();assert(local.calls==1);
 qemu_thread_atexit_add(&local.notifier);qemu_thread_atexit_remove(&local.notifier);
 qemu_thread_run_atexit();assert(local.calls==1);
 puts("Thread exit: 16 rthreads + 16 native foreign threads; owner isolation, drain/reuse/removal and host-owner cleanup PASS");
 return 0;
}
