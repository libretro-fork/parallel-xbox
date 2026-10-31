/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "qemu/win32-tls.h"
typedef struct Error Error;
#include "qemu/notify.h"
#define g_new0(t,n) ((t *)calloc((n),sizeof(t)))
#define g_free free
typedef struct QemuThreadData { NotifierList exit; } QemuThreadData;
/* NOTIFY */
/* EXIT */
static volatile LONG ready,total;
static __thread struct { Notifier notifier; DWORD owner; unsigned calls; } local;
static void callback(Notifier *n,void *unused) {
 assert(n==&local.notifier);
 assert(local.owner==GetCurrentThreadId());
 assert(!n->node.le_prev && !n->node.le_next);
 local.calls++;InterlockedIncrement(&total);
}
static DWORD WINAPI foreign(void *unused) {
 local.owner=GetCurrentThreadId();local.notifier.notify=callback;
 qemu_thread_atexit_add(&local.notifier);
 InterlockedIncrement(&ready);
 while(ready<16)SwitchToThread();
 qemu_thread_run_atexit();qemu_thread_run_atexit();
 assert(local.calls==1);
 /* Reuse on the same host thread after core deinit/init. */
 qemu_thread_atexit_add(&local.notifier);qemu_thread_run_atexit();
 assert(local.calls==2);
 assert(!qemu_win32_tls_get(&tls_key_host_thread_exit));
 return 0;
}
int main(void) {
 HANDLE threads[16];
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 for(unsigned i=0;i<16;i++){threads[i]=CreateThread(NULL,0,foreign,NULL,0,NULL);assert(threads[i]);}
 assert(WaitForMultipleObjects(16,threads,TRUE,10000)==WAIT_OBJECT_0);
 for(unsigned i=0;i<16;i++){DWORD code;assert(GetExitCodeThread(threads[i],&code));assert(!code);CloseHandle(threads[i]);}
 assert(total==32);
 QemuThreadData owned={0};set_qemu_thread_data(&owned);
 local.owner=GetCurrentThreadId();local.notifier.notify=callback;
 qemu_thread_atexit_add(&local.notifier);qemu_thread_run_atexit();
 assert(local.calls==1 && !QLIST_FIRST(&owned.exit.notifiers));
 set_qemu_thread_data(NULL);
 qemu_thread_atexit_add(&local.notifier);qemu_thread_atexit_remove(&local.notifier);
 qemu_thread_run_atexit();assert(local.calls==1);
 puts("Windows thread exit: 16 isolated foreign threads, TLS lifetime, reuse, removal and owned-thread cleanup PASS");
 return 0;
}
