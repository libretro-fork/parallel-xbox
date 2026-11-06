/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
static unsigned locks,conds;
slock_t *slock_new(void) {locks++;return (slock_t*)malloc(1);}
void slock_free(slock_t*p) {free(p);}
void slock_lock(slock_t*p) {(void)p;abort();}
void slock_unlock(slock_t*p) {(void)p;abort();}
scond_t *scond_new(void) {conds++;return (scond_t*)malloc(1);}
void scond_free(scond_t*p) {free(p);}
int scond_broadcast(scond_t*p) {(void)p;abort();}
void scond_wait(scond_t*p,slock_t*q) {(void)p;(void)q;abort();}
bool scond_wait_timeout(scond_t*p,slock_t*q,int64_t t) {(void)p;(void)q;(void)t;abort();}
int main(void)
{
    retro_eventcount_t e;
    assert(!retro_eventcount_init_lock_free(NULL));
    assert(!retro_eventcount_init_lock_free(&e));
    assert(!locks && !conds);
    assert(retro_eventcount_init(&e));
    assert(locks==1 && conds==1);retro_eventcount_free(&e);
    puts("Eventcount: lock-free-only init refuses fallback before creating locks; legacy init preserved");
}
