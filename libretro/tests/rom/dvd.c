/*
 * Test flash: DVD reads by DMA, as the kernel does them. Freestanding C,
 * 32-bit, built into dvd.inc by dvd.sh and run by flash.asm (-DDVD).
 *
 * The DVD drive is the primary IDE channel's slave. The PIIX3's bus-master
 * DMA is given its I/O window (BAR4) and enabled; the drive is asked TEST
 * UNIT READY until it has settled after its disc went in, then three
 * READ(10) packets are sent with DMA into the PRD tables below, and every
 * 32-bit word read is checked against the pattern mkdisc.c stamps into the
 * test disc: word i of sector L is DISC_WORD(L, i).
 *
 *   read 1: 16 sectors from LBA 16, one PRD entry
 *   read 2: 40 sectors from LBA 100, three entries of 4, 64 and 12 KB at
 *           scattered addresses
 *   read 3: 300 sectors from LBA 2000 (600 KB), ten entries
 *
 * DVD_STATUS is 0 when every read came back whole, else the step that
 * failed; DVD_WORDS counts the words checked, DVD_BAD the first wrong
 * word's address.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define DVD_STATUS   0x00300008u
#define DVD_WORDS    0x0030000Cu
#define DVD_BAD      0x00300010u

#define DISC_WORD(lba, i) (((u32)(lba) << 9) + (u32)(i) + 0x10000000u)

#define IDE_DATA     0x1F0
#define IDE_FEATURES 0x1F1
#define IDE_LCYL     0x1F4
#define IDE_HCYL     0x1F5
#define IDE_SELECT   0x1F6
#define IDE_CMD      0x1F7  /* status on read */
#define IDE_ALTSTAT  0x3F6
#define BM_BASE      0xFF60 /* BAR4: the primary channel's at +0 */
#define BM_CMD       (BM_BASE + 0)
#define BM_STATUS    (BM_BASE + 2)
#define BM_PRD       (BM_BASE + 4)

#define ST_ERR  0x01
#define ST_DRQ  0x08
#define ST_BSY  0x80

#define PRD_TABLE    0x00480000u

static inline void outb(u16 port, u8 v)
{
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}
static inline void outw(u16 port, u16 v)
{
    __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"(port));
}
static inline void outl(u16 port, u32 v)
{
    __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(port));
}
static inline u8 inb(u16 port)
{
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void ram32(u32 addr, u32 v) { *(volatile u32 *)addr = v; }
static inline u32 rd32(u32 addr) { return *(volatile u32 *)addr; }

static void pcicfg(u32 dev, u32 reg, u32 v)
{
    outl(0xCF8, 0x80000000u | (dev << 11) | reg);
    outl(0xCFC, v);
}

/* BSY clear; the status */
static u8 ide_wait(void)
{
    u8 st;
    for (u32 i = 0; i < 100000000u; i++) {
        st = inb(IDE_ALTSTAT);
        if (!(st & ST_BSY)) {
            return inb(IDE_CMD);
        }
    }
    return 0xFF;
}

/* A packet command: 12 bytes after PACKET, DMA or not */
static int packet(const u8 *cdb, int dma)
{
    outb(IDE_SELECT, 0xB0); /* the slave */
    if (ide_wait() & ST_BSY) {
        return -1;
    }
    outb(IDE_FEATURES, dma ? 1 : 0);
    outb(IDE_LCYL, 0xFE);
    outb(IDE_HCYL, 0xFF);
    outb(IDE_CMD, 0xA0);
    u8 st = ide_wait();
    if (!(st & ST_DRQ)) {
        return -2;
    }
    for (int i = 0; i < 12; i += 2) {
        outw(IDE_DATA, (u16)(cdb[i] | (cdb[i + 1] << 8)));
    }
    return 0;
}

static int unit_ready(void)
{
    static const u8 tur[12] = { 0 };
    for (int tries = 0; tries < 10; tries++) {
        if (packet(tur, 0) < 0) {
            return 0;
        }
        u8 st = ide_wait();
        if (!(st & (ST_ERR | ST_BSY))) {
            return 1;
        }
    }
    return 0;
}

/* READ(10) of count sectors from lba by DMA into the PRD table at
 * PRD_TABLE; 0 when the transfer ended without error */
static int read_dma(u32 lba, u32 count)
{
    u8 cdb[12] = { 0x28, 0,
                   (u8)(lba >> 24), (u8)(lba >> 16), (u8)(lba >> 8), (u8)lba,
                   0, (u8)(count >> 8), (u8)count, 0, 0, 0 };

    outb(BM_CMD, 0);                 /* stopped */
    outl(BM_PRD, PRD_TABLE);
    outb(BM_STATUS, 0x06);           /* interrupt and error cleared */
    outb(BM_CMD, 0x08);              /* to memory */
    if (packet(cdb, 1) < 0) {
        return -1;
    }
    outb(BM_CMD, 0x09);              /* start */
    for (u32 i = 0; i < 100000000u; i++) {
        u8 bs = inb(BM_STATUS);
        if (bs & 0x04) {             /* the transfer's interrupt */
            break;
        }
        u8 st = inb(IDE_ALTSTAT);
        if ((st & ST_ERR) && !(st & ST_BSY)) {
            outb(BM_CMD, 0);
            return -2;
        }
    }
    outb(BM_CMD, 0);
    u8 st = ide_wait();
    if (st & (ST_ERR | ST_BSY)) {
        return -3;
    }
    return (inb(BM_STATUS) & 0x02) ? -4 : 0;
}

static void prd(int i, u32 addr, u32 len, int last)
{
    ram32(PRD_TABLE + i * 8, addr);
    ram32(PRD_TABLE + i * 8 + 4, (len & 0xFFFF) | (last ? 0x80000000u : 0));
}

/* The words of a piece of the read, against the disc's pattern; the
 * piece starts at byte offset `at` of the transfer from `lba` */
static int check(u32 addr, u32 len, u32 lba, u32 at)
{
    for (u32 b = 0; b < len; b += 4) {
        u32 pos = at + b;
        u32 want = DISC_WORD(lba + pos / 2048, (pos % 2048) / 4);
        if (rd32(addr + b) != want) {
            ram32(DVD_BAD, addr + b);
            return 0;
        }
        ram32(DVD_WORDS, rd32(DVD_WORDS) + 1);
    }
    return 1;
}

/* Fills the destinations with what the disc never holds, so a piece not
 * written shows */
static void poison(u32 addr, u32 len)
{
    for (u32 b = 0; b < len; b += 4) {
        ram32(addr + b, 0xDEADBEEFu);
    }
}

void dvd_main(void)
{
    ram32(DVD_STATUS, 1);
    ram32(DVD_WORDS, 0);
    ram32(DVD_BAD, 0);

    /* the IDE controller (bus 0, device 9): bus-master window, I/O and
     * bus mastering on */
    pcicfg(9, 0x20, BM_BASE | 1);
    pcicfg(9, 0x04, 0x00000005u);
    ram32(DVD_STATUS, 2);

    if (!unit_ready()) {
        return;
    }
    ram32(DVD_STATUS, 3);

    /* read 1: one entry */
    poison(0x00500000u, 16 * 2048);
    prd(0, 0x00500000u, 16 * 2048, 1);
    if (read_dma(16, 16) < 0) {
        return;
    }
    ram32(DVD_STATUS, 4);
    if (!check(0x00500000u, 16 * 2048, 16, 0)) {
        return;
    }
    ram32(DVD_STATUS, 5);

    /* read 2: three entries, scattered */
    poison(0x00600000u, 4096);
    poison(0x00610000u, 65536);
    poison(0x00630000u, 12288);
    prd(0, 0x00600000u, 4096, 0);
    prd(1, 0x00610000u, 0, 0);       /* 0: 64 KB */
    prd(2, 0x00630000u, 12288, 1);
    if (read_dma(100, 40) < 0) {
        return;
    }
    ram32(DVD_STATUS, 6);
    if (!check(0x00600000u, 4096, 100, 0) ||
        !check(0x00610000u, 65536, 100, 4096) ||
        !check(0x00630000u, 12288, 100, 4096 + 65536)) {
        return;
    }
    ram32(DVD_STATUS, 7);

    /* read 3: 600 KB, ten entries */
    poison(0x00700000u, 300 * 2048);
    for (int i = 0; i < 10; i++) {
        u32 len = i < 9 ? 65536 : 300 * 2048 - 9 * 65536;
        prd(i, 0x00700000u + i * 65536, len == 65536 ? 0 : len, i == 9);
    }
    if (read_dma(2000, 300) < 0) {
        return;
    }
    ram32(DVD_STATUS, 8);
    if (!check(0x00700000u, 300 * 2048, 2000, 0)) {
        return;
    }
    ram32(DVD_STATUS, 0);
}
