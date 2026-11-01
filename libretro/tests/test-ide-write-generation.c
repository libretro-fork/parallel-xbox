/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <retro_atomic.h>
#define BUSY_STAT 0x80
#define BDRV_SECTOR_SIZE 512
#define IDE_RETRY_PIO 1
#define QEMU_CLOCK_VIRTUAL 0
#define NANOSECONDS_PER_SECOND 1000000000
struct BlockBackend {retro_atomic_64_t guest_write_generation;};typedef struct BlockBackend BlockBackend;
static BlockBackend *state_disk;
static BlockBackend *lr_state_hdd(void){return state_disk;}
typedef struct IDEState IDEState;
struct IDEState {BlockBackend *blk;void *pio_aiocb,*bus,*sector_write_timer;unsigned status;int nsector,req_nb_sectors;bool win2k_install_hack;unsigned irq_count;uint8_t io_buffer[8192];uint64_t sector;};
static bool stop_error;static unsigned errors,irqs,stops,starts,timers;static size_t start_bytes;
static int ide_handle_rw_error(IDEState *s,int error,int op){(void)s;assert(error>0&&op==IDE_RETRY_PIO);errors++;return stop_error;}
static void ide_set_sector(IDEState *s,uint64_t sector){s->sector=sector;}
static uint64_t ide_get_sector(IDEState *s){return s->sector;}
static void ide_transfer_stop(IDEState *s){(void)s;stops++;}
static void ide_sector_write(IDEState *s){(void)s;}
static void ide_transfer_start(IDEState *s,void *buffer,size_t bytes,void (*callback)(IDEState*)){assert(buffer==s->io_buffer&&callback==ide_sector_write);starts++;start_bytes=bytes;}
static void ide_bus_set_irq(void *bus){(void)bus;irqs++;}
static int64_t qemu_clock_get_ns(unsigned type){assert(type==0);return 100;}
static void timer_mod(void *timer,int64_t when){(void)timer;assert(when==100+1000000);timers++;}
/* PRODUCTION */
int main(void)
{
 BlockBackend blk;retro_atomic_store_relaxed_64(&blk.guest_write_generation,0);assert(lr_state_hdd_writes()==0);state_disk=&blk;
 unsigned cases=0;uint64_t expected=0;
 for(unsigned fail=0;fail<3;fail++)for(unsigned n=1;n<=64;n++)for(unsigned chunk=1;chunk<=16;chunk++)for(unsigned hack=0;hack<2;hack++){
  IDEState s={.blk=&blk,.pio_aiocb=&blk,.status=BUSY_STAT|3,.nsector=n,.req_nb_sectors=chunk,.win2k_install_hack=hack,.irq_count=15,.sector=123};
  stop_error=fail==1;errors=irqs=stops=starts=timers=0;start_bytes=0;ide_sector_write_cb(&s,fail?-5:0);
  assert(!s.pio_aiocb&&s.status==3&&errors==(fail?1u:0u));
  if(fail==1){assert(s.nsector==(int)n&&s.sector==123&&!irqs&&!stops&&!starts&&!timers);}
  else{unsigned copied=n<chunk?n:chunk,remaining=n-copied;expected++;assert(s.nsector==(int)remaining&&s.sector==123+copied);assert(stops==!remaining&&starts==!!remaining);if(remaining)assert(start_bytes==(remaining<chunk?remaining:chunk)*512);assert(irqs==!hack&&timers==hack);}
  assert(lr_state_hdd_writes()==expected);cases++;
 }
 /* Same 64-bit representation and wrapping count as the old unsigned statistic. */
 retro_atomic_store_relaxed_64(&blk.guest_write_generation,-2);blk_note_guest_write(&blk);assert(lr_state_hdd_writes()==UINT64_MAX);blk_note_guest_write(&blk);assert(!lr_state_hdd_writes());state_disk=NULL;assert(!lr_state_hdd_writes());
 printf("IDE write generation: %u completion/error/retry/IRQ schedules; exact sectors, counter, continuation and missing-drive/wrap behavior PASS\n",cases);return 0;
}
