# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production compatibility paths without a GPU or guest image."""
from pathlib import Path
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()


def function(source, name):
    start = source.index(name + '(')
    start = source.rfind('\n', 0, start) + 1
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


compute = (repo / 'hw/xbox/nv2a/pgraph/vk/surface-compute.c').read_text()
texture = (repo / 'hw/xbox/nv2a/pgraph/vk/texture.c').read_text()
pbus = (repo / 'hw/xbox/nv2a/pbus.c').read_text()
nvnet = (repo / 'hw/xbox/mcpx/nvnet/nvnet.c').read_text()
border = texture[texture.index('    bool is_integer_type = vkf.vk_format'):
                 texture.index('    if (filter & NV_PGRAPH_TEXFILTER0_ASIGNED)')]
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan.h>
#define MIN(a,b) ((a)<(b)?(a):(b))
typedef struct { VkPhysicalDeviceProperties device_props;
    bool custom_border_color_extension_enabled; } PGRAPHVkState;
/* COMPUTE */
static void pgraph_argb_pack32_to_rgba_float(uint32_t c, float *rgba) {
    rgba[0]=((c>>16)&255)/255.0f; rgba[1]=((c>>8)&255)/255.0f;
    rgba[2]=(c&255)/255.0f; rgba[3]=(c>>24)/255.0f;
}
static void check_border(bool integer, bool extension, uint32_t color) {
    PGRAPHVkState state={.custom_border_color_extension_enabled=extension};
    PGRAPHVkState *r=&state;
    struct { VkFormat vk_format; } vkf={integer?VK_FORMAT_R32_UINT:VK_FORMAT_R8G8B8A8_UNORM};
    uint32_t border_color_pack32=color;
    void *sampler_next_struct=NULL;
    VkSamplerCustomBorderColorCreateInfoEXT custom_border_color_create_info={0};
    VkBorderColor vk_border_color=VK_BORDER_COLOR_MAX_ENUM;
    /* BORDER */
    bool exact=color==0 || (!integer && (color==0xff000000 || color==0xffffffff));
    assert((sampler_next_struct!=NULL)==(!exact && extension));
    if (sampler_next_struct) {
        assert(sampler_next_struct==&custom_border_color_create_info);
        assert(custom_border_color_create_info.format==vkf.vk_format);
        if (integer) {
            assert(vk_border_color==VK_BORDER_COLOR_INT_CUSTOM_EXT);
            if (color==0xffffffff) {
                /* Integer opaque white (1) would corrupt this value. */
                assert(custom_border_color_create_info.customBorderColor.uint32[0]==UINT32_MAX);
            }
        } else {
            assert(vk_border_color==VK_BORDER_COLOR_FLOAT_CUSTOM_EXT);
            assert(custom_border_color_create_info.customBorderColor.float32[0]==((color>>16)&255)/255.0f);
        }
    } else if (integer) {
        assert(vk_border_color==VK_BORDER_COLOR_INT_TRANSPARENT_BLACK);
    } else {
        assert(vk_border_color==(color==0?VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
            color==0xff000000?VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE));
    }
}
typedef uint64_t hwaddr;
typedef struct { uint8_t config[256], mask[256]; } PCIDevice;
typedef PCIDevice NV2AState;
#define PCI_DEVICE(s) ((PCIDevice *)(s))
#define PCI_CONFIG_SPACE_SIZE 256
#define NV_PBUS_PCI_NV_0 0x800
#define NV_PBUS 0
#define nv2a_reg_log_read(...) ((void)0)
#define nv2a_reg_log_write(...) ((void)0)
static unsigned reads,writes,last_addr,last_size;
static uint32_t pci_host_config_read_common(PCIDevice *d,unsigned a,unsigned limit,unsigned size) {
    assert(limit==256); reads++;last_addr=a;last_size=size;
    uint32_t v=0; for(unsigned i=0;i<size && a+i<limit;i++) v|=(uint32_t)d->config[a+i]<<(8*i);
    return v;
}
static void pci_host_config_write_common(PCIDevice *d,unsigned a,unsigned limit,uint32_t val,unsigned size) {
    assert(limit==256); writes++;last_addr=a;last_size=size;
    for(unsigned i=0;i<size && a+i<limit;i++) {
        unsigned mask=d->mask[a+i];
        d->config[a+i]=(d->config[a+i]&~mask)|((val>>(8*i))&mask);
    }
}
/* PBUS */
typedef struct { bool pending; int64_t deadline; } QEMUTimer;
typedef struct NvNetState NvNetState;
typedef struct { NvNetState *opaque; bool link_down; } NetClientState;
typedef struct { NetClientState nc; } NICState;
struct NvNetState { bool rx,dma,link,available; QEMUTimer *rx_poll_timer; NICState *nic;
    uint32_t phy_regs[6]; uint8_t tx_ring_index,rx_ring_index; };
static unsigned descriptor_reads,flushes;
static int64_t clock_ms=100;
#define QEMU_CLOCK_VIRTUAL 0
#define RX_POLL_INTERVAL_MS 1
#define trace_nvnet_cant_rx(...) ((void)0)
static NvNetState *qemu_get_nic_opaque(NetClientState *nc) { return nc->opaque; }
static NetClientState *qemu_get_queue(NICState *nic) { return &nic->nc; }
static bool rx_enabled(NvNetState *s) { return s->rx; }
static bool dma_enabled(NvNetState *s) { return s->dma; }
static bool link_up(NvNetState *s) { return s->link; }
static bool rx_buf_available(NvNetState *s) { descriptor_reads++;return s->available; }
static bool timer_pending(QEMUTimer *t) { return t->pending; }
static void timer_mod(QEMUTimer *t,int64_t when) { t->pending=true;t->deadline=when; }
static void timer_del(QEMUTimer *t) { t->pending=false; }
static int64_t qemu_clock_get_ms(int clock) { (void)clock;return clock_ms; }
#define NVNET(s) ((NvNetState *)(s))
#define MII_BMSR 1
#define MII_BMSR_LINK_ST 4
#define MII_BMSR_AN_COMP 32
#define NVNET_TX_RING_PHYS_ADDR 0
#define NVNET_RX_RING_PHYS_ADDR 1
#define NVNET_TX_RING_NEXT_DESC_PHYS_ADDR 2
#define NVNET_RX_RING_NEXT_DESC_PHYS_ADDR 3
struct RingDesc { uint32_t addr; uint16_t length,flags; };
static void reset_phy_regs(NvNetState *s) { memset(s->phy_regs,0,sizeof(s->phy_regs)); }
static uint32_t get_reg(NvNetState *s,unsigned reg) { (void)s;(void)reg;return 0; }
static unsigned get_tx_ring_size(NvNetState *s) { (void)s;return 1; }
static unsigned get_rx_ring_size(NvNetState *s) { (void)s;return 1; }
static void set_reg(NvNetState *s,unsigned reg,uint32_t val) { (void)s;(void)reg;(void)val; }
static bool have_autoneg(NvNetState *s) { (void)s;return false; }
static void restart_autoneg(NvNetState *s) { (void)s;assert(false); }
/* Simulate draining a packet: a second queued packet finds the ring full. */
static bool nvnet_can_receive(NetClientState *nc);
static void qemu_flush_queued_packets(NetClientState *nc) {
    assert(nvnet_can_receive(nc));flushes++;
    nc->opaque->available=false;
    assert(!nvnet_can_receive(nc));
}
/* NVNET */
int main(void) {
    unsigned cases=0;
    const unsigned limits[]={1,32,64,128,192,256,512,1024,2048};
    for(unsigned x=0;x<9;x++) for(unsigned y=0;y<9;y++) {
        PGRAPHVkState r={0};
        r.device_props.limits.maxComputeWorkGroupSize[0]=limits[x];
        r.device_props.limits.maxComputeWorkGroupInvocations=limits[y];
        for(int units=1;units<=4096;units++) {
            unsigned g=get_workgroup_size_for_output_units(&r,units);
            assert(g && g<=1024 && g<=limits[x] && g<=limits[y] && units%g==0);
            assert(g==1024 || g*2>limits[x] || g*2>limits[y] || units%(g*2)!=0);
            cases++;
        }
    }
    uint32_t colors[]={0,0xff000000,0xffffffff,0x12345678,0x00ffffff};
    for(unsigned i=0;i<2;i++) for(unsigned e=0;e<2;e++) for(unsigned c=0;c<5;c++) {
        check_border(i,e,colors[c]); cases++;
    }
    NV2AState dev={0};
    for(unsigned i=0;i<256;i++) { dev.config[i]=(uint8_t)(i*13);dev.mask[i]=255; }
    dev.mask[0]=dev.mask[1]=dev.mask[2]=dev.mask[3]=0;
    for(unsigned alias=0;alias<2;alias++) for(unsigned size=1;size<=4;size*=2)
    for(unsigned off=0;off<256;off+=size) {
        hwaddr addr=NV_PBUS_PCI_NV_0+alias*256+off;
        unsigned before=reads;
        pbus_read(&dev,addr,size); assert(reads==before+1 && last_addr==off && last_size==size);
        before=writes;
        uint32_t old=pbus_read(&dev,addr,size);
        pbus_write(&dev,addr,0xa5a5a5a5,size);
        assert(writes==before+1 && last_addr==off && last_size==size);
        uint32_t mask=size==4?UINT32_MAX:((1u<<(size*8))-1);
        uint32_t got=pbus_read(&dev,NV_PBUS_PCI_NV_0+(1-alias)*256+off,size);
        assert(got==(off<4?old:(0xa5a5a5a5&mask)));cases++;
    }
    hwaddr outside[]={0, NV_PBUS_PCI_NV_0-1,NV_PBUS_PCI_NV_0+512,UINT64_MAX};
    for(unsigned i=0;i<4;i++) {
        unsigned rd=reads,wr=writes;
        assert(pbus_read(&dev,outside[i],4)==0);pbus_write(&dev,outside[i],1,4);
        assert(reads==rd && writes==wr);cases++;
    }
    QEMUTimer timer={0}; NICState nic={0};
    NvNetState s={.rx_poll_timer=&timer,.nic=&nic};nic.nc.opaque=&s;
    for(unsigned flags=0;flags<16;flags++) {
        s.rx=flags&1;s.dma=flags&2;s.link=flags&4;s.available=flags&8;
        timer.pending=true;timer.deadline=77;
        unsigned before=descriptor_reads;
        bool ready=s.rx && s.dma && s.link;
        assert(nvnet_can_receive(&nic.nc)==(ready && s.available));
        assert(descriptor_reads==before+ready);
        assert(timer.pending==(ready && !s.available));
        if(timer.pending) assert(timer.deadline==77);
        cases++;
    }
    s.rx=s.dma=s.link=true;s.available=false;timer.pending=false;
    nvnet_flush_rx(&s);assert(!flushes && timer.pending && timer.deadline==101);
    /* Descriptor-only recovery, with no MMIO write. */
    s.available=true;timer.pending=false;clock_ms=101;rx_poll_timer(&s);
    assert(flushes==1 && timer.pending && timer.deadline==102);
    /* A disable cancels polling and prevents descriptor DMA. */
    s.dma=false;unsigned before=descriptor_reads;nvnet_flush_rx(&s);
    assert(!timer.pending && descriptor_reads==before && flushes==1);
    /* Loading a state schedules recovery without inspecting or writing RAM.
     * Stale host deadlines must not survive an inactive restored receiver. */
    for(unsigned flags=0;flags<8;flags++) {
        s.rx=flags&1;s.dma=flags&2;s.link=flags&4;
        s.phy_regs[MII_BMSR]=s.link?MII_BMSR_LINK_ST:0;
        timer.pending=true;timer.deadline=1;before=descriptor_reads;
        assert(nvnet_post_load(&s,2)==0);
        assert(descriptor_reads==before && flushes==1);
        assert(timer.pending==(s.rx && s.dma && s.link));
        if(timer.pending) assert(timer.deadline==102);
        cases++;
    }
    puts("compatibility backports: workgroup, border, PBUS and RX recovery passed");
    printf("%u matrix cases plus descriptor-only recovery\n",cases);
}
'''
code = code.replace('/* COMPUTE */', function(compute, 'get_workgroup_size_for_output_units'))
code = code.replace('/* BORDER */', border)
code = code.replace('/* PBUS */', pbus[pbus.index('#define NV_PBUS_PCI_NV_MIRROR_SIZE'):])
code = code.replace('/* NVNET */', '\n'.join(function(nvnet, f) for f in
                    ('nvnet_can_receive', 'nvnet_flush_rx', 'rx_poll_timer',
                     'nvnet_post_load')))
with tempfile.TemporaryDirectory(prefix='backport-compat-') as tmp:
    tmp = Path(tmp)
    (tmp / 'test.c').write_text(code)
    subprocess.run([sys.argv[2] if len(sys.argv)>2 else 'gcc', '-O2', '-std=gnu11',
                    '-Wall', '-Wextra', '-Werror', '-Wno-sign-compare',
                    '-fsanitize=undefined', '-fsanitize-undefined-trap-on-error',
                    '-I' + str(repo / 'deps/vulkan-headers'), str(tmp / 'test.c'),
                    '-o', str(tmp / 'test')], check=True)
    subprocess.run([str(tmp / 'test')], check=True, timeout=15)
