/*
 * Test flash: the Xbox pad through the USB controller, as the kernel reads
 * it. Freestanding C, 32-bit, built into pad.inc by pad.sh and loaded with
 * flash.asm (-DPAD), which calls pad_main after the APU is set up.
 *
 * The pad is on the daughterboard's hub (root port 1 of the first OHCI,
 * port 3), behind the controller's own hub (port 1). Each hub is given an
 * address and configured, its port powered and reset, and the pad is read
 * over its interrupt IN endpoint 2, every USB frame, the 20-byte XID report
 * copied to RAM at PAD_REPORT for the harness to look at (--peek). The
 * count of reports is at PAD_COUNT, and PAD_STATUS says how far the setup
 * got (0 done, else the step that failed).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define PAD_STATUS   0x00300008u   /* u32: 0 when the pad is being read */
#define PAD_COUNT    0x0030000Cu   /* u32: reports read */
#define PAD_REPORT   0x00300010u   /* the last XID report, 20 bytes */

/* OHCI's structures in RAM, 256-byte aligned for the HCCA */
#define HCCA         0x00400000u
#define ED_CTRL      0x00400100u
#define ED_INT       0x00400110u
#define TD_BASE      0x00400200u   /* 8 TDs, 16 bytes each */
#define BUF          0x00400400u   /* setup packet and data */

#define PCI_ADDR     0xCF8
#define PCI_DATA     0xCFC
#define OHCI_BAR     0xFED00000u

/* OHCI registers */
#define HcControl        0x04
#define HcCommandStatus  0x08
#define HcInterruptStatus 0x0C
#define HcHCCA           0x18
#define HcControlHeadED  0x20
#define HcControlCurrentED 0x24
#define HcBulkHeadED     0x28
#define HcBulkCurrentED  0x2C
#define HcFmInterval     0x34
#define HcPeriodicStart  0x40
#define HcRhStatus       0x50
#define HcRhPortStatus   0x54

#define HC_OPERATIONAL   (2u << 6)
#define HC_PLE           (1u << 2)
#define HC_CLE           (1u << 4)
#define HC_BLE           (1u << 5)
#define HC_HCR           (1u << 0)
#define HC_CLF           (1u << 1)
#define HC_BLF           (1u << 2)
#define RH_LPSC          (1u << 16)
#define PORT_CCS         (1u << 0)
#define PORT_PES         (1u << 1)
#define PORT_PRS         (1u << 4)
#define PORT_PPS         (1u << 8)
#define PORT_PRSC        (1u << 20)

static inline void outl(u16 port, u32 v)
{
    __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(port));
}

static inline u32 inl(u16 port)
{
    u32 v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline u32 rd(u32 a)
{
    return *(volatile u32 *)a;
}

static inline void wr(u32 a, u32 v)
{
    *(volatile u32 *)a = v;
}

static inline u32 hc_rd(u32 reg)
{
    return rd(OHCI_BAR + reg);
}

static inline void hc_wr(u32 reg, u32 v)
{
    wr(OHCI_BAR + reg, v);
}

static inline unsigned long long rdtsc(void)
{
    u32 lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((unsigned long long)hi << 32) | lo;
}

/* the TSC runs at 733.33 MHz of the guest's time */
static void delay_us(u32 us)
{
    unsigned long long end = rdtsc() + (unsigned long long)us * 733;
    while (rdtsc() < end) {
    }
}

static void pci_cfg_wr(u32 dev, u32 reg, u32 v)
{
    outl(PCI_ADDR, 0x80000000u | (dev << 11) | (reg & 0xFC));
    outl(PCI_DATA, v);
}

static void fail(u32 step)
{
    wr(PAD_STATUS, step);
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/* An endpoint descriptor and a chain of transfer descriptors, run on the
 * control list; true when every TD completed without error */
static u32 td_at(int i)
{
    return TD_BASE + 16u * i;
}

static void td_set(int i, u32 flags, u32 cbp, u32 next, u32 be)
{
    u32 t = td_at(i);
    wr(t + 0, flags);
    wr(t + 4, cbp);
    wr(t + 8, next);
    wr(t + 12, be);
}

/* TD flags: direction (0 setup, 1 out, 2 in), toggle (2 DATA0, 3 DATA1),
 * no completion interrupt, buffer rounding on, condition code "not
 * accessed" */
static u32 td_flags(u32 dir, u32 toggle)
{
    return (0xFu << 28) | (toggle << 24) | (7u << 21) | (dir << 19) |
           (1u << 18);
}

static int run_ed(u32 ed, u32 addr, u32 ep, u32 mps, int ntd, int bulk)
{
    u32 head = td_at(0), tail = td_at(ntd);
    int i;

    td_set(ntd, 0, 0, 0, 0);            /* the dummy at the tail */
    wr(ed + 0, addr | (ep << 7) | (mps << 16));
    wr(ed + 4, tail);
    wr(ed + 8, head);
    wr(ed + 12, 0);
    if (bulk) {
        hc_wr(HcBulkHeadED, ed);
        hc_wr(HcBulkCurrentED, 0);
        hc_wr(HcCommandStatus, HC_BLF);
    } else {
        hc_wr(HcControlHeadED, ed);
        hc_wr(HcControlCurrentED, 0);
        hc_wr(HcCommandStatus, HC_CLF);
    }
    /* each TD takes a USB frame at most; the head reaches the tail when
     * all are done, or halts (bit 0) on an error */
    for (i = 0; i < 300; i++) {
        u32 h = rd(ed + 8);
        if (h & 1) {
            return 0;
        }
        if ((h & ~3u) == tail) {
            break;
        }
        delay_us(1000);
    }
    for (i = 0; i < ntd; i++) {
        if ((rd(td_at(i)) >> 28) != 0) {
            return 0;
        }
    }
    return 1;
}

/* A control transfer: setup, optional data, status */
static int control(u32 addr, u8 type, u8 req, u16 value, u16 index,
                   u16 len, u32 data_in)
{
    u32 setup = BUF;
    int ntd = 0;

    wr(setup, type | (req << 8) | ((u32)value << 16));
    wr(setup + 4, index | ((u32)len << 16));
    td_set(ntd, td_flags(0, 2), setup, td_at(ntd + 1), setup + 7);
    ntd++;
    if (len) {
        td_set(ntd, td_flags(data_in ? 2 : 1, 3), BUF + 16, td_at(ntd + 1),
               BUF + 16 + len - 1);
        ntd++;
    }
    /* status: the other way, zero length */
    td_set(ntd, td_flags(len && data_in ? 1 : 2, 3), 0, td_at(ntd + 1), 0);
    ntd++;
    return run_ed(ED_CTRL, addr, 0, 64, ntd, 0);
}

#define REQ_SET_ADDRESS       5
#define REQ_SET_CONFIGURATION 9
#define HUB_PORT_RESET        4
#define HUB_PORT_POWER        8
#define HUB_C_PORT_RESET      20

/* A hub at addr: power and reset its port, so the device behind it answers
 * at address 0 */
static int hub_reset_port(u32 addr, u16 port)
{
    if (!control(addr, 0x23, 3, HUB_PORT_POWER, port, 0, 0)) {
        return 0;
    }
    delay_us(2000);
    if (!control(addr, 0x23, 3, HUB_PORT_RESET, port, 0, 0)) {
        return 0;
    }
    delay_us(2000);
    return control(addr, 0x23, 1, HUB_C_PORT_RESET, port, 0, 0);
}

/* A hub's status-change report (interrupt IN endpoint 1, one byte: a bit
 * per port with a change), how the kernel learns a device is there; the
 * hub NAKs while nothing changed, and the transfer waits on it. True when
 * the port's bit is set within 300 frames. */
static int hub_change(u32 addr, u32 port)
{
    wr(BUF + 32, 0);
    td_set(0, td_flags(2, 0), BUF + 32, td_at(1), BUF + 32);
    if (!run_ed(ED_INT, addr, 1, 8, 1, 1)) {
        return 0;
    }
    return (rd(BUF + 32) >> port) & 1;
}

static int set_address_and_config(u32 addr)
{
    if (!control(0, 0x00, REQ_SET_ADDRESS, addr, 0, 0, 0)) {
        return 0;
    }
    delay_us(1000);
    return control(addr, 0x00, REQ_SET_CONFIGURATION, 1, 0, 0, 0);
}

void pad_main(void)
{
    int i;

    wr(PAD_STATUS, 0xFFFFFFFFu);
    wr(PAD_COUNT, 0);

    /* The OHCI the hubs hang off: QEMU's first USB bus, which is the
     * first pci-ohci realized (hw/xbox/xbox.c), PCI device 3 on an Xbox;
     * the other one when its root port 1 has nothing. Its registers at
     * OHCI_BAR, memory decode and bus mastering on */
    for (i = 0; i < 2; i++) {
        u32 dev = i ? 2 : 3;

        pci_cfg_wr(dev, 0x10, OHCI_BAR);
        pci_cfg_wr(dev, 0x04, 0x00000006);
        hc_wr(HcRhStatus, RH_LPSC);
        hc_wr(HcRhPortStatus, PORT_PPS);
        delay_us(1000);
        if (hc_rd(HcRhPortStatus) & PORT_CCS) {
            break;
        }
        pci_cfg_wr(dev, 0x04, 0);
    }
    if (i == 2) {
        fail(1);
    }

    /* reset the controller and bring it up operational, with the
     * periodic, control and bulk lists on, frames of 12000 bits, ports
     * powered */
    hc_wr(HcCommandStatus, HC_HCR);
    for (i = 0; i < 100 && (hc_rd(HcCommandStatus) & HC_HCR); i++) {
        delay_us(10);
    }
    hc_wr(HcFmInterval, (0x2778u << 16) | 0x2EDF);
    hc_wr(HcPeriodicStart, 0x2A2F);
    for (i = 0; i < 0x100; i += 4) {
        wr(HCCA + i, 0);
    }
    hc_wr(HcHCCA, HCCA);
    hc_wr(HcControlHeadED, 0);
    hc_wr(HcBulkHeadED, 0);
    hc_wr(HcControl, HC_OPERATIONAL | HC_PLE | HC_CLE | HC_BLE);
    hc_wr(HcRhStatus, RH_LPSC);
    delay_us(2000);

    /* root port 1: the daughterboard's hub */
    hc_wr(HcRhPortStatus, PORT_PPS);
    delay_us(2000);
    hc_wr(HcRhPortStatus, PORT_PRS);
    for (i = 0; i < 100 && !(hc_rd(HcRhPortStatus) & PORT_PRSC); i++) {
        delay_us(100);
    }
    hc_wr(HcRhPortStatus, PORT_PRSC);
    if (!(hc_rd(HcRhPortStatus) & PORT_PES)) {
        fail(2);
    }
    delay_us(1000);

    if (!set_address_and_config(1)) {         /* the daughterboard's hub */
        fail(3);
    }
    if (!hub_change(1, 3)) {                  /* it reports its port 3 */
        fail(4);
    }
    if (!hub_reset_port(1, 3)) {              /* port 3: pad 1's hub */
        fail(5);
    }
    if (!set_address_and_config(2)) {
        fail(6);
    }
    if (!hub_change(2, 1)) {                  /* it reports its port 1 */
        fail(7);
    }
    if (!hub_reset_port(2, 1)) {              /* port 1: the pad */
        fail(8);
    }
    if (!set_address_and_config(3)) {
        fail(9);
    }
    wr(PAD_STATUS, 0);

    /* The pad's reports over its interrupt IN endpoint, on the periodic
     * list, as the kernel takes them: the ED in every slot of the
     * interrupt table, served each frame; one TD at a time, the next
     * given when it is done */
    wr(ED_INT + 0, 3 | (2u << 7) | (32u << 16));
    wr(ED_INT + 12, 0);
    for (;;) {
        u32 n;

        td_set(0, td_flags(2, 0), BUF + 64, td_at(1), BUF + 64 + 31);
        td_set(1, 0, 0, 0, 0);
        wr(ED_INT + 4, td_at(1));
        wr(ED_INT + 8, td_at(0));
        for (i = 0; i < 32; i++) {
            wr(HCCA + 4 * i, ED_INT);
        }
        for (i = 0; i < 300; i++) {
            u32 h = rd(ED_INT + 8);
            if (h & 1) {
                fail(10);
            }
            if ((h & ~3u) == td_at(1)) {
                break;
            }
            delay_us(500);
        }
        if (i == 300 || (rd(td_at(0)) >> 28) != 0) {
            fail(11);
        }
        for (i = 0; i < 20; i += 4) {
            wr(PAD_REPORT + i, rd(BUF + 64 + i));
        }
        n = rd(PAD_COUNT);
        wr(PAD_COUNT, n + 1);
    }
}
