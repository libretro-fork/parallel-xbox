/*
 * Test flash: the hard disk's reads and writes, as the kernel does them.
 * Freestanding C, 32-bit, built into hdd.inc by hdd.sh and run by
 * flash.asm (-DHDD).
 *
 * The hard disk is the primary IDE channel's master, an 8 GB raw image
 * (block/libretro-disk.c). Past its partitions, at HDD_LBA (the last 1 MB
 * of the disk), it is written and read back:
 *
 *   write: 40 sectors by DMA (WRITE DMA) from two PRD entries, 4 KB and
 *          16 KB at scattered addresses, word i of sector L HDD_WORD(L, i)
 *   flush: FLUSH CACHE
 *   read 1: the 40 sectors by DMA (READ DMA) into three entries
 *   read 2: one of them by PIO (READ SECTORS)
 *   read 3: a sector past the disk's end, which must fail (ERR, not a
 *           hang or data)
 *
 * HDD_STATUS is 0 when every step held, else the step that failed;
 * HDD_WORDS counts the words checked, HDD_BAD the first wrong word's
 * address. run.sh checks the image file for the words too.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define HDD_STATUS   0x00300008u
#define HDD_WORDS    0x0030000Cu
#define HDD_BAD      0x00300010u

#define HDD_LBA      0x00FFF800u     /* 8 GB less 1 MB */
#define HDD_SECTORS  0x01000000u     /* the disk's end */
#define HDD_WORD(lba, i) (((u32)(lba) << 7) + (u32)(i) + 0x48440000u)

#define IDE_DATA     0x1F0
#define IDE_COUNT    0x1F2
#define IDE_LBA0     0x1F3
#define IDE_LBA1     0x1F4
#define IDE_LBA2     0x1F5
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
static inline u16 inw(u16 port)
{
    u16 v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
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

/* a 28-bit LBA command to the master */
static int command(u8 cmd, u32 lba, u32 count)
{
    outb(IDE_SELECT, (u8)(0xE0 | ((lba >> 24) & 0x0F)));
    if (ide_wait() & ST_BSY) {
        return -1;
    }
    outb(IDE_COUNT, (u8)count);
    outb(IDE_LBA0, (u8)lba);
    outb(IDE_LBA1, (u8)(lba >> 8));
    outb(IDE_LBA2, (u8)(lba >> 16));
    outb(IDE_CMD, cmd);
    return 0;
}

/* READ DMA (0xC8) or WRITE DMA (0xCA) through the PRD table; 0 when the
 * transfer ended without error */
static int dma(u8 cmd, u32 lba, u32 count)
{
    const int to_memory = cmd == 0xC8;

    outb(BM_CMD, 0);                 /* stopped */
    outl(BM_PRD, PRD_TABLE);
    outb(BM_STATUS, 0x06);           /* interrupt and error cleared */
    outb(BM_CMD, to_memory ? 0x08 : 0x00);
    if (command(cmd, lba, count) < 0) {
        return -1;
    }
    outb(BM_CMD, to_memory ? 0x09 : 0x01); /* start */
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

static void fill(u32 addr, u32 len, u32 lba, u32 at)
{
    for (u32 b = 0; b < len; b += 4) {
        u32 pos = at + b;
        ram32(addr + b, HDD_WORD(lba + pos / 512, (pos % 512) / 4));
    }
}

static int check(u32 addr, u32 len, u32 lba, u32 at)
{
    for (u32 b = 0; b < len; b += 4) {
        u32 pos = at + b;
        if (rd32(addr + b) != HDD_WORD(lba + pos / 512, (pos % 512) / 4)) {
            ram32(HDD_BAD, addr + b);
            return 0;
        }
        ram32(HDD_WORDS, rd32(HDD_WORDS) + 1);
    }
    return 1;
}

static void poison(u32 addr, u32 len)
{
    for (u32 b = 0; b < len; b += 4) {
        ram32(addr + b, 0xDEADBEEFu);
    }
}

void hdd_main(void)
{
    ram32(HDD_STATUS, 1);
    ram32(HDD_WORDS, 0);
    ram32(HDD_BAD, 0);

    /* the IDE controller (bus 0, device 9): bus-master window, I/O and
     * bus mastering on */
    pcicfg(9, 0x20, BM_BASE | 1);
    pcicfg(9, 0x04, 0x00000005u);
    ram32(HDD_STATUS, 2);

    /* write: 40 sectors from 4 KB and 16 KB, scattered */
    fill(0x00500000u, 4096, HDD_LBA, 0);
    fill(0x00520000u, 16384, HDD_LBA, 4096);
    prd(0, 0x00500000u, 4096, 0);
    prd(1, 0x00520000u, 16384, 1);
    if (dma(0xCA, HDD_LBA, 40) < 0) {
        return;
    }
    ram32(HDD_STATUS, 3);

    /* flush */
    if (command(0xE7, 0, 0) < 0 || (ide_wait() & (ST_ERR | ST_BSY))) {
        return;
    }
    ram32(HDD_STATUS, 4);

    /* read 1: three entries */
    poison(0x00600000u, 8192);
    poison(0x00610000u, 2048);
    poison(0x00630000u, 10240);
    prd(0, 0x00600000u, 8192, 0);
    prd(1, 0x00610000u, 2048, 0);
    prd(2, 0x00630000u, 10240, 1);
    if (dma(0xC8, HDD_LBA, 40) < 0) {
        return;
    }
    ram32(HDD_STATUS, 5);
    if (!check(0x00600000u, 8192, HDD_LBA, 0) ||
        !check(0x00610000u, 2048, HDD_LBA, 8192) ||
        !check(0x00630000u, 10240, HDD_LBA, 10240)) {
        return;
    }
    ram32(HDD_STATUS, 6);

    /* read 2: sector 7 of them by PIO */
    poison(0x00700000u, 512);
    if (command(0x20, HDD_LBA + 7, 1) < 0) {
        return;
    }
    if (!(ide_wait() & ST_DRQ)) {
        return;
    }
    for (u32 b = 0; b < 512; b += 2) {
        *(volatile u16 *)(0x00700000u + b) = inw(IDE_DATA);
    }
    ram32(HDD_STATUS, 7);
    if (!check(0x00700000u, 512, HDD_LBA + 7, 0)) {
        return;
    }
    ram32(HDD_STATUS, 8);

    /* read 3: past the end, which must fail */
    prd(0, 0x00700000u, 512, 1);
    if (dma(0xC8, HDD_SECTORS, 1) == 0) {
        return;
    }
    ram32(HDD_STATUS, 0);
}
