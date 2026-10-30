/*
 * Test flash: the MCPX AC97 controller's PCM out DMA, timed against the
 * guest's clock. Freestanding C, 32-bit, built into aci.inc by aci.sh and
 * loaded with flash.asm (-DACI), which calls aci_main after the APU is set
 * up.
 *
 * The samples go nowhere in the core, but how fast the controller takes
 * them is what a guest sees: its position, its buffer-complete status and
 * interrupt. The controller is given 32 buffers of 50 ms (2400 frames of
 * 16-bit stereo at 48 kHz) and started; its position is read 100 ms in and
 * again a second later, by the TSC. A second of guest time has to take
 * 96000 samples (one 10 ms audio tick, 960 samples, either way).
 *
 * Results for the harness (--peek):
 *   ACI_STATUS  0 done, else the step that failed
 *   ACI_DELTA   samples taken in that second
 *   ACI_SR      PCM out status at the end (BCIS set, not halted)
 *   ACI_START   samples taken in the first 100 ms
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define ACI_STATUS   0x00300008u
#define ACI_DELTA    0x0030000Cu
#define ACI_SR       0x00300010u
#define ACI_START    0x00300014u

#define BDL          0x00400000u   /* 32 descriptors, 8 bytes each */
#define PCM_BUF      0x00410000u   /* one buffer, shared by all of them */
#define BUF_SAMPLES  4800u         /* 16-bit samples: 2400 stereo frames */

#define PCI_ADDR     0xCF8
#define PCI_DATA     0xCFC
#define ACI_DEV      6             /* 0:6.0, mcpx-aci */
#define ACI_BAR      0xFEC00000u   /* BAR 2: mixer at 0, bus master at 0x100 */

#define NABM         (ACI_BAR + 0x100)
#define PO_BDBAR     0x10
#define PO_CIV       0x14
#define PO_LVI       0x15
#define PO_SR        0x16
#define PO_PICB      0x18
#define PO_CR        0x1B

#define CR_RPBM      1u
#define SR_DCH       1u
#define SR_BCIS      8u
#define BD_IOC       (1u << 31)

static inline void outl(u16 port, u32 v)
{
    __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(port));
}

static inline void wr(u32 a, u32 v)
{
    *(volatile u32 *)a = v;
}

static inline void wr8(u32 a, u8 v)
{
    *(volatile u8 *)a = v;
}

static inline u8 rd8(u32 a)
{
    return *(volatile u8 *)a;
}

static inline u16 rd16(u32 a)
{
    return *(volatile u16 *)a;
}

static inline unsigned long long rdtsc(void)
{
    u32 lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((unsigned long long)hi << 32) | lo;
}

/* the TSC runs at 733.33 MHz of the guest's time; between reads, a
 * register-only count the core's idle detection moves time past (pad.c) */
static void wait_until(unsigned long long t)
{
    while (rdtsc() < t) {
        u32 n = 8192;
        __asm__ volatile("1: dec %0\n\tjnz 1b" : "+r"(n));
    }
}

static void pci_cfg_wr(u32 dev, u32 reg, u32 v)
{
    outl(PCI_ADDR, 0x80000000u | (dev << 11) | (reg & 0xFC));
    outl(PCI_DATA, v);
}

static void fail(u32 step)
{
    wr(ACI_STATUS, step);
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/* samples taken so far: whole buffers before the current one, and what
 * the current one has given; read twice so CIV and PICB agree */
static u32 position(void)
{
    for (;;) {
        u8 civ = rd8(NABM + PO_CIV) & 31;
        u16 picb = rd16(NABM + PO_PICB);
        if ((rd8(NABM + PO_CIV) & 31) == civ) {
            return civ * BUF_SAMPLES + (BUF_SAMPLES - picb);
        }
    }
}

void aci_main(void)
{
    unsigned long long t0;
    u32 p0, p1;
    int i;

    wr(ACI_STATUS, 1);
    pci_cfg_wr(ACI_DEV, 0x18, ACI_BAR);
    pci_cfg_wr(ACI_DEV, 0x04, 0x6);     /* memory space, bus master */

    for (i = 0; i < 32; i++) {
        wr(BDL + 8 * i, PCM_BUF);
        wr(BDL + 8 * i + 4, BD_IOC | BUF_SAMPLES);
    }
    wr(NABM + PO_BDBAR, BDL);
    wr8(NABM + PO_LVI, 31);

    t0 = rdtsc();
    wr8(NABM + PO_CR, CR_RPBM);

    wait_until(t0 + 100ull * 733333);
    p0 = position();
    wait_until(t0 + 1100ull * 733333);
    p1 = position();

    wr(ACI_START, p0);
    wr(ACI_DELTA, p1 - p0);
    wr(ACI_SR, rd16(NABM + PO_SR));
    if (p1 <= p0) {
        fail(2);
    }
    wr(ACI_STATUS, 0);
    for (;;) {
        __asm__ volatile("hlt");
    }
}
