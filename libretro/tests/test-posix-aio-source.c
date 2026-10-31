/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <glib.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include "qemu/rcu_queue.h"
typedef struct Error Error;
typedef void IOHandler(void *);
typedef bool AioPollFn(void *);
typedef struct AioHandler AioHandler;
/* HANDLER */
typedef QLIST_RCU_HEAD(,AioHandler) AioHandlerList;
typedef struct AioContext {
 GSource source;uintptr_t handler_owner_id;unsigned handler_walk_depth;
 int notifier;AioHandlerList aio_handlers,deleted_aio_handlers;
} AioContext;
static uintptr_t sthread_get_current_thread_id(void){return (uintptr_t)pthread_self();}
/* OWNER HELPERS */
static unsigned frees,notifications;
static void release(void *ptr){frees++;g_free(ptr);}
#define g_free release
static void aio_notify(AioContext *ctx){notifications++;}
static int aio_bh_poll(AioContext *ctx){return 0;}
/* PRODUCTION */
static gboolean prepare(GSource *s,gint *timeout){*timeout=-1;return aio_prepare((AioContext*)s);}
static gboolean check(GSource *s){return aio_pending((AioContext*)s);}
static gboolean dispatch_source(GSource *s,GSourceFunc f,gpointer data){aio_dispatch((AioContext*)s);return G_SOURCE_CONTINUE;}
static void finalize(GSource *s){aio_context_destroy((AioContext*)s);}
static GSourceFuncs funcs={.prepare=prepare,.check=check,.dispatch=dispatch_source,.finalize=finalize};
static GMainContext *main_context;
static AioContext *ctx;
static int pipes[2];static unsigned first_calls,second_calls;
static void nested_second(void *opaque){
 char b;assert(opaque==ctx&&ctx->handler_walk_depth==2);assert(read(pipes[0],&b,1)==1&&b=='B');
 second_calls++;aio_set_fd_handler(ctx,pipes[0],NULL,NULL,NULL,NULL,NULL);assert(!frees);
}
static void nested_first(void *opaque){
 char b;assert(opaque==ctx&&ctx->handler_walk_depth==1);assert(read(pipes[0],&b,1)==1&&b=='A');first_calls++;
 aio_set_fd_handler(ctx,pipes[0],nested_second,NULL,NULL,NULL,ctx);assert(!frees);
 assert(write(pipes[1],"B",1)==1);assert(g_main_context_iteration(main_context,FALSE));
 assert(second_calls==1&&!frees&&ctx->handler_walk_depth==1);
}
static void readable_once(void *opaque){
 int fd=(int)(intptr_t)opaque;char b;assert(read(fd,&b,1)==1&&b=='X');second_calls++;
 aio_set_fd_handler(ctx,fd,NULL,NULL,NULL,NULL,NULL);
}
static void writable_once(void *opaque){
 assert(write(pipes[1],"W",1)==1);first_calls++;aio_set_fd_handler(ctx,pipes[1],NULL,NULL,NULL,NULL,NULL);
}
static void hung_up(void *opaque){
 char b;assert(read(pipes[0],&b,1)==0);first_calls++;aio_set_fd_handler(ctx,pipes[0],NULL,NULL,NULL,NULL,NULL);
}
static void make_pipe(int p[2]){assert(!pipe(p));assert(fcntl(p[0],F_SETFL,O_NONBLOCK)==0);assert(fcntl(p[1],F_SETFL,O_NONBLOCK)==0);}
int main(void){
 main_context=g_main_context_new();ctx=(AioContext*)g_source_new(&funcs,sizeof(*ctx));
 ctx->handler_owner_id=sthread_get_current_thread_id();assert(aio_context_setup(ctx,NULL));
 g_source_set_can_recurse(&ctx->source,TRUE);g_source_attach(&ctx->source,main_context);
 for(unsigned n=0;n<100;n++){
  make_pipe(pipes);frees=first_calls=second_calls=0;
  aio_set_fd_handler(ctx,pipes[0],nested_first,NULL,NULL,NULL,ctx);assert(aio_has_fd_handlers(ctx));
  g_main_context_iteration(main_context,FALSE);assert(!first_calls&&!second_calls);
  assert(write(pipes[1],"A",1)==1);assert(g_main_context_iteration(main_context,TRUE));
  assert(first_calls==1&&second_calls==1&&frees==2&&!ctx->handler_walk_depth&&!aio_has_fd_handlers(ctx));
  close(pipes[0]);close(pipes[1]);
 }
 make_pipe(pipes);first_calls=0;aio_set_fd_handler(ctx,pipes[1],NULL,writable_once,NULL,NULL,ctx);
 assert(g_main_context_iteration(main_context,TRUE));assert(first_calls==1);char byte;assert(read(pipes[0],&byte,1)==1&&byte=='W');
 aio_set_fd_handler(ctx,pipes[0],hung_up,NULL,NULL,NULL,ctx);close(pipes[1]);
 assert(g_main_context_iteration(main_context,TRUE));assert(first_calls==2);close(pipes[0]);
 enum{N=96};int many[N][2];second_calls=0;frees=0;
 for(unsigned i=0;i<N;i++){make_pipe(many[i]);aio_set_fd_handler(ctx,many[i][0],readable_once,NULL,NULL,NULL,(void*)(intptr_t)many[i][0]);assert(write(many[i][1],"X",1)==1);}
 while(second_calls<N)assert(g_main_context_iteration(main_context,TRUE));
 assert(second_calls==N&&frees==N&&QLIST_EMPTY_RCU(&ctx->aio_handlers)&&QLIST_EMPTY_RCU(&ctx->deleted_aio_handlers));
 for(unsigned i=0;i<N;i++){close(many[i][0]);close(many[i][1]);}
 g_source_destroy(&ctx->source);g_source_unref(&ctx->source);g_main_context_unref(main_context);
 puts("POSIX AIO/GSource: real pipes, 100 nested replacements/removals, idle readiness, writable/HUP events and 96 descriptors PASS");return 0;
}