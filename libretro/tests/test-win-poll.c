/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#ifdef _WIN32
#include <windows.h>
#else
#define WIN_POLL_MOCK 1
#define _WIN32 1
#define MAXIMUM_WAIT_OBJECTS 64
#define WAIT_OBJECT_0 0u
#define WAIT_TIMEOUT 258u
#define WAIT_IO_COMPLETION 192u
#define WAIT_FAILED 0xffffffffu
#define INFINITE 0xffffffffu
#define TRUE 1
#define FALSE 0
#define HANDLE uintptr_t
#define DWORD unsigned
#endif
typedef int gint;
typedef unsigned guint;
typedef unsigned short gushort;
typedef intptr_t gintptr;
/* POLLTYPE */
#define G_IO_IN 1
static unsigned copies;
static __attribute__((unused)) void *counted_memcpy(void *dst, const void *src, size_t n)
{
    copies++;
    return memcpy(dst, src, n);
}
#define memcpy counted_memcpy
#define g_newa(t,n) ((t *)__builtin_alloca(sizeof(t)*(n)))
#ifdef WIN_POLL_MOCK
static bool signal_state[128], automatic[128];
static unsigned mode;
static gint last_timeout;


static DWORD WaitForSingleObject(HANDLE h, DWORD timeout)
{
    (void)timeout;
    assert(h && h < 128);
    if (!signal_state[h]) return WAIT_TIMEOUT;
    if (automatic[h]) signal_state[h] = false;
    return WAIT_OBJECT_0;
}
static DWORD WaitForMultipleObjectsEx(DWORD n, const HANDLE *h, int all,
                                      DWORD timeout, int alertable)
{
    assert(n && n <= 64 && !all && alertable);
    last_timeout = (gint)timeout;
    if (mode == 1) return WAIT_FAILED;
    if (mode == 2) return WAIT_IO_COMPLETION;
    for (unsigned i = 0; i < n; i++) {
        if (WaitForSingleObject(h[i], 0) == WAIT_OBJECT_0)
            return WAIT_OBJECT_0 + i;
    }
    return WAIT_TIMEOUT;
}
#endif

/* POLL */
int main(void)
{
#ifdef WIN_POLL_MOCK
    unsigned cases=0;
    const int waits[]={-1,0,1,1000000,INT32_MAX};
    for(unsigned n=0;n<=80;n++)for(unsigned w=0;w<5;w++)for(mode=0;mode<3;mode++){
        GPollFD fds[82];unsigned expected=0;
        memset(signal_state,0,sizeof(signal_state));memset(automatic,0,sizeof(automatic));
        fds[0]=(GPollFD){99,3,77};fds[n+1]=fds[0];
        for(unsigned i=0;i<n;i++){
            fds[i+1]=(GPollFD){i+1,G_IO_IN,0xffff};signal_state[i+1]=(i%3)==0;
            if(i<64 && signal_state[i+1])expected++;
        }
        unsigned before=copies;int actual=g_poll(&fds[1],n,waits[w]);
        assert(actual==(mode==1&&n?-1:(int)expected));assert(copies==before);
        assert(fds[0].revents==77&&fds[n+1].revents==77);
        if(n)assert(last_timeout==(waits[w]<0?-1:0));
        for(unsigned i=0;i<n;i++)assert(fds[i+1].revents==((mode!=1&&i<64&&signal_state[i+1])?G_IO_IN:0));
        cases++;
    }
    printf("Windows poll: %u notification/readiness/positive-duration/error/APC/handle-limit cases passed\n",cases);
#else
    HANDLE event=CreateEvent(NULL,TRUE,TRUE,NULL);assert(event);
    GPollFD p[3]={{77,3,9},{(gintptr)event,G_IO_IN,0xffff},{88,4,7}};
    const int waits[]={-1,0,1,INT32_MAX};
    for(unsigned i=0;i<4;i++){assert(g_poll(&p[1],1,waits[i])==1);assert(p[1].revents==G_IO_IN);}
    ResetEvent(event);assert(g_poll(&p[1],1,INT32_MAX)==0&&!p[1].revents);
    assert(g_poll(NULL,0,INT32_MAX)==0);
    assert(p[0].revents==9&&p[2].revents==7);
    HANDLE handles[64];GPollFD fds[64];
    for(unsigned i=0;i<64;i++){handles[i]=CreateEvent(NULL,TRUE,i==63,NULL);assert(handles[i]);fds[i]=(GPollFD){(gintptr)handles[i],G_IO_IN,0xffff};}
    assert(g_poll_with_handle(fds,64,0,(gintptr)event)==1&&fds[63].revents==G_IO_IN);
    for(unsigned i=0;i<64;i++){CloseHandle(handles[i]);}
    CloseHandle(event);
    puts("Native Windows notification poll: signaled/unsignaled events, positive-duration readiness, canaries and 64 descriptors passed");
#endif
    return 0;
}
