/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <stdint.h>
typedef uintptr_t SOCKET;
typedef uintptr_t HANDLE;
#define FD_READ 1
#define FD_WRITE 2
#define FD_ACCEPT 8
#define FD_CONNECT 16
#define FD_CLOSE 32
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qemu/queue.h"
#define QLIST_RCU_ENTRY QLIST_ENTRY
#define QLIST_FOREACH_RCU QLIST_FOREACH
#define QLIST_FOREACH_SAFE_RCU QLIST_FOREACH_SAFE
#define QLIST_INSERT_HEAD_RCU QLIST_INSERT_HEAD
#define QLIST_SAFE_REMOVE_RCU QLIST_REMOVE
#define G_IO_IN 1
#define G_IO_OUT 4
typedef void IOHandler(void *);
typedef bool AioPollFn(void *);
typedef struct {HANDLE handle;} EventNotifier;
typedef void EventNotifierHandler(EventNotifier *);
typedef struct {SOCKET fd;int events,revents;} GPollFD;
typedef struct AioHandler AioHandler;
/* HANDLER */
typedef struct {unsigned list_lock;EventNotifier notifier;QLIST_HEAD(,AioHandler) aio_handlers;} AioContext;
static unsigned allocations,removals,notifications,errors,wrapped_selects,selects;
static long selected_mask;
static SOCKET selected_socket,raw_socket,wrapped_socket;
static int wrapped_fd;
static bool force_failure;
static void *allocate(size_t size) {void *p=calloc(1,size);assert(p);allocations++;return p;}
#define g_new0(t,n) ((t *)allocate(sizeof(t)*(n)))
#define g_free free
#define error_report(...) (errors++)
static void qemu_lockcnt_lock(unsigned *lock) {assert(!*lock);(*lock)++;}
static void qemu_lockcnt_unlock(unsigned *lock) {assert(*lock==1);(*lock)--;}
static void qemu_lockcnt_inc(unsigned *lock) {(*lock)++;}
static void qemu_lockcnt_dec(unsigned *lock) {assert(*lock);(*lock)--;}
static bool qemu_lockcnt_dec_if_lock(unsigned *lock) {assert(false);return false;}
static void qemu_lockcnt_inc_and_unlock(unsigned *lock) {assert(false);}
static HANDLE event_notifier_get_handle(EventNotifier *event) {return event->handle;}
static void aio_notify(AioContext *ctx) {notifications++;}
static void aio_remove_fd_handler(AioContext *ctx,AioHandler *node) {QLIST_REMOVE(node,node);free(node);removals++;}
static bool fd_is_socket(int fd) {return fd==wrapped_fd;}
static intptr_t test_oshandle(int fd) {assert(fd==wrapped_fd);return (intptr_t)wrapped_socket;}
#define _get_osfhandle test_oshandle
static int test_select(SOCKET socket,HANDLE event,long mask) {
    selects++;selected_socket=socket;selected_mask=mask;
    if(force_failure) {
#ifdef _WIN32
        WSASetLastError(WSAENOTSOCK);
#endif
        return -1;
    }
#ifdef _WIN32
    return WSAEventSelect(socket,event,mask);
#else
    return 0;
#endif
}
#define WSAEventSelect test_select
#ifndef _WIN32
#define WSAGetLastError() 10038
#endif
static void qemu_socket_select_nofail(int fd,HANDLE event,long mask) {wrapped_selects++;assert(!test_select((SOCKET)test_oshandle(fd),event,mask));}
/* PRODUCTION */
static unsigned callbacks;
static void callback(void *opaque) {assert(opaque==&callbacks);callbacks++;}
#ifdef _WIN32
static SOCKET bound_socket(struct sockaddr_in *address) {
    SOCKET socket=WSASocket(AF_INET,SOCK_DGRAM,0,NULL,0,0);assert(socket!=INVALID_SOCKET);
    *address=(struct sockaddr_in){.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
    assert(!bind(socket,(struct sockaddr *)address,sizeof(*address)));int length=sizeof(*address);
    assert(!getsockname(socket,(struct sockaddr *)address,&length));return socket;
}
static void transfer(AioContext *ctx,SOCKET socket,struct sockaddr_in *address) {
    char byte;assert(recv(socket,&byte,1,0)==SOCKET_ERROR && WSAGetLastError()==WSAEWOULDBLOCK);
    WSANETWORKEVENTS events;assert(!WSAEnumNetworkEvents(socket,ctx->notifier.handle,&events));
    SOCKET sender=WSASocket(AF_INET,SOCK_DGRAM,0,NULL,0,0);assert(sender!=INVALID_SOCKET);
    assert(sendto(sender,"X",1,0,(struct sockaddr *)address,sizeof(*address))==1);
    /* Only the standalone fixture waits, with a one-second deadline. */
    assert(WSAWaitForMultipleEvents(1,&ctx->notifier.handle,FALSE,1000,FALSE)==WSA_WAIT_EVENT_0);
    assert(aio_prepare(ctx) && aio_pending(ctx));unsigned before=callbacks;
    qemu_lockcnt_inc(&ctx->list_lock);assert(aio_dispatch_handlers(ctx,NULL));qemu_lockcnt_dec(&ctx->list_lock);
    assert(callbacks>before);assert(recv(socket,&byte,1,0)==1 && byte=='X');closesocket(sender);
}
#endif
int main(void) {
    AioContext ctx={0};
#ifdef _WIN32
    WSADATA data;assert(!WSAStartup(MAKEWORD(2,2),&data));ctx.notifier.handle=WSACreateEvent();assert(ctx.notifier.handle!=WSA_INVALID_EVENT);
    struct sockaddr_in addresses[2];raw_socket=bound_socket(&addresses[0]);wrapped_socket=bound_socket(&addresses[1]);
    /* fd classification/CRT translation are modelled; Winsock registration,
     * nonblocking status, readiness and callbacks use the real APIs. */
    wrapped_fd=3;
#else
    raw_socket=1001;wrapped_socket=100003;wrapped_fd=3;ctx.notifier.handle=1;
#endif
    int raw_fd=(int)(unsigned int)(uintptr_t)raw_socket;
    assert((SOCKET)(uintptr_t)(unsigned int)raw_fd==raw_socket);
    unsigned cases=0;
    for(unsigned wrapped=0;wrapped<2;wrapped++) for(unsigned mode=1;mode<4;mode++) {
        int fd=wrapped?wrapped_fd:raw_fd;unsigned old_wrapped=wrapped_selects,old_removals=removals;
        aio_set_fd_handler(&ctx,fd,(mode&1)?callback:NULL,(mode&2)?callback:NULL,NULL,NULL,&callbacks);
        AioHandler *node=QLIST_FIRST(&ctx.aio_handlers);assert(node && !QLIST_NEXT(node,node));
        assert(node->pfd.fd==(wrapped?wrapped_socket:raw_socket) && !ctx.list_lock);
        assert(node->pfd.events==((mode&1?G_IO_IN:0)|(mode&2?G_IO_OUT:0)));
        assert(selected_socket==node->pfd.fd && selected_mask==((mode&1?FD_READ|FD_ACCEPT|FD_CLOSE:0)|(mode&2?FD_WRITE|FD_CONNECT:0)));
        assert(wrapped_selects==old_wrapped+wrapped);
#ifdef _WIN32
        if(mode&1) transfer(&ctx,node->pfd.fd,&addresses[wrapped]);
#endif
        /* Replacing a node preserves one registration and updates callbacks. */
        aio_set_fd_handler(&ctx,fd,callback,NULL,NULL,NULL,&callbacks);
        node=QLIST_FIRST(&ctx.aio_handlers);assert(node && !QLIST_NEXT(node,node));assert(node->io_read==callback && !node->io_write);
        assert(removals==old_removals+1);
        if(!wrapped) {
            unsigned old_allocations=allocations,old_notifications=notifications;AioHandler *old=node;
            force_failure=true;aio_set_fd_handler(&ctx,fd,NULL,callback,NULL,NULL,&callbacks);force_failure=false;
            assert(QLIST_FIRST(&ctx.aio_handlers)==old && old->io_read==callback && !old->io_write);
            assert(allocations==old_allocations && notifications==old_notifications && !ctx.list_lock);
        }
        unsigned old_selects=selects;aio_set_fd_handler(&ctx,fd,NULL,NULL,NULL,NULL,NULL);
        assert(QLIST_EMPTY(&ctx.aio_handlers) && selects==old_selects && !ctx.list_lock);cases++;
    }
    force_failure=true;unsigned old_allocations=allocations,old_notifications=notifications;
    aio_set_fd_handler(&ctx,raw_fd,callback,NULL,NULL,NULL,&callbacks);
    assert(QLIST_EMPTY(&ctx.aio_handlers) && allocations==old_allocations && notifications==old_notifications && errors==4 && !ctx.list_lock);
#ifdef _WIN32
    force_failure=false;
    aio_set_fd_handler(&ctx,-1,callback,NULL,NULL,NULL,&callbacks);
    assert(QLIST_EMPTY(&ctx.aio_handlers) && allocations==old_allocations && notifications==old_notifications && errors==5 && !ctx.list_lock);
    closesocket(raw_socket);closesocket(wrapped_socket);WSACloseEvent(ctx.notifier.handle);WSACleanup();
    printf("Windows AIO: %u raw/wrapped registration/replacement/removal cases, real nonblocking UDP readiness/dispatch and failure preservation passed\n",cases);
#else
    printf("Windows AIO model: %u raw/wrapped registration/replacement/removal cases and failure preservation passed\n",cases);
#endif
}
