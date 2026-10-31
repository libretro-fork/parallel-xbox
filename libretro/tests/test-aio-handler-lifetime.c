/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "qemu/rcu_queue.h"
#define G_IO_IN 1
#define G_IO_OUT 4
#define G_IO_ERR 8
#define G_IO_HUP 16
typedef uintptr_t HANDLE;
typedef void IOHandler(void *);
typedef bool AioPollFn(void *);
typedef struct {HANDLE handle;} EventNotifier;
typedef void EventNotifierHandler(EventNotifier *);
typedef struct {uintptr_t fd;int events,revents;} GPollFD;
typedef struct {int64_t ns;} AioPolledEvent;
typedef struct AioHandler AioHandler;
/* HANDLER */
typedef QLIST_RCU_HEAD(,AioHandler) AioHandlerList;
typedef struct {
 uintptr_t handler_owner_id;unsigned handler_walk_depth;
 EventNotifier notifier;int source;bool poll_started;
 AioHandlerList aio_handlers,deleted_aio_handlers,poll_aio_handlers;
} AioContext;
static uintptr_t sthread_get_current_thread_id(void){return 1;}
/* OWNER HELPERS */
static unsigned freed,calls[4];
static void release(AioHandler *node){memset(node,0xcc,sizeof(*node));free(node);freed++;}
#define g_free release
static bool g_source_is_destroyed(int *source){return false;}
static void g_source_remove_poll(int *source,GPollFD *pfd){}
static HANDLE event_notifier_get_handle(EventNotifier *e){return e->handle;}
typedef struct {long lNetworkEvents;} WSANETWORKEVENTS;
static void WSAEnumNetworkEvents(uintptr_t fd,HANDLE event,WSANETWORKEVENTS *ev){ev->lNetworkEvents=0;}
#define trace_poll_add(...) ((void)0)
/* PRODUCTION */
static AioContext ctx;
static AioHandler *nodes[4];
static void dispatch(void);
static void remove_node(AioHandler *node){
#ifdef CASE_WIN32
 aio_remove_fd_handler(&ctx,node);
#else
 if(aio_remove_fd_handler(&ctx,node))release(node);
#endif
}
static void callback(void *opaque){
 unsigned id=(unsigned)(uintptr_t)opaque;calls[id]++;
 assert(id!=2); /* The outer callback deletes the next callback before it runs. */
 if(id==1){
  assert(ctx.handler_walk_depth==1);remove_node(nodes[1]);remove_node(nodes[2]);
  assert(!freed);dispatch();assert(!freed&&ctx.handler_walk_depth==1);
 }else{
  assert(id==3&&ctx.handler_walk_depth==2);remove_node(nodes[3]);assert(!freed);
 }
}
static AioHandler *new_node(unsigned id){
 AioHandler *n=calloc(1,sizeof(*n));assert(n);n->io_read=callback;n->opaque=(void*)(uintptr_t)id;
 n->pfd.events=n->pfd.revents=G_IO_IN;
 QLIST_INSERT_HEAD_RCU(&ctx.aio_handlers,n,node);return n;
}
static void dispatch(void){
 aio_handler_walk_begin(&ctx);
#ifdef CASE_WIN32
 aio_dispatch_handlers(&ctx,0);
#else
 AioHandler *n,*next;QLIST_FOREACH_SAFE_RCU(n,&ctx.aio_handlers,node,next)aio_dispatch_handler(&ctx,n);
 aio_free_deleted_handlers(&ctx);
#endif
 aio_handler_walk_end(&ctx);
}
int main(void){
 ctx.handler_owner_id=sthread_get_current_thread_id();ctx.notifier.handle=1;
 for(unsigned step=0;step<10000;step++){
  freed=0;memset(calls,0,sizeof(calls));
  for(unsigned id=3;id;id--)nodes[id]=new_node(id);
  dispatch();assert(!ctx.handler_walk_depth&&freed==3);
  assert(calls[1]==1&&calls[2]==0&&calls[3]==1);assert(QLIST_EMPTY_RCU(&ctx.aio_handlers));
  AioHandler *idle=new_node(3);remove_node(idle);assert(freed==4&&QLIST_EMPTY_RCU(&ctx.aio_handlers));
#ifndef CASE_WIN32
  /* Context teardown may reclaim an already-deleted handler with no walker. */
  AioHandler *retired=new_node(3);aio_handler_walk_begin(&ctx);remove_node(retired);aio_handler_walk_end(&ctx);
  assert(freed==4);aio_free_deleted_handlers(&ctx);assert(freed==5&&QLIST_EMPTY_RCU(&ctx.aio_handlers));
#endif
 }
#ifdef CASE_WIN32
 puts("Windows AIO: 10000 nested callback deletion schedules, self/next deletion, deferred free and idle removal PASS");
#else
 puts("POSIX AIO: 10000 nested callback deletion schedules, self/next deletion, deferred free, idle removal and teardown PASS");
#endif
 return 0;
}