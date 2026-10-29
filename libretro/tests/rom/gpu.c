/*
 * Test flash: the NV2A from bare metal, for the display and renderer
 * tests. Freestanding C, 32-bit, built into gpu.inc by gpu.sh and run by
 * flash.asm (-DGPU) after the APU is set up.
 *
 * The AGP bridge is given its bus numbers and windows and the NV2A its
 * BARs, as the kernel does; one PFIFO channel (0) runs in DMA mode from a
 * pushbuffer in RAM, with its objects in RAMIN: the pushbuffer's and the
 * RAM's DMA objects, the Kelvin (3D) object, and the hash table that names
 * them. The CRTC shows 640x480 at 32 bits from the address PCRTC_START
 * gives. Time is the guest's vblanks (PCRTC's interrupt, polled): one a
 * frame of the core.
 *
 * The scenario, its frames counted from the first vblank, the colours
 * those the harness reads at the picture's centre (--pixel):
 *
 *   frame 0    surface A cleared red, the scanout moved to A
 *   every frame from 1: surface B cleared (A never drawn to again) and a
 *              flip counted (FLIP_INCREMENT_WRITE), so the renderer's
 *              frame count runs on and A is a surface shown, not drawn
 *   frame 20   the CPU reads A's first and last pixels back (GPU_READ0,
 *              GPU_READ1): the renderer downloads the surface for it, and
 *              both must be red
 *   frame 30   the CPU writes A green, all of it, as a movie decoder or a
 *              CPU-drawn loading screen writes a front buffer
 *
 * The picture must be red before frame 30 (not black: the surface on
 * screen is not dropped as an old one) and green after (a CPU write to
 * the front buffer is a new picture).
 *
 *   frame 60   a swizzled surface C, 32 x 16, its pitch set to half a row,
 *              cleared red on its left half and blue on its right; at frame
 *              61 the CPU reads all its texels where the NV2A lays them out:
 *              by width and height alone, the pitch ignored (as hardware
 *              does, nxdk_pgraph_tests surface_pitch). The count of wrong
 *              ones is at GPU_SWZ_BAD, the first wrong word at GPU_SWZ_WORD.
 *              Taking the pitch as the row folded the rows onto each other
 *   frame 62   B's depth and stencil cleared (Z24S8); at frame 63 the CPU
 *              reads its first and last words back (GPU_ZETA0, GPU_ZETA1):
 *              both the clear value
 *
 *   from frame 50: into B, a triangle drawn, B cleared, the same triangle
 *              drawn again: draws and clears on both renderers, the
 *              picture (A) unchanged by them
 *   from frame 100: after that, until the vblank and at most TRIPS_MAX
 *              times, the GPU given a run of no-ops (NOP_SUB, 8 times)
 *              and waited for on a semaphore in RAM: the fewest of these
 *              round trips in a frame (from frame 101, the first whole
 *              one) at GPU_TRIPS, TRIPS_MAX when each wait took the
 *              guest no more than the GPU's time. Its time moving on to
 *              its next deadline while it waited instead made 1; Ninja
 *              Gaiden Black, waiting so on its GPU, ran in slow motion
 *
 * The frame count is at GPU_FRAMES in RAM, GPU_STATUS says how far the
 * setup got (0 running).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

typedef unsigned char u8;
typedef unsigned int u32;

#define GPU_STATUS   0x00300008u   /* u32: 0 once running */
#define GPU_FRAMES   0x0030000Cu   /* u32: vblanks seen */
#define GPU_READ0    0x00300014u   /* u32: A's first pixel, read at frame 20 */
#define GPU_READ1    0x00300018u   /* u32: A's last pixel, read at frame 20 */
#define GPU_TRIPS    0x0030001Cu   /* u32: fewest GPU round trips in a frame */
#define GPU_SEM      0x00300020u   /* u32: the semaphore the GPU writes */
#define GPU_SWZ_BAD  0x00300024u   /* u32: C's wrong texels, read at frame 61 */
#define GPU_SWZ_WORD 0x00300028u   /* u32: the first wrong one */
#define GPU_ZETA0    0x0030002Cu   /* u32: B's first depth word, frame 63 */
#define GPU_ZETA1    0x00300030u   /* u32: B's last depth word, frame 63 */
/* 0x00300010: the pushbuffer's write offset (put) */

#define NV2A         0xFD000000u   /* BAR0, the registers */
#define NV2A_VRAM    0xF0000000u   /* BAR1 */
#define RAMIN        (NV2A + 0x700000u)
#define USER         (NV2A + 0x800000u)

#define PMC_ENABLE        0x000200
#define PFIFO_RAMHT       0x002210
#define PFIFO_MODE        0x002504
#define PFIFO_PUSH0       0x003200
#define PFIFO_PUSH1       0x003204
#define PFIFO_DMA_PUSH    0x003220
#define PFIFO_DMA_INST    0x00322C
#define PFIFO_DMA_PUT     0x003240
#define PFIFO_DMA_GET     0x003244
#define PFIFO_PULL0       0x003250
#define PGRAPH_INTR_EN    0x400140
#define PGRAPH_CTX_CTRL   0x400144
#define PGRAPH_CTX_USER   0x400148
#define PGRAPH_FIFO       0x400720
#define PCRTC_INTR        0x600100
#define PCRTC_INTR_EN     0x600140
#define PCRTC_START       0x600800
#define PRMVIO_MISC_W     0x0C03C2
#define PRMCIO_CRX        0x6013D4
#define PRMCIO_CR         0x6013D5

/* RAM: the pushbuffer and the surfaces (the NV2A's memory is the RAM) */
#define PUSHBUF      0x00800000u
#define PUSHBUF_SIZE 0x00010000u
/* a pushbuffer subroutine of 14329 no-ops: GPU work that takes the GPU's
 * thread a while and any renderer the same */
#define NOP_SUB      PUSHBUF_SIZE
#define NV097_NO_OPERATION 0x0100
#define TRIPS_MAX    4
#define SURF_A       0x01000000u
#define SURF_B       0x01200000u
#define WIDTH        640
#define HEIGHT       480
#define PITCH        (WIDTH * 4)
#define SURF_C       0x01800000u   /* swizzled, C_W x C_H */
#define C_LOG_W      5
#define C_LOG_H      4
#define C_W          (1u << C_LOG_W)
#define C_H          (1u << C_LOG_H)
#define ZETA_B       (SURF_B + 0x200000u)
#define ZS_CLEAR     0x12345678u   /* depth 0x123456, stencil 0x78 */

/* RAMIN: the hash table, then the objects (16 bytes each) */
#define RAMHT        0x10000u
#define OBJ_PUSHBUF  0x11000u
#define OBJ_RAM      0x11010u
#define OBJ_KELVIN   0x11020u
#define H_RAM        0x00000101u   /* the objects' handles */
#define H_KELVIN     0x00000102u

#define NV097_SET_OBJECT                0x0000
#define NV097_SET_FLIP_READ             0x0120
#define NV097_SET_FLIP_WRITE            0x0124
#define NV097_SET_FLIP_MODULO           0x0128
#define NV097_FLIP_INCREMENT_WRITE      0x012C
#define NV097_SET_COLOR_MASK            0x0358
#define NV097_SET_DEPTH_MASK            0x035C
#define NV097_SET_SEMAPHORE_OFFSET      0x1D6C
#define NV097_BACK_END_WRITE_SEMAPHORE_RELEASE 0x1D70
#define NV097_SET_CONTEXT_DMA_NOTIFIES  0x0180
#define NV097_SET_CONTEXT_DMA_A         0x0184
#define NV097_SET_CONTEXT_DMA_B         0x0188
#define NV097_SET_CONTEXT_DMA_COLOR     0x0194
#define NV097_SET_CONTEXT_DMA_ZETA      0x0198
#define NV097_SET_CONTEXT_DMA_VERTEX_A  0x019C
#define NV097_SET_CONTEXT_DMA_VERTEX_B  0x01A0
#define NV097_SET_CONTEXT_DMA_SEMAPHORE 0x01A4
#define NV097_SET_CONTEXT_DMA_REPORT    0x01A8
#define NV097_SET_SURFACE_CLIP_H        0x0200
#define NV097_SET_SURFACE_CLIP_V        0x0204
#define NV097_SET_SURFACE_FORMAT        0x0208
#define NV097_SET_SURFACE_PITCH         0x020C
#define NV097_SET_SURFACE_COLOR_OFFSET  0x0210
#define NV097_SET_SURFACE_ZETA_OFFSET   0x0214
#define NV097_SET_VERTEX_DATA_ARRAY_FORMAT 0x1760
#define NV097_SET_BEGIN_END             0x17FC
#define NV097_INLINE_ARRAY              0x1818
#define NV097_SET_ZSTENCIL_CLEAR_VALUE  0x1D8C
#define NV097_SET_COLOR_CLEAR_VALUE     0x1D90
#define NV097_CLEAR_SURFACE             0x1D94
#define NV097_SET_CLEAR_RECT_H          0x1D98
#define NV097_SET_CLEAR_RECT_V          0x1D9C

static inline void wr(u32 reg, u32 v) { *(volatile u32 *)(NV2A + reg) = v; }
static inline u32 rd(u32 reg) { return *(volatile u32 *)(NV2A + reg); }
static inline void wr8(u32 reg, u8 v) { *(volatile u8 *)(NV2A + reg) = v; }
static inline void ram32(u32 addr, u32 v) { *(volatile u32 *)addr = v; }
static inline u32 rd32(u32 addr) { return *(volatile u32 *)addr; }

static inline void outl(unsigned short port, u32 v)
{
    __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(port));
}

static void pcicfg(u32 bus, u32 dev, u32 reg, u32 v)
{
    outl(0xCF8, 0x80000000u | (bus << 16) | (dev << 11) | reg);
    outl(0xCFC, v);
}

static void cr(u8 index, u8 v)
{
    wr8(PRMCIO_CRX, index);
    wr8(PRMCIO_CR, v);
}

/* A DMA object over [base, base + limit] of the RAM: class 0x3D */
static void dma_object(u32 at, u32 base, u32 limit)
{
    ram32(RAMIN + at + 0, 0x0000003Du | ((base & 0xFFFu) << 20));
    ram32(RAMIN + at + 4, limit);
    ram32(RAMIN + at + 8, (base & 0xFFFFF000u) | 3u);
    ram32(RAMIN + at + 12, 0);
}

/* The hash table's slot for a handle, as the NV2A computes it: 4 KB, so
 * 11-bit chunks folded, the channel (0) above them */
static u32 ramht_slot(u32 handle)
{
    u32 hash = 0;
    while (handle) {
        hash ^= handle & 0x7FFu;
        handle >>= 11;
    }
    return hash;
}

static void ramht_add(u32 handle, u32 instance)
{
    u32 at = RAMHT + ramht_slot(handle) * 8;
    ram32(RAMIN + at, handle);
    /* the instance in 16-byte units, the graphics engine, valid */
    ram32(RAMIN + at + 4, (instance >> 4) | (1u << 16) | (1u << 31));
}

/* the pushbuffer's write offset: in RAM, the code being in the flash */
#define put (*(volatile u32 *)0x00300010u)

static void push(u32 word)
{
    ram32(PUSHBUF + put, word);
    put += 4;
}

static void method(u32 m, u32 v)
{
    push((1u << 18) | (0u << 13) | m); /* one word, subchannel 0 */
    push(v);
}

/* Hands the words written to the GPU and waits until it has read them */
static void kick(void)
{
    *(volatile u32 *)(USER + 0x40) = put;
    while (*(volatile u32 *)(USER + 0x44) != put) {
    }
    if (put > PUSHBUF_SIZE - 0x400) {
        /* back to the start: a jump, then the pointers */
        push(0x20000000u | 0);
        *(volatile u32 *)(USER + 0x40) = put;
        while (*(volatile u32 *)(USER + 0x44) != 0) {
        }
        put = 0;
        *(volatile u32 *)(USER + 0x40) = 0;
    }
}

static void clear(u32 surface, u32 argb)
{
    method(NV097_SET_SURFACE_COLOR_OFFSET, surface);
    method(NV097_SET_COLOR_CLEAR_VALUE, argb);
    method(NV097_SET_CLEAR_RECT_H, (u32)(WIDTH - 1) << 16);
    method(NV097_SET_CLEAR_RECT_V, (u32)(HEIGHT - 1) << 16);
    method(NV097_CLEAR_SURFACE, 0xF0); /* R, G, B, A */
}

/* A triangle from the inline array: position only, 4 floats a vertex */
/* Hands the words written to the GPU without waiting for it, unless the
 * ring is about to wrap */
static void post(void)
{
    if (put > PUSHBUF_SIZE - 0x400) {
        kick();
        return;
    }
    *(volatile u32 *)(USER + 0x40) = put;
}

static void triangle(void)
{
    static const u32 v[12] = {
        0x43200000u, 0x42C80000u, 0, 0x3F800000u,   /* 160, 100 */
        0x43F00000u, 0x42C80000u, 0, 0x3F800000u,   /* 480, 100 */
        0x43A00000u, 0x43C80000u, 0, 0x3F800000u,   /* 320, 400 */
    };
    method(NV097_SET_BEGIN_END, 5); /* triangles */
    push(0x40000000u | (12u << 18) | (0u << 13) | NV097_INLINE_ARRAY);
    for (int i = 0; i < 12; i++) {
        push(v[i]);
    }
    method(NV097_SET_BEGIN_END, 0);
}

/* x's bits and y's interleaved from bit 0, x first, until the shorter
 * runs out (swizzle.c generate_swizzle_masks) */
static __attribute__((noinline)) u32 swizzled(u32 x, u32 y)
{
    u32 s = 0, bit = 0;
    for (u32 i = 0; i < 16; i++) {
        if ((1u << i) < C_W) {
            s |= ((x >> i) & 1u) << bit++;
        }
        if ((1u << i) < C_H) {
            s |= ((y >> i) & 1u) << bit++;
        }
    }
    return s;
}

/* C: swizzled, its pitch half a row; red left, blue right. Then B's
 * shape back. */
static __attribute__((noinline)) void draw_swizzled(void)
{
    method(NV097_SET_SURFACE_CLIP_H, C_W << 16);
    method(NV097_SET_SURFACE_CLIP_V, C_H << 16);
    method(NV097_SET_SURFACE_FORMAT, 0x08u | (2u << 4) | (2u << 8) |
                                         ((u32)C_LOG_W << 16) |
                                         ((u32)C_LOG_H << 24));
    method(NV097_SET_SURFACE_PITCH, (C_W * 2) | ((C_W * 2) << 16));
    method(NV097_SET_SURFACE_COLOR_OFFSET, SURF_C);
    method(NV097_SET_COLOR_CLEAR_VALUE, 0xFFFF0000u);
    method(NV097_SET_CLEAR_RECT_H, (C_W / 2 - 1) << 16);
    method(NV097_SET_CLEAR_RECT_V, (C_H - 1) << 16);
    method(NV097_CLEAR_SURFACE, 0xF0);
    method(NV097_SET_COLOR_CLEAR_VALUE, 0xFF0000FFu);
    method(NV097_SET_CLEAR_RECT_H, ((C_W - 1) << 16) | (C_W / 2));
    method(NV097_CLEAR_SURFACE, 0xF0);
    method(NV097_SET_SURFACE_CLIP_H, (u32)WIDTH << 16);
    method(NV097_SET_SURFACE_CLIP_V, (u32)HEIGHT << 16);
    method(NV097_SET_SURFACE_FORMAT, 0x08u | (2u << 4) | (1u << 8));
    method(NV097_SET_SURFACE_PITCH, PITCH | ((u32)PITCH << 16));
}

static __attribute__((noinline)) void check_swizzled(void)
{
    u32 bad = 0, word = 0;
    for (u32 y = 0; y < C_H; y++) {
        for (u32 x = 0; x < C_W; x++) {
            u32 v = rd32(SURF_C + swizzled(x, y) * 4);
            u32 want = x < C_W / 2 ? 0xFFFF0000u : 0xFF0000FFu;
            if (v != want && !bad++) {
                word = v;
            }
        }
    }
    ram32(GPU_SWZ_WORD, word);
    ram32(GPU_SWZ_BAD, bad);
}

static void wait_vblank(void)
{
    while (!(rd(PCRTC_INTR) & 1)) {
    }
    wr(PCRTC_INTR, 1);
    (*(volatile u32 *)GPU_FRAMES)++;
}

void gpu_main(void)
{
    ram32(GPU_STATUS, 1);
    ram32(GPU_FRAMES, 0);
    ram32(GPU_READ0, 0);
    ram32(GPU_READ1, 0);
    ram32(GPU_TRIPS, 0);
    ram32(GPU_SEM, 0);
    ram32(GPU_SWZ_BAD, 0xFFFFFFFFu);
    ram32(GPU_SWZ_WORD, 0);
    ram32(GPU_ZETA0, 0);
    ram32(GPU_ZETA1, 0);
    /* the no-op subroutine: seven no-op methods of 2047 words, a return */
    {
        u32 at = PUSHBUF + NOP_SUB;
        for (int k = 0; k < 7; k++) {
            ram32(at, 0x40000000u | (2047u << 18) | NV097_NO_OPERATION);
            at += 4;
            for (int i = 0; i < 2047; i++, at += 4) {
                ram32(at, 0);
            }
        }
        ram32(at, 0x00020000u);
    }
    put = 0;

    /* the AGP bridge (bus 0, device 30): bus 1 behind it, the NV2A's
     * registers and memory in its windows; then the NV2A's BARs */
    pcicfg(0, 30, 0x18, 0x00010100u);
    pcicfg(0, 30, 0x20, 0xFDF0FD00u);
    pcicfg(0, 30, 0x24, 0xF7F0F000u);
    pcicfg(0, 30, 0x04, 0x00000007u);
    pcicfg(1, 0, 0x10, NV2A);
    pcicfg(1, 0, 0x14, NV2A_VRAM);
    pcicfg(1, 0, 0x04, 0x00000006u);
    ram32(GPU_STATUS, 2);

    wr(PMC_ENABLE, 0xFFFFFFFFu);

    /* the display: colour addressing (the CRTC at 0x3D4), CR0-7 writable,
     * then 640x480, 32 bits, a line of PITCH bytes */
    wr8(PRMVIO_MISC_W, 0x01);
    cr(0x11, 0x00);
    cr(0x01, WIDTH / 8 - 1);
    cr(0x12, (HEIGHT - 1) & 0xFF);
    cr(0x07, (((HEIGHT - 1) >> 8) & 1) << 1 | (((HEIGHT - 1) >> 9) & 1) << 6);
    cr(0x13, (PITCH / 8) & 0xFF);
    cr(0x19, (((PITCH / 8) >> 8) & 7) << 5);
    cr(0x28, 3);
    wr(PCRTC_INTR_EN, 1);
    ram32(GPU_STATUS, 3);

    /* RAMIN: the objects and the table */
    for (u32 i = 0; i < 0x1000; i += 4) {
        ram32(RAMIN + RAMHT + i, 0);
    }
    /* the ring, and after it the subroutine of no-ops (NOP_SUB) */
    dma_object(OBJ_PUSHBUF, PUSHBUF, 2 * PUSHBUF_SIZE - 1);
    dma_object(OBJ_RAM, 0, 0x03FFFFFFu);
    ram32(RAMIN + OBJ_KELVIN + 0, 0x97);
    ram32(RAMIN + OBJ_KELVIN + 4, 0);
    ram32(RAMIN + OBJ_KELVIN + 8, 0);
    ram32(RAMIN + OBJ_KELVIN + 12, 0);
    ramht_add(H_RAM, OBJ_RAM);
    ramht_add(H_KELVIN, OBJ_KELVIN);

    /* PGRAPH: channel 0 loaded, the FIFO let through */
    wr(PGRAPH_INTR_EN, 0);
    wr(PGRAPH_CTX_USER, 0);
    wr(PGRAPH_CTX_CTRL, 1u << 16);
    wr(PGRAPH_FIFO, 1);

    /* PFIFO: channel 0 in DMA mode from the pushbuffer */
    wr(PFIFO_RAMHT, (RAMHT >> 12) << 4);
    wr(PFIFO_MODE, 1);
    wr(PFIFO_PUSH1, 1u << 8);
    wr(PFIFO_DMA_INST, OBJ_PUSHBUF >> 4);
    wr(PFIFO_DMA_PUT, 0);
    wr(PFIFO_DMA_GET, 0);
    wr(PFIFO_PUSH0, 1);
    wr(PFIFO_DMA_PUSH, 1);
    wr(PFIFO_PULL0, 1);
    ram32(GPU_STATUS, 4);

    /* the Kelvin object and its surfaces: colour only, A8R8G8B8, pitch */
    method(NV097_SET_OBJECT, H_KELVIN);
    method(NV097_SET_CONTEXT_DMA_NOTIFIES, H_RAM);
    method(NV097_SET_CONTEXT_DMA_A, H_RAM);
    method(NV097_SET_CONTEXT_DMA_B, H_RAM);
    method(NV097_SET_CONTEXT_DMA_COLOR, H_RAM);
    method(NV097_SET_CONTEXT_DMA_ZETA, H_RAM);
    method(NV097_SET_CONTEXT_DMA_VERTEX_A, H_RAM);
    method(NV097_SET_CONTEXT_DMA_VERTEX_B, H_RAM);
    method(NV097_SET_CONTEXT_DMA_SEMAPHORE, H_RAM);
    method(NV097_SET_CONTEXT_DMA_REPORT, H_RAM);
    method(NV097_SET_SURFACE_CLIP_H, (u32)WIDTH << 16);
    method(NV097_SET_SURFACE_CLIP_V, (u32)HEIGHT << 16);
    method(NV097_SET_SURFACE_FORMAT, 0x08u | (2u << 4) | (1u << 8));
    method(NV097_SET_SURFACE_PITCH, PITCH | ((u32)PITCH << 16));
    method(NV097_SET_SURFACE_ZETA_OFFSET, ZETA_B);
    method(NV097_SET_FLIP_READ, 0);
    method(NV097_SET_FLIP_WRITE, 0);
    method(NV097_SET_FLIP_MODULO, 2);
    /* every channel written, as the kernel sets it up */
    method(NV097_SET_COLOR_MASK, 0x01010101u);
    /* the vertex arrays: attribute 0 (position) 4 floats, the rest off */
    for (u32 i = 0; i < 16; i++) {
        method(NV097_SET_VERTEX_DATA_ARRAY_FORMAT + i * 4,
               i == 0 ? (2u | (4u << 4) | (16u << 8)) : 2u);
    }
    kick();
    ram32(GPU_STATUS, 5);

    /* frame 0: A red, shown */
    wait_vblank();
    clear(SURF_A, 0xFFFF0000u);
    kick();
    wr(PCRTC_START, SURF_A);
    ram32(GPU_STATUS, 0);

    for (u32 frame = 1;; frame++) {
        wait_vblank();
        if (frame == 20) {
            /* the CPU reads the front buffer the GPU drew */
            ram32(GPU_READ0, rd32(SURF_A));
            ram32(GPU_READ1, rd32(SURF_A + PITCH * (HEIGHT - 1) + (WIDTH - 1) * 4));
        }
        if (frame == 30) {
            /* the CPU writes the front buffer green */
            for (u32 i = 0; i < PITCH * HEIGHT; i += 4) {
                ram32(SURF_A + i, 0xFF00FF00u);
            }
        }
        if (frame == 61) {
            check_swizzled();
        }
        if (frame == 63) {
            ram32(GPU_ZETA0, rd32(ZETA_B));
            ram32(GPU_ZETA1, rd32(ZETA_B + PITCH * (HEIGHT - 1) + (WIDTH - 1) * 4));
        }
        if (frame == 60) {
            draw_swizzled();
        }
        if (frame == 62) {
            method(NV097_SET_SURFACE_COLOR_OFFSET, SURF_B);
            method(NV097_SET_ZSTENCIL_CLEAR_VALUE, ZS_CLEAR);
            method(NV097_SET_CLEAR_RECT_H, (u32)(WIDTH - 1) << 16);
            method(NV097_SET_CLEAR_RECT_V, (u32)(HEIGHT - 1) << 16);
            /* depth written while the clear runs, as games clear it */
            method(NV097_SET_DEPTH_MASK, 1);
            method(NV097_CLEAR_SURFACE, 0x03); /* Z, stencil */
            method(NV097_SET_DEPTH_MASK, 0);
        }
        /* another surface drawn, a flip counted; A left alone */
        if (frame >= 50) {
            /* a draw, a clear, the same draw, into B */
            method(NV097_SET_SURFACE_COLOR_OFFSET, SURF_B);
            triangle();
            clear(SURF_B, 0xFF0000FFu | (frame << 8));
            triangle();
        } else {
            clear(SURF_B, 0xFF0000FFu | (frame << 8));
        }
        method(NV097_FLIP_INCREMENT_WRITE, 0);
        kick();
        if (frame >= 100) {
            /* until the vblank: work for the GPU, then a wait on it, as a
             * game waits on a fence. The wait spins on RAM (a wait the
             * core sees as one), and takes the guest only the GPU's
             * time */
            static u32 sem;
            u32 trips = 0;
            while (trips < TRIPS_MAX && !(rd(PCRTC_INTR) & 1)) {
                sem++;
                method(NV097_SET_SEMAPHORE_OFFSET, GPU_SEM);
                for (int i = 0; i < 8; i++) {
                    push(NOP_SUB | 2); /* call */
                }
                method(NV097_BACK_END_WRITE_SEMAPHORE_RELEASE, sem);
                post();
                while (rd32(GPU_SEM) != sem) {
                }
                trips++;
            }
            if (frame == 101 || trips < rd32(GPU_TRIPS)) {
                ram32(GPU_TRIPS, trips);
            }
        }
    }
}
