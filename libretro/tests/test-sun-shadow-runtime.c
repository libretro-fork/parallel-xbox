/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hw/xbox/nv2a/pgraph/sun-shadow-runtime.h"
#define TARGET_PAGE_SIZE 4096u
#define TARGET_PAGE_MASK (~(uint32_t)4095)
#define MIN(a,b) ((a)<(b)?(a):(b))
typedef uint64_t hwaddr;
typedef struct CPUState { bool present; } CPUState;
typedef struct MemoryRegion { bool ram; unsigned refs; } MemoryRegion;
typedef struct MemoryRegionSection { MemoryRegion *mr; uint64_t size; } MemoryRegionSection;
static CPUState cpu={true};
static uint8_t ram[16384];
static MemoryRegion regions[4]={{true,0},{true,0},{true,0},{true,0}};
static unsigned translations,reads,unrefs,fail_read;
static bool short_region,missing_region;
static CPUState *qemu_get_cpu(unsigned index){assert(index==0);return cpu.present?&cpu:NULL;}
static int virt_to_phys(uint32_t address,hwaddr *physical)
{
    translations++;
    uint32_t page=address&TARGET_PAGE_MASK;
    unsigned slot;
    if(page>=0x10000 && page<0x14000)slot=(page-0x10000)/4096;
    else if(page==0x80000000)slot=2;
    else if(page==0xfffff000)slot=3;
    else return 1;
    *physical=(3-slot)*4096+(address&4095);return 0;
}
static void *get_system_memory(void){return ram;}
static MemoryRegionSection memory_region_find(void *root,hwaddr address,size_t size)
{
    assert(root==ram && address+size<=sizeof(ram));
    if(missing_region)return(MemoryRegionSection){0};
    MemoryRegion *mr=&regions[address/4096];mr->refs++;
    return(MemoryRegionSection){mr,short_region?size-1:size};
}
static bool memory_region_is_ram(MemoryRegion *r){return r->ram;}
static uint64_t int128_get64(uint64_t size){return size;}
static void memory_region_unref(MemoryRegion *r){assert(r->refs);r->refs--;unrefs++;}
static int cpu_memory_rw_debug(CPUState *c,uint32_t address,uint8_t *data,size_t size,bool write)
{
    assert(c==&cpu && !write && size && size<=4096-(address&4095));
    reads++;if(reads==fail_read)return -1;
    hwaddr physical;assert(!virt_to_phys(address,&physical));
    assert(regions[physical/4096].ram && !regions[physical/4096].refs);
    memcpy(data,ram+physical,size);return 0;
}
/* PRODUCTION RAM READER */

static bool accepted;
static unsigned verifies;
static bool verify(void *opaque){assert(opaque==&accepted);verifies++;return accepted;}

int main(void)
{
    PgraphSunIdentity identity={0};
    for(unsigned i=0;i<100;i++) {
        pgraph_sun_identity_update(&identity,i,verify,&accepted);
        assert(identity.build==PGRAPH_SUN_UNSUPPORTED);
    }
    assert(verifies==0);
    assert(pgraph_sun_identity_update(&identity,0x54430007,verify,&accepted));
    assert(verifies==1 && identity.build==PGRAPH_SUN_UNSUPPORTED);
    accepted=true;
    for(unsigned i=0;i<100;i++)assert(!pgraph_sun_identity_update(&identity,0x54430007,verify,&accepted));
    assert(verifies==1 && identity.build==PGRAPH_SUN_UNSUPPORTED);
    pgraph_sun_identity_reset(&identity);
    assert(!identity.checked && identity.build==PGRAPH_SUN_UNSUPPORTED);
    assert(pgraph_sun_identity_update(&identity,0x54430007,verify,&accepted));
    assert(verifies==2 && identity.build==PGRAPH_SUN_DOAXVB_US_11106);
    assert(pgraph_sun_identity_update(&identity,0,verify,&accepted));
    assert(verifies==2 && identity.build==PGRAPH_SUN_UNSUPPORTED);
    assert(pgraph_sun_identity_update(&identity,0x54430007,verify,&accepted));
    assert(verifies==3 && identity.build==PGRAPH_SUN_DOAXVB_US_11106);
    uint64_t lifetime=identity.generation;
    pgraph_sun_identity_reset(&identity);assert(identity.generation>lifetime);
    identity.generation=UINT64_MAX;
    pgraph_sun_identity_reset(&identity);assert(identity.generation==UINT64_MAX && !identity.checked);
    for(unsigned i=0;i<sizeof(ram);i++)ram[i]=(uint8_t)(i*73+i/4096);
    uint8_t output[8192];
    for(unsigned offset=0;offset<4096;offset+=17) {
        memset(output,0x99,sizeof(output));
        assert(xemu_read_virtual_ram(0x10000+offset,output,sizeof(output)));
        for(unsigned i=0;i<sizeof(output);i++) {
            unsigned page=(offset+i)/4096;
            unsigned physical=(3-page)*4096+(offset+i)%4096;
            assert(output[i]==ram[physical]);
        }
    }
    assert(xemu_read_virtual_ram(0x80000000,output,16));
    assert(!memcmp(output,ram+4096,16));
    assert(xemu_read_virtual_ram(UINT32_MAX,output,1) && output[0]==ram[4095]);
    unsigned before=translations;
    assert(!xemu_read_virtual_ram(UINT32_MAX,output,2) && translations==before);
    assert(!xemu_read_virtual_ram(0x10000,NULL,1) && translations==before);
    assert(xemu_read_virtual_ram(0x10000,NULL,0));
    assert(!xemu_read_virtual_ram(0x14000,output,1));
    unsigned read_before=reads;
    regions[3].ram=false;
    assert(!xemu_read_virtual_ram(0x10000,output,1) && reads==read_before);
    regions[3].ram=true;short_region=true;
    assert(!xemu_read_virtual_ram(0x10000,output,2) && reads==read_before);
    short_region=false;missing_region=true;
    assert(!xemu_read_virtual_ram(0x10000,output,1) && reads==read_before);
    missing_region=false;fail_read=reads+1;
    assert(!xemu_read_virtual_ram(0x10000,output,1));
    cpu.present=false;
    assert(!xemu_read_virtual_ram(0x10000,output,1));
    for(unsigned i=0;i<4;i++)assert(!regions[i].refs);
    assert(unrefs>0);
    puts("Sun runtime: one verification per lifetime, reset/title transitions, 241 cross-page reads, kernel alias, MMIO/gap/failure rejection passed");
    return 0;
}
