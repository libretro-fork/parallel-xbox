/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* DEFINITIONS */
typedef struct { unsigned nr; } USBEndpoint;
typedef struct { unsigned unused; } USBDevice;
typedef struct { int status, actual_length; uint8_t *data; size_t len; } USBPacket;
typedef struct {
    uint32_t async_td, done;
    bool async_complete;
    USBPacket usb_packet;
    uint8_t usb_buf[8192];
    int done_count;
} OHCIState;
static USBDevice device;
static USBEndpoint endpoint;
static uint8_t ram[65536];
static struct ohci_td descriptors[2];
static unsigned reads, writes, copies_in, copies_out, setups, handles, flushes, finds, deaths;
static int result;
static void ohci_die(OHCIState *s) { deaths++; }
static int ohci_read_td(OHCIState *s, uint32_t addr, struct ohci_td *td)
{ assert(addr == 0x100 || addr == 0x200); reads++; *td = descriptors[addr / 0x100 - 1]; return 0; }
static int ohci_put_td(OHCIState *s, uint32_t addr, struct ohci_td *td)
{ writes++; descriptors[addr / 0x100 - 1] = *td; return 0; }
enum { DMA_DIRECTION_TO_DEVICE, DMA_DIRECTION_FROM_DEVICE };
static int ohci_copy_td(OHCIState *s, struct ohci_td *td, uint8_t *data, size_t len, int dir)
{
    assert(td->cbp + len <= sizeof(ram));
    if (dir == DMA_DIRECTION_TO_DEVICE) { copies_out++; memcpy(data, ram + td->cbp, len); }
    else { copies_in++; memcpy(ram + td->cbp, data, len); }
    return 0;
}
static USBDevice *ohci_find_device(OHCIState *s, unsigned addr) { finds++; return &device; }
static USBEndpoint *usb_ep_get(USBDevice *d, int pid, unsigned nr) { endpoint.nr = nr; return &endpoint; }
static void usb_packet_setup(USBPacket *p, int pid, USBEndpoint *ep, int stream, uint32_t id, bool short_not_ok, bool int_req)
{ setups++; p->status = USB_RET_SUCCESS; p->actual_length = 0; }
static void usb_packet_addbuf(USBPacket *p, void *data, size_t len) { p->data = data; p->len = len; }
static void usb_handle_packet(USBDevice *d, USBPacket *p) { handles++; p->status = result; p->actual_length = result == USB_RET_SUCCESS ? p->len : 0; }
static void usb_device_flush_ep_queue(USBDevice *d, USBEndpoint *ep) { flushes++; }
static void ohci_td_pkt(const char *str, const uint8_t *buf, size_t len) {}
/* PRODUCTION */
static void reset(OHCIState *s)
{
    memset(s, 0, sizeof(*s)); s->done_count = 7;
    memset(s->usb_buf, 0xa5, sizeof(s->usb_buf));
    memset(ram, 0x3c, sizeof(ram)); memset(descriptors, 0, sizeof(descriptors));
    reads = writes = copies_in = copies_out = setups = handles = flushes = finds = deaths = 0;
    result = USB_RET_ASYNC;
}
static struct ohci_ed transfer(unsigned slot, unsigned dir, unsigned ep, unsigned len, bool td_direction)
{
    struct ohci_td *td = &descriptors[slot];
    *td = (struct ohci_td){ .flags = OHCI_TD_R | (dir << OHCI_TD_DP_SHIFT),
        .cbp = len ? 0x1000u + slot * 0x1000u : 0, .next = 0x300,
        .be = len ? 0x1000u + slot * 0x1000u + len - 1 : 0 };
    return (struct ohci_ed){ .head = (slot + 1) * 0x100,
        .flags = 1 | (ep << OHCI_ED_EN_SHIFT) | (64u << OHCI_ED_MPS_SHIFT)
               | (td_direction ? 0 : dir << OHCI_ED_D_SHIFT), .tail = 0x300 };
}
int main(void)
{
    OHCIState s;
    unsigned scenarios = 0;
    /* A real initial async submission owns the pointer retained by USB. A
     * blocked OUT/SETUP must never overwrite its bytes, even after completion
     * has arrived but before the original TD has consumed the result. */
    for (unsigned dir = 1; dir <= 2; dir++) for (unsigned len = 1; len <= 64; len++)
    for (unsigned incoming = 0; incoming <= 2; incoming++) for (unsigned td_dir = 0; td_dir < 2; td_dir++) {
        reset(&s);
        struct ohci_ed first = transfer(0, dir, 1, len, td_dir);
        assert(ohci_service_td(&s, &first) == 1 && s.async_td == 0x100 && flushes == 1);
        assert(handles == 1 && setups == 1 && reads == 1 && !writes);
        assert(ohci_service_td(&s, &first) == 1 && reads == 1 && handles == 1);
        memset(s.usb_packet.data, 0x69, s.usb_packet.len);
        uint8_t original[8192]; memcpy(original, s.usb_buf, sizeof(original));
        struct ohci_ed second = transfer(1, incoming, incoming ? 2 : 0, 8192, td_dir);
        for (unsigned complete = 0; complete < 2; complete++) {
            s.async_complete = complete;
            s.usb_packet.status = complete ? USB_RET_SUCCESS : USB_RET_ASYNC;
            s.usb_packet.actual_length = len;
            OHCIState before = s;
            unsigned old_reads = reads, old_copies = copies_out;
            assert(ohci_service_td(&s, &second) == 1);
            assert(!memcmp(original, s.usb_buf, sizeof(original)));
            assert(!memcmp(&before, &s, sizeof(s)));
            assert(reads == old_reads && copies_out == old_copies && handles == 1 && finds == 1);
        }
        assert(!ohci_service_td(&s, &first) && !s.async_td && !s.async_complete);
        assert((first.head & OHCI_DPTR_MASK) == 0x300 && s.done == 0x100 && writes == 1);
        if (dir == OHCI_TD_DIR_IN) {
            assert(copies_in == 1);
            for (unsigned i = 0; i < len; i++) assert(ram[0x1000 + i] == 0x69);
        }
        /* The next TD may start immediately once the slot is released. */
        second = transfer(1, OHCI_TD_DIR_OUT, 2, len, td_dir);
        result = USB_RET_SUCCESS;
        assert(!ohci_service_td(&s, &second) && handles == 2 && setups == 2 && writes == 2 && !deaths);
        scenarios++;
    }
    const unsigned lengths[] = {0, 1, 63, 64, 65, 128, 4095, 4096, 8192};
    for (unsigned i = 0; i < sizeof(lengths) / sizeof(*lengths); i++) {
        reset(&s); result = USB_RET_SUCCESS;
        unsigned len = lengths[i];
        struct ohci_ed ed = transfer(0, OHCI_TD_DIR_OUT, 1, len, false);
        assert(!ohci_service_td(&s, &ed) && writes == 1 && handles == 1);
        assert(s.usb_packet.len == (len < 64 ? len : 64));
        assert((ed.head & OHCI_DPTR_MASK) == (len <= 64 ? 0x300u : 0x100u));
        assert(descriptors[0].cbp == (len <= 64 ? 0 : 0x1040u)); scenarios++;
    }
    const int statuses[] = { USB_RET_SUCCESS, USB_RET_NAK, USB_RET_STALL, USB_RET_IOERROR, USB_RET_NODEV, USB_RET_BABBLE };
    for (unsigned i = 0; i < sizeof(statuses) / sizeof(*statuses); i++) {
        reset(&s); result = statuses[i];
        struct ohci_ed ed = transfer(0, OHCI_TD_DIR_OUT, 1, 32, false);
        int ret = ohci_service_td(&s, &ed);
        assert(ret == (result == USB_RET_SUCCESS ? 0 : 1));
        assert(!s.async_td && handles == 1 && setups == 1 && !deaths);
        assert(writes == (result == USB_RET_NAK ? 0 : 1)); scenarios++;
    }
    reset(&s); struct ohci_ed invalid = {0};
    assert(ohci_service_td(&s, &invalid) == 1 && deaths == 1 && !reads && !handles);
    printf("OHCI pending payload: %u async/completion/blocked-transfer/synchronous scenarios passed; blocked TDs perform zero DMA reads/copies\n", scenarios + 1);
}
