/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SCALE_MS 1000000LL
#define NANOSECONDS_PER_SECOND 1000000000LL
#define QEMU_CLOCK_VIRTUAL 0
#define MEMTXATTRS_UNSPECIFIED 0
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
/* CONSTANTS */
/* REGISTERS */
/* PACING */
typedef struct { bool irq; } PCIDevice;
typedef struct { int64_t deadline; bool armed; } QEMUTimer;
typedef struct { unsigned unused; } MemoryRegion;
typedef struct { unsigned reads; uint8_t data[0x4000]; } AddressSpace;
/* TYPES */
static int64_t now;
static unsigned checks;
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "ACI playback: line %u: %s\n", __LINE__, #x); exit(1); } checks++; } while (0)
static int64_t qemu_clock_get_ns(int clock) { (void)clock; return now; }
static void timer_del(QEMUTimer *t) { t->armed = false; }
static void timer_mod(QEMUTimer *t, int64_t deadline) { t->armed = true; t->deadline = deadline; }
static void pci_irq_assert(PCIDevice *d) { d->irq = true; }
static void pci_irq_deassert(PCIDevice *d) { d->irq = false; }
static uint32_t ldl_le_p(const void *pointer)
{
    const uint8_t *p = pointer;
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void dma_memory_read(AddressSpace *as, uint32_t address, void *out, unsigned size, int attrs)
{
    (void)attrs;
    REQUIRE(size == 8 && address >= 0x1000 && address + size <= sizeof(as->data));
    as->reads++;
    memcpy(out, as->data + address, size);
}
/* PRODUCTION */
static void put32(AddressSpace *as, unsigned address, uint32_t value)
{
    for (unsigned i=0;i<4;i++) as->data[address+i] = value >> (i*8);
}
static void init(MCPXACIState *s, AddressSpace *as, QEMUTimer *po, QEMUTimer *so, int rate)
{
    memset(s,0,sizeof(*s)); memset(as,0,sizeof(*as)); memset(po,0,sizeof(*po)); memset(so,0,sizeof(*so));
    now=0; s->as=as; s->po_timer=po; s->so_timer=so;
    reset_bus_masters(s); po_open(s,rate);
    for(unsigned stream=0;stream<2;stream++) {
        unsigned index=stream?SO_INDEX:PO_INDEX, base=stream?0x2000:0x1000;
        for(unsigned i=0;i<32;i++) {
            put32(as,base+8*i,0x800000+0x10000*stream+0x4000*i);
            put32(as,base+8*i+4,BD_IOC|4800);
        }
        nabm_writel(s,index*16,base); nabm_writeb(s,index*16+5,31);
    }
}
static void start(MCPXACIState *s, unsigned index)
{ nabm_writeb(s,index*16+11,CR_RPBM|CR_IOCE|CR_LVBIE); }
static void tick(MCPXACIState *s, unsigned ms)
{ now=(int64_t)ms*SCALE_MS; po_tick(s); so_tick(s); }
static unsigned position(MCPXACIState *s, unsigned index)
{
    AC97BusMasterRegs *r=&s->bm_regs[index];
    return r->civ*9600+(4800-r->picb)*2;
}
static void independent_clocks(void)
{
    MCPXACIState s; AddressSpace as; QEMUTimer po,so;
    init(&s,&as,&po,&so,44100); start(&s,SO_INDEX);
    REQUIRE(so.armed && !po.armed && !s.po_active && s.so_active);
    for(unsigned ms=10;ms<=100;ms+=10) tick(&s,ms);
    unsigned begin=position(&s,SO_INDEX);
    for(unsigned ms=110;ms<=1100;ms+=10) tick(&s,ms);
    REQUIRE(position(&s,SO_INDEX)-begin==192000);
    REQUIRE(s.bm_regs[PO_INDEX].picb==0 && !s.po_active);
    start(&s,PO_INDEX);
    for(unsigned ms=1110;ms<=1200;ms+=10) tick(&s,ms);
    unsigned p=position(&s,PO_INDEX),d=position(&s,SO_INDEX);
    /* Measure within the remaining original descriptors. */
    for(unsigned ms=1210;ms<=1400;ms+=10) tick(&s,ms);
    REQUIRE(position(&s,PO_INDEX)-p==35280);
    REQUIRE(position(&s,SO_INDEX)-d==38400);
    nabm_writeb(&s,PO_CR,0); REQUIRE(!po.armed && so.armed);
    unsigned old=s.bm_regs[PO_INDEX].picb;
    tick(&s,1410); REQUIRE(s.bm_regs[PO_INDEX].picb==old && s.so_active);
    po_open(&s,0); REQUIRE(!po.armed && so.armed);
    unsigned prior=position(&s,SO_INDEX); tick(&s,1420);
    REQUIRE(position(&s,SO_INDEX)>prior);
    nabm_writeb(&s,SO_CR,0); REQUIRE(!so.armed && !s.so_active);
    puts("PASS: S/PDIF-only 192000 B/s; simultaneous 44100-Hz PCM and 48000-Hz S/PDIF; independent stop");
}
static void completion_and_resume(void)
{
    MCPXACIState s; AddressSpace as; QEMUTimer po,so;
    init(&s,&as,&po,&so,48000);
    /* Odd descriptor lengths are 16-bit units, and can split a stereo frame. */
    for(unsigned i=0;i<32;i++) put32(&as,0x2000+8*i+4,BD_IOC|1);
    nabm_writeb(&s,SO_LVI,2); start(&s,SO_INDEX); tick(&s,10);
    AC97BusMasterRegs *r=&s.bm_regs[SO_INDEX];
    REQUIRE(r->civ==2 && r->picb==0 && r->piv==3);
    REQUIRE((r->sr&(SR_BCIS|SR_LVBCI|SR_DCH|SR_CELV))==(SR_BCIS|SR_LVBCI|SR_DCH|SR_CELV));
    REQUIRE(s.dev.irq && (s.glob_sta&GS_SOINT));
    REQUIRE(as.reads==3);
    nabm_writew(&s,SO_SR,SR_INT_MASK); REQUIRE(!s.dev.irq);
    nabm_writeb(&s,SO_LVI,4); tick(&s,20);
    REQUIRE(r->civ==4 && r->picb==0 && as.reads==5);
    REQUIRE(s.dev.irq);
    nabm_writeb(&s,SO_CR,CR_RR); REQUIRE(!s.so_active && !so.armed && !s.dev.irq);
    REQUIRE(r->sr==SR_DCH && r->picb==0 && r->bdbar==0);
    puts("PASS: odd descriptors, IOC/end-of-list IRQs, W1C, LVI resume and reset");
}
static void ring_and_empty(void)
{
    MCPXACIState s; AddressSpace as; QEMUTimer po,so;
    init(&s,&as,&po,&so,48000); start(&s,SO_INDEX);
    AC97BusMasterRegs *r=&s.bm_regs[SO_INDEX];
    r->civ=31; r->piv=0; r->lvi=1; r->bd_valid=0;
    put32(&as,0x2000+31*8+4,BD_IOC|2); put32(&as,0x2004,BD_IOC|2); put32(&as,0x200c,BD_IOC|2);
    tick(&s,10); REQUIRE(r->civ==1 && r->piv==2 && r->picb==0);
    init(&s,&as,&po,&so,48000);
    for(unsigned i=0;i<32;i++) put32(&as,0x2000+8*i+4,0);
    start(&s,SO_INDEX);
    for(unsigned ms=10;ms<=400;ms+=10) tick(&s,ms);
    REQUIRE(r->civ==31 && r->picb==0 && (r->sr&SR_DCH));
    REQUIRE(as.reads<=32 && s.so_credit>=0 && s.so_credit<=SO_CAP_BYTES);
    puts("PASS: ring wrap and bounded zero-length descriptor handling");
}
static void state_and_time(void)
{
    MCPXACIState s; AddressSpace as; QEMUTimer po,so;
    init(&s,&as,&po,&so,48000); start(&s,SO_INDEX); tick(&s,10);
    int64_t credit=s.so_credit; now=5*SCALE_MS; so_tick(&s);
    REQUIRE(s.so_credit<=credit && s.so_credit>=0);
    /* Version 2 states have no S/PDIF clock state; restart from guest CR. */
    s.so_active=true; s.so_credit=-5; s.so_last_ns=INT64_MAX; s.so_rem=77;
    now=100*SCALE_MS; REQUIRE(mcpx_aci_post_load(&s,2)==0);
    REQUIRE(s.so_active && s.so_credit==SO_CAP_BYTES && s.so_last_ns==now && s.so_rem==0);
    REQUIRE(so.armed && so.deadline==now+PO_PERIOD_NS);
    s.bm_regs[SO_INDEX].cr=0; REQUIRE(mcpx_aci_post_load(&s,1)==0);
    REQUIRE(!s.so_active && !so.armed);
    /* Version 3 continuation retains partial carrier credit and remainder. */
    s.so_active=true; s.so_credit=2; s.so_last_ns=now; s.so_rem=123; timer_mod(&so,now+PO_PERIOD_NS);
    REQUIRE(mcpx_aci_post_load(&s,3)==0);
    REQUIRE(s.so_credit==2 && s.so_rem==123 && so.armed);
    /* Fractional updates may not drift or accumulate unbounded time debt. */
    s.so_credit=0; s.so_rem=0; s.so_last_ns=0;
    uint64_t consumed=0;
    for(int64_t t=1000001;t<1000000000;t+=1000001) {
        so_earn(&s,t); consumed+=so_take(&s,SO_CAP_BYTES);
        REQUIRE(s.so_credit>=0 && s.so_credit<=SO_CAP_BYTES);
    }
    so_earn(&s,1000000000); consumed+=so_take(&s,SO_CAP_BYTES);
    REQUIRE(consumed==192000);
    s.so_last_ns=0; so_earn(&s,INT64_MAX/4);
    REQUIRE(s.so_credit==SO_CAP_BYTES && s.so_rem==0);
    puts("PASS: backwards virtual time, legacy state clock initialization and version-3 clock preservation");
}
int main(void)
{
    independent_clocks(); completion_and_resume(); ring_and_empty(); state_and_time();
    printf("ACI playback: %u checks passed\n",checks); return 0;
}
