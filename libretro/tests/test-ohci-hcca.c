/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef OHCI_FRAME_STEP
#define OHCI_FRAME_STEP 1
#endif
typedef uint64_t dma_addr_t;
/* TYPES */
#define MEMTXATTRS_UNSPECIFIED 0
#define cpu_to_le16(x) (x)
#define cpu_to_le32(x) (x)
#define le32_to_cpu(x) (x)
typedef struct OHCIState {
    void *as, *eof_timer;
    uint32_t hcca, localmem_base, ctl, old_ctl, status, intr_status, intr;
    uint32_t ctrl_cur, ctrl_head, bulk_cur, bulk_head, done;
    unsigned frame_number, done_count, frt, fit;
    int64_t sof_time;
} OHCIState;
static unsigned char ram[1024];
static OHCIState *current;
static unsigned writes, notices, deaths, timers, interleave, error_mode;
static bool delivery;
static uint32_t expected_head;
static int64_t usb_frame_time = 1000;
static struct ohci_hcca *guest(void) { return (void *)(ram + 256); }
static void timer_mod(void *timer, int64_t deadline)
{ (void)timer; assert(deadline == current->sof_time + usb_frame_time); timers++; }
static void ohci_die(OHCIState *s) { s->intr_status |= OHCI_INTR_UE; deaths++; }
static void trace_usb_ohci_hcca_read_error(uint32_t addr) { (void)addr; }
static void trace_usb_ohci_process_lists(uint32_t a, uint32_t b) { (void)a; (void)b; }
static int ohci_read_hcca(OHCIState *s, dma_addr_t addr, struct ohci_hcca *h)
{ (void)s; (void)addr; memcpy(h, guest(), sizeof(*h)); return error_mode == 1; }
static int ohci_service_ed_list(OHCIState *s, uint32_t head)
{
    (void)head;
    /* Guest clears the old queue after the controller read its HCCA. */
    if (interleave) guest()->done = 0;
    if (error_mode == 2) s->intr_status |= OHCI_INTR_UE;
    return 0;
}
static void ohci_stop_endpoints(OHCIState *s) { (void)s; }
static int dma_memory_write(void *as, dma_addr_t addr, const void *src,
                            size_t size, int attrs)
{
    (void)as; (void)attrs;
    assert(addr == 256 + offsetof(struct ohci_hcca, frame));
    assert(size == (delivery ? 8 : 4));
    /* Model a guest running while DMA releases the BQL, before publication. */
    assert(notices == 0);
    if (error_mode == 3) return 1;
    memcpy(ram + addr, src, size); writes++;
    return 0;
}
static void ohci_set_interrupt(OHCIState *s, uint32_t bits)
{
    assert(writes == 1 && !deaths);
    assert(guest()->frame == s->frame_number && guest()->pad == 0);
    assert(bits == (OHCI_INTR_SF | (delivery ? OHCI_INTR_WD : 0)));
    assert(guest()->done == expected_head);
    s->intr_status |= bits; notices++;
    /* Immediate guest IRQ acknowledgement must not be overwritten later. */
    if (delivery) guest()->done = 0;
}
/* PRODUCTION */
int main(void)
{
    unsigned cases = 0;
    for (unsigned frame = 0; frame < 65536; frame+=OHCI_FRAME_STEP)
    for (unsigned count = 0; count < 8; count++)
    for (unsigned pending = 0; pending < 2; pending++)
    for (unsigned enabled = 0; enabled < 2; enabled++)
    for (interleave = 0; interleave < 2; interleave++) {
        OHCIState s = { .hcca=128, .localmem_base=128,
            .ctl=OHCI_CTL_PLE | OHCI_CTL_CLE | OHCI_CTL_BLE,
            .status=OHCI_STATUS_CLF | OHCI_STATUS_BLF,
            .frame_number=frame, .done_count=count, .done=0x12345678,
            .intr_status=pending ? OHCI_INTR_WD : OHCI_INTR_SF,
            .intr=enabled ? OHCI_INTR_SF : 0, .fit=1, .sof_time=123 };
        current=&s; memset(ram, 0xa5, sizeof(ram));
        guest()->done=0xabcdef00;
        writes=notices=deaths=timers=error_mode=0;
        delivery=count==0 && !pending;
        expected_head=delivery ? (0x12345678 | enabled) :
                      (interleave ? 0 : 0xabcdef00);
        ohci_frame_boundary(&s);
        assert(writes==1 && notices==1 && !deaths && timers==1);
        assert(s.frame_number==((frame+1)&0xffff) && s.frt==1);
        assert(s.sof_time==1123);
        assert(s.done_count==(delivery ? 7 : (count && count!=7 ? count-1 : count)));
        assert(s.done==(delivery ? 0 : 0x12345678));
        assert(guest()->done==(delivery ? 0 : expected_head));
        for (unsigned n=0;n<256;n++) assert(ram[n]==0xa5);
        for (unsigned n=256+sizeof(*guest());n<sizeof(ram);n++) assert(ram[n]==0xa5);
        cases++;
    }
    for (error_mode=1;error_mode<=3;error_mode++) {
        OHCIState s={ .hcca=128, .localmem_base=128,
                     .ctl=OHCI_CTL_PLE, .done_count=0, .done=0x12345678 };
        current=&s; writes=notices=deaths=timers=0; delivery=true;
        ohci_frame_boundary(&s);
        assert(!notices && !writes && (s.intr_status & OHCI_INTR_UE));
        assert(deaths==(error_mode==2 ? 0 : 1));
    }
    printf("PASS: %u OHCI frames; DMA-before-IRQ, guest head clears, countdown, wrap and error paths\n",cases);
}
