/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <retro_atomic.h>
#define TARGET_PAGE_SIZE 4096
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
static retro_atomic_int_t ownership_ready,ownership_posted,ownership_done;
static bool ownership_is_worker,armed;
static uint64_t ownership_table_pages[512];
static const uint64_t *ownership_readonly_pages=ownership_table_pages;
typedef struct {void*ram;} MCPXAPUState;
static MCPXAPUState device,*ownership_device=&device;
static uint64_t memory_region_get_ram_addr(void*p){return 0x100000;}
static uint64_t memory_region_size(void*p){return 16*4096;}
static uint64_t live;static unsigned waits,relaxes;
static int ownership_page_live(uint64_t page){return page<16 && ((live>>page)&1);}
static void ownership_relax_if_cpu(void){if(armed){armed=false;relaxes++;}}
static void lr_dsp_ownership_quiesce(void){waits++;retro_atomic_store_release_int(&ownership_done,retro_atomic_load_acquire_int(&ownership_posted));ownership_relax_if_cpu();}
#define LR_DSP_FENCE_RAM 4
static void lr_dsp_ownership_fence(unsigned reason){lr_dsp_ownership_quiesce();}
/* PRODUCTION ACCESS */
int main(void)
{
    retro_atomic_int_init(&ownership_ready,1);retro_atomic_int_init(&ownership_posted,0);retro_atomic_int_init(&ownership_done,0);
    /* Retired work must remove guards even when no wait was necessary. */
    armed=true;lr_dsp_ownership_ram(0x103000,4);assert(!armed && relaxes==1 && !waits);
    for(unsigned epoch=1;epoch<=2000;epoch++) {
        live=1ull<<3;ownership_table_pages[0]=live;armed=true;
        retro_atomic_store_release_int(&ownership_posted,epoch);
        lr_dsp_ownership_read(0x103000,4);assert(armed && waits==epoch-1);
        lr_dsp_ownership_ram(0x109000,4);assert(armed && waits==epoch-1);
        lr_dsp_ownership_ram(0x102fff,2);assert(!armed && waits==epoch);
        lr_dsp_ownership_ram(0x103000,4);assert(waits==epoch);
    }
    live=(1ull<<3)|(1ull<<4);ownership_table_pages[0]=1ull<<3;armed=true;
    retro_atomic_store_release_int(&ownership_posted,2001);
    lr_dsp_ownership_read(0x103fff,2);assert(!armed && waits==2001);
    ownership_is_worker=true;armed=true;retro_atomic_store_release_int(&ownership_posted,2002);
    lr_dsp_ownership_ram(0x103000,4);assert(armed && waits==2001);
    ownership_is_worker=false;lr_dsp_ownership_ram(0x103000,4);assert(!armed && waits==2002);
    puts("PASS: production physical-RAM helper removes retired guards on first access; 2000 live/disjoint and cross-page hazards wait correctly, with no worker self-wait");
}
