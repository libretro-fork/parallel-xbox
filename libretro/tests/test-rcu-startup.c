/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <retro_atomic.h>
#ifdef _WIN32
#include <windows.h>
typedef HANDLE Thread;
#define THREAD_FN DWORD WINAPI
#define YIELD() SwitchToThread()
static uint64_t now_ms(void) { return GetTickCount64(); }
static Thread spawn(LPTHREAD_START_ROUTINE fn,void *arg) { Thread t=CreateThread(NULL,0,fn,arg,0,NULL);assert(t);return t; }
static void join(Thread t) { assert(WaitForSingleObject(t,5000)==WAIT_OBJECT_0);CloseHandle(t); }
#else
#include <pthread.h>
#include <sched.h>
#include <time.h>
typedef pthread_t Thread;
#define THREAD_FN void *
#define YIELD() sched_yield()
static uint64_t now_ms(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000; }
static Thread spawn(void *(*fn)(void *),void *arg) {Thread t;assert(!pthread_create(&t,NULL,fn,arg));return t;}
static void join(Thread t) {assert(!pthread_join(t,NULL));}
#endif
typedef size_t gsize;
typedef uintptr_t guintptr;
typedef int gboolean;
#define TRUE 1
#define FALSE 0
#define g_assert assert
#define g_new(type, count) ((type *)malloc(sizeof(type) * (count)))
#define g_free free
static unsigned init_calls,barrier_calls,init_entered,init_release,init_done;
static unsigned first_read,queue_release,callbacks,blocked,returned;
static bool concurrent;
static _Thread_local unsigned consumer_id;
static _Thread_local bool paused,waited;
#define LOAD(p) __atomic_load_n((p),__ATOMIC_ACQUIRE)
#define STORE(p,v) __atomic_store_n((p),(v),__ATOMIC_RELEASE)
#define INC(p) (__atomic_add_fetch((p),1,__ATOMIC_ACQ_REL))
static void await(unsigned *p,unsigned n) {uint64_t end=now_ms()+5000;while(LOAD(p)<n){assert(now_ms()<end);YIELD();}}
static void retro_sleep(unsigned ms) {if(concurrent&&!waited){waited=true;INC(&blocked);}YIELD();}
/* ONCE */
struct rcu_head {retro_atomic_ptr_t next;void (*func)(struct rcu_head *);};
static void queue_read_hook(struct rcu_head *next) {
 if(consumer_id==1 && next && !paused){paused=true;STORE(&first_read,1);await(&queue_release,1);}
}
static void queue_abort(void) { fprintf(stderr,"RCU queue empty-state abort (reported crash)\n");abort(); }
#define abort queue_abort
/* QUEUE */
#undef abort
static struct rcu_head callback_node;
static Thread consumers[2];
static void callback(struct rcu_head *node) {assert(node==&callback_node);INC(&callbacks);}
static THREAD_FN consume(void *arg) {
#if HAVE_CLAIM
 if(!rcu_claim_callback_worker())return 0;
#endif
 consumer_id=(unsigned)(uintptr_t)arg;
 struct rcu_head *n=try_dequeue();assert(n);n->func(n);return 0;
}
static void smp_mb_global_init(void) {INC(&barrier_calls);}
static void rcu_init_complete(bool register_self) {
 unsigned n=INC(&init_calls);
 if(concurrent){STORE(&init_entered,1);await(&init_release,1);STORE(&init_done,1);}
 else {assert(n<=2);consumers[n-1]=spawn(consume,(void *)(uintptr_t)n);}
}
static int atfork_depth=1;
static uintptr_t registry[4];
static struct { bool registered; } child_reader;
static inline typeof(child_reader) *get_ptr_rcu_reader(void) { return &child_reader; }
/* INIT */
/* CHILD */
static THREAD_FN initialize(void *arg) {rcu_init();assert(LOAD(&init_done));INC(&returned);return 0;}
int main(int argc,char **argv) {
#ifdef _WIN32
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
#endif
 assert(argc==2);
 if(!strcmp(argv[1],"nested")) {
  static gsize locations[65]; /* The old 64-slot table aliases these. */
  assert(g_once_init_enter(&locations[0]));
  assert(g_once_init_enter(&locations[64]));
  g_once_init_leave(&locations[64],2);
  g_once_init_leave(&locations[0],1);
  assert(!g_once_init_enter(&locations[0]));
  assert(!g_once_init_enter(&locations[64]));
  puts("PASS: nested independent once locations cannot deadlock on a hash collision");return 0;
 }
 if(!strcmp(argv[1],"worker")) {
  Thread racers[16];callback_node.func=callback;enqueue(&callback_node);
  for(unsigned i=0;i<16;i++)racers[i]=spawn(consume,(void *)(uintptr_t)1);
  await(&first_read,1);STORE(&queue_release,1);
  for(unsigned i=0;i<16;i++)join(racers[i]);
  assert(LOAD(&callbacks)==1 && !try_dequeue());
  atfork_depth=0;rcu_init_child();assert(LOAD(&init_calls)==0);
  atfork_depth=1;memset(registry,0xff,sizeof(registry));enqueue(&callback_node);
  rcu_init_child();join(consumers[0]);
  assert(LOAD(&init_calls)==1 && LOAD(&callbacks)==2 && !try_dequeue());
  for(unsigned i=0;i<4;i++)assert(!registry[i]);
  puts("PASS: 16 competing workers have one queue consumer; fork child replaces its owner");return 0;
 }
 concurrent=!strcmp(argv[1],"concurrent");
 if(concurrent){
  Thread racers[17];racers[0]=spawn(initialize,NULL);await(&init_entered,1);
  for(unsigned i=1;i<17;i++)racers[i]=spawn(initialize,NULL);
  uint64_t end=now_ms()+5000;
  while(LOAD(&blocked)<16 && LOAD(&init_calls)==1){assert(now_ms()<end);YIELD();}
  bool one=LOAD(&init_calls)==1 && LOAD(&returned)==0;
  STORE(&init_release,1);for(unsigned i=0;i<17;i++)join(racers[i]);
  if(!one||LOAD(&init_calls)!=1){fprintf(stderr,"FAIL: simultaneous initialization created %u workers\n",LOAD(&init_calls));return 1;}
  assert(LOAD(&returned)==17);
 } else {
  callback_node.func=callback;enqueue(&callback_node);
  rcu_init();await(&first_read,1);
  rcu_init();
  if(LOAD(&init_calls)>1){join(consumers[1]);assert(LOAD(&callbacks)==1);}
  /* With two consumers the first now hits the reported empty-state abort. */
  STORE(&queue_release,1);join(consumers[0]);
  assert(LOAD(&init_calls)==1 && LOAD(&callbacks)==1 && !try_dequeue());
 }
 for(unsigned i=0;i<100000;i++)rcu_init();
 assert(LOAD(&init_calls)==1 && LOAD(&barrier_calls)==1);
 printf("PASS: RCU %s initialization has one consumer, published once, 100000 replays\n",argv[1]);return 0;
}
