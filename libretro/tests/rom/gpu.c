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
 *   frame 5    a second swizzled surface E, as C but green and blue, then
 *              left alone: at 60 frames unused the renderer evicts it
 *              while dirty, reading it back without waiting (OpenGL: a PBO
 *              the binding parks with); at frame 75 the CPU reads all its
 *              texels, which takes that read-back: wrong ones at
 *              GPU_SWZ2_BAD
 *   frame 62   B's depth and stencil cleared (Z24S8); at frame 63 the CPU
 *              reads its first and last words back (GPU_ZETA0, GPU_ZETA1):
 *              both the clear value
 *
 *   frame 64   B's depth and stencil cleared again, to 0x33333344, with
 *              depth and stencil writes off: the NV2A clears regardless of
 *              the write masks (nxdk_pgraph_tests Clear on hardware); at 65
 *              the CPU reads the first word, the clear value, at GPU_ZETA3.
 *              The renderers cleared but took the surface for unchanged
 *              and gave the CPU the old 0x12345678
 *   frame 70   the CPU writes B's whole depth buffer: depth 0xAABBCC,
 *              stencil 0x11, as a game writes its depth buffer
 *   frame 71   only B's stencil cleared, to 0x55: the renderer takes the
 *              depth the CPU wrote first (on Vulkan through the unpack
 *              shader); at frame 72 the CPU reads it back (on Vulkan through
 *              the pack shader): 0xAABBCC55 at GPU_ZETA2
 *
 *   from frame 50: into B, a triangle drawn, B cleared, the same triangle
 *              drawn again: draws and clears on both renderers, the
 *              picture (A) unchanged by them
 *   from frame 100 through 124: until the vblank and at most TRIPS_MAX
 *              times, the GPU given a run of no-ops (NOP_SUB, 8 times)
 *              and waited for on a semaphore in RAM: the fewest of these
 *              round trips in a frame (from frame 101, the first whole
 *              one) at GPU_TRIPS, TRIPS_MAX when each wait took the
 *              guest no more than the GPU's time. Its time moving on to
 *              its next deadline while it waited instead made 1; Ninja
 *              Gaiden Black, waiting so on its GPU, ran in slow motion
 *
 *   frames 125..154: offset red/green GPU tiles over a blue CPU frame;
 *              the CPU changes its top border to yellow at 140.
 *   frame 155: swizzled cyan bytes overlap an older parked red view.
 *   frames 175/185: leave and return; the static picture survives to 240.
 *   frames 255/285: change to 15/16-bit scanout, red tile and blue border.
 *   frames 315/335: return to the warmed 15/16-bit modes; at 355 return
 *              to the 32-bit static picture after mode-cache eviction.
 *   frame 385: 32 offset strips; at 405 a 33rd exceeds the direct-draw cap.
 *   frame 425: CPU purple replaces them; at 445 one new red tile.
 *   frames 465/475: keyed/unkeyed white PVIDEO crosses tile and CPU border.
 *   frames 680/700/720/740: matching and incompatible texture views of a
 *              GPU-produced surface: red ARGB, black/white RGB565 words,
 *              then a GPU redraw that must invalidate the aliased texture.
 *   frames 760/780: blue's nontrivial RGB565 words, 00FF and FF00, sampled
 *              as 001CFF and FFE300 to catch channel-order mistakes.
 * The larger command ring keeps this extended scenario before wraparound.
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
#define GPU_ZETA2    0x00300034u   /* u32: B's depth word at frame 72 */
#define GPU_SWZ2_BAD 0x00300038u   /* u32: E's wrong texels, read at frame 75 */
#define GPU_ZETA3    0x0030003Cu   /* u32: B's first depth word, frame 65 */
#define GPU_SCANOUT  0x00300040u   /* u32: offset scanout test steps, 0x7f */
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
#define PRAMDAC_GENERAL   0x680600

/* RAM: the pushbuffer and the surfaces (the NV2A's memory is the RAM) */
#define PUSHBUF      0x00800000u
#define PUSHBUF_SIZE 0x00040000u
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
#define SURF_E       0x01C00000u   /* swizzled, C_W x C_H, aged out */
#define C_LOG_W      5
#define C_LOG_H      4
#define C_W          (1u << C_LOG_W)
#define C_H          (1u << C_LOG_H)
#define ZETA_B       (SURF_B + 0x200000u)
#define SURF_D       0x01A00000u   /* shown from frame 105 */
#define SURF_F       0x02000000u   /* CPU border, two offset GPU tiles */
#define SURF_H       0x02800000u   /* direct-layer limit and fallback */
#define VIDEO_DATA   0x02C00000u
#define VRAM_END     0x04000000u   /* boundary tests require xemu_memory=64 */
#define SURF_ORDER   0x03000000u   /* retained second draw for ordering checks */
#define TEX_COPIED   0x03500000u   /* RGB565 GPU copy with later sampler change */
#define TEX_RETIRED  0x03400000u   /* GPU-only source, later CPU read */
#define TEX_YUV      0x03200000u   /* odd-width packed YUV pair tests */
#define TEX_SOURCE   0x02E00000u   /* GPU-produced pixels, sampled through aliases */
#define SURF_G       0x02400000u   /* 15-bit mode change, offset tile */
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

#define NV097_SET_COMPOSITE_MATRIX 0x0680
#define NV097_SET_VIEWPORT_OFFSET 0x0a20
#define NV097_SET_CLIP_MIN 0x0394
#define NV097_SET_CLIP_MAX 0x0398
#define NV097_SET_ALPHA_TEST_ENABLE 0x0300
#define NV097_SET_CULL_FACE_ENABLE 0x0308
#define NV097_SET_DEPTH_TEST_ENABLE 0x030c
#define NV097_SET_STENCIL_TEST_ENABLE 0x032c
#define NV097_SET_WINDOW_CLIP_TYPE 0x02b4
#define NV097_SET_WINDOW_CLIP_HORIZONTAL 0x02c0
#define NV097_SET_WINDOW_CLIP_VERTICAL 0x02e0
#define NV097_SET_TEXTURE_PALETTE 0x1b20
#define NV097_SET_TEXTURE_OFFSET 0x1b00
#define NV097_SET_TEXTURE_FORMAT 0x1b04
#define NV097_SET_TEXTURE_ADDRESS 0x1b08
#define NV097_SET_TEXTURE_CONTROL0 0x1b0c
#define NV097_SET_TEXTURE_CONTROL1 0x1b10
#define NV097_SET_TEXTURE_FILTER 0x1b14
#define NV097_SET_TEXTURE_IMAGE_RECT 0x1b1c
#define NV097_SET_SHADER_STAGE_PROGRAM 0x1e70
#define NV097_SET_COMBINER_CONTROL 0x1e60
#define NV097_SET_COMBINER_SPECULAR_FOG_CW0 0x0288
#define NV097_SET_COMBINER_SPECULAR_FOG_CW1 0x028c
#define NV097_SET_VERTEX_DATA2F_M 0x1880

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
        /* Publish wrapped PUT before waiting: GET must stop at zero,
         * rather than replaying the previous lap past a transient zero. */
        push(0x20000000u | 0);
        put = 0;
        *(volatile u32 *)(USER + 0x40) = 0;
        while (*(volatile u32 *)(USER + 0x44) != 0) {
        }
    }
}

/* Ensure the first draw has consumed its texture before a CPU rewrite;
 * waiting for pushbuffer GET alone only drains the command parser. */
static void texture_cpu_barrier(void)
{
    ram32(GPU_SEM, 0);
    method(NV097_SET_SEMAPHORE_OFFSET, GPU_SEM);
    method(NV097_BACK_END_WRITE_SEMAPHORE_RELEASE, 1);
    kick();
    while (rd32(GPU_SEM) != 1) {
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

static void disable_texture(void)
{
    method(NV097_SET_TEXTURE_CONTROL0, 0);
    method(NV097_SET_SHADER_STAGE_PROGRAM, 0);
    method(NV097_SET_COMBINER_SPECULAR_FOG_CW0, 0);
    method(NV097_SET_COMBINER_SPECULAR_FOG_CW1, 0);
}

/* A 32x16 ARGB surface viewed as either matching ARGB pixels or a 64x16
 * RGB565 texture with the same byte pitch. Red's bytes are 00 00 FF FF:
 * the narrow view must alternate black and white, not stale CPU blue.
 * A one-byte Y8 view additionally checks the surface writeback boundary. */
static __attribute__((noinline)) void texture_alias_target(u32 format, u32 width,
                                                          u32 u, u32 redraw,
                                                          u32 texture_address,
                                                          u32 target)
{
    u32 retain = format >> 31; /* Fixture-only: leave sampling state bound. */
    u32 defer_draw = (format >> 30) & 1; /* Caller completes cube state first. */
    format &= 0x3fffffffu;
    if (redraw) {
        method(NV097_SET_SURFACE_CLIP_H, 32u << 16);
        method(NV097_SET_SURFACE_CLIP_V, 16u << 16);
        method(NV097_SET_SURFACE_FORMAT, 0x128u);
        method(NV097_SET_SURFACE_PITCH, 128u | (128u << 16));
        method(NV097_SET_SURFACE_COLOR_OFFSET, TEX_SOURCE);
        method(NV097_SET_COLOR_CLEAR_VALUE, redraw);
        method(NV097_SET_CLEAR_RECT_H, 31u << 16);
        method(NV097_SET_CLEAR_RECT_V, 15u << 16);
        method(NV097_CLEAR_SURFACE, 0xf0);
    }
    method(NV097_SET_SURFACE_CLIP_H, WIDTH << 16);
    method(NV097_SET_SURFACE_CLIP_V, HEIGHT << 16);
    method(NV097_SET_SURFACE_FORMAT, 0x128u);
    method(NV097_SET_SURFACE_PITCH, PITCH | (PITCH << 16));
    clear(target, 0xff0000ffu);
    /* Window-space positions through an identity composite matrix. Earlier
     * clear/readback cases do not need initialized transform constants. */
    for (u32 i = 0; i < 16; i++) method(NV097_SET_COMPOSITE_MATRIX + i * 4, i % 5 == 0 ? 0x3f800000u : 0);
    for (u32 i = 0; i < 4; i++) method(NV097_SET_VIEWPORT_OFFSET + i * 4, 0);
    method(NV097_SET_CLIP_MIN, 0);
    method(NV097_SET_CLIP_MAX, 0x3f800000u);
    method(NV097_SET_ALPHA_TEST_ENABLE, 0); /* alpha test */
    method(NV097_SET_CULL_FACE_ENABLE, 0); /* cull */
    method(NV097_SET_DEPTH_TEST_ENABLE, 0); /* depth test */
    method(NV097_SET_STENCIL_TEST_ENABLE, 0); /* stencil test */
    method(NV097_SET_WINDOW_CLIP_TYPE, 0); /* inclusive window clip */
    method(NV097_SET_WINDOW_CLIP_HORIZONTAL, (WIDTH - 1) << 16);
    method(NV097_SET_WINDOW_CLIP_VERTICAL, (HEIGHT - 1) << 16);
    /* Texture stage zero: nearest sampling, DMA A. Width 1 selects a
     * single texel; other cases use rectangle coordinates. */
    method(NV097_SET_TEXTURE_OFFSET, texture_address);
    method(NV097_SET_TEXTURE_FORMAT, 1u | (2u << 4) | (1u << 3) | (format << 8) |
                   (1u << 16) | ((width == 1 ? 0u : width == 32 ? 5u : 6u) << 20) |
                   ((width == 1 ? 0u : 4u) << 24));
    method(NV097_SET_TEXTURE_ADDRESS, 0x00030303u);
    method(NV097_SET_TEXTURE_CONTROL0, 1u << 30);
    method(NV097_SET_TEXTURE_CONTROL1, 128u << 16);
    method(NV097_SET_TEXTURE_FILTER, 0x01010000u);
    method(NV097_SET_TEXTURE_IMAGE_RECT, (width << 16) | (width == 1 ? 1u : 16u));
    method(NV097_SET_SHADER_STAGE_PROGRAM, 1);           /* PROJECT2D on stage zero */
    method(NV097_SET_COMBINER_CONTROL, 0);           /* final combiner only */
    method(NV097_SET_COMBINER_SPECULAR_FOG_CW0, 8);           /* final RGB = T0 */
    method(NV097_SET_COMBINER_SPECULAR_FOG_CW1, 0x00001800u); /* final alpha = T0.a */
    method(NV097_SET_VERTEX_DATA2F_M + 9 * 8, u);   /* constant texture coordinates */
    method(NV097_SET_VERTEX_DATA2F_M + 9 * 8 + 4, 0x3f000000u);
    if (!defer_draw) triangle();
    if (!retain) disable_texture();
    kick();
    cr(0x28, 3);
    cr(0x13, (PITCH / 8) & 0xff);
    cr(0x19, (((PITCH / 8) >> 8) & 7) << 5);
    wr(PCRTC_START, target);
}

static __attribute__((noinline)) void texture_alias_at(u32 format, u32 width,
                                                      u32 u, u32 redraw,
                                                      u32 texture_address)
{
    texture_alias_target(format, width, u, redraw, texture_address, SURF_D);
}

static __attribute__((noinline)) void texture_alias(u32 format, u32 width,
                                                   u32 u, u32 redraw)
{
    texture_alias_at(format, width, u, redraw, TEX_SOURCE);
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

/* A swizzled surface at addr, its pitch half a row; left on its left
 * half, right on its right. Then B's shape back. */
static __attribute__((noinline)) void draw_swizzled(u32 addr, u32 left,
                                                    u32 right)
{
    method(NV097_SET_SURFACE_CLIP_H, C_W << 16);
    method(NV097_SET_SURFACE_CLIP_V, C_H << 16);
    method(NV097_SET_SURFACE_FORMAT, 0x08u | (2u << 4) | (2u << 8) |
                                         ((u32)C_LOG_W << 16) |
                                         ((u32)C_LOG_H << 24));
    method(NV097_SET_SURFACE_PITCH, (C_W * 2) | ((C_W * 2) << 16));
    method(NV097_SET_SURFACE_COLOR_OFFSET, addr);
    method(NV097_SET_COLOR_CLEAR_VALUE, left);
    method(NV097_SET_CLEAR_RECT_H, (C_W / 2 - 1) << 16);
    method(NV097_SET_CLEAR_RECT_V, (C_H - 1) << 16);
    method(NV097_CLEAR_SURFACE, 0xF0);
    method(NV097_SET_COLOR_CLEAR_VALUE, right);
    method(NV097_SET_CLEAR_RECT_H, ((C_W - 1) << 16) | (C_W / 2));
    method(NV097_CLEAR_SURFACE, 0xF0);
    method(NV097_SET_SURFACE_CLIP_H, (u32)WIDTH << 16);
    method(NV097_SET_SURFACE_CLIP_V, (u32)HEIGHT << 16);
    method(NV097_SET_SURFACE_FORMAT, 0x08u | (2u << 4) | (1u << 8));
    method(NV097_SET_SURFACE_PITCH, PITCH | ((u32)PITCH << 16));
}

/* B's depth buffer written by the CPU */
static __attribute__((noinline)) void write_zeta(void)
{
    for (u32 i = 0; i < PITCH * HEIGHT; i += 4) {
        ram32(ZETA_B + i, 0xAABBCC11u);
    }
}

static __attribute__((noinline)) void clear_stencil(void)
{
    method(NV097_SET_SURFACE_COLOR_OFFSET, SURF_B);
    method(NV097_SET_ZSTENCIL_CLEAR_VALUE, 0x00000055u);
    method(NV097_SET_CLEAR_RECT_H, (u32)(WIDTH - 1) << 16);
    method(NV097_SET_CLEAR_RECT_V, (u32)(HEIGHT - 1) << 16);
    method(NV097_SET_DEPTH_MASK, 1);
    method(NV097_CLEAR_SURFACE, 0x02); /* stencil */
    method(NV097_SET_DEPTH_MASK, 0);
}

/* The texels of the surface at addr where the NV2A lays them out: the
 * count of wrong ones, the first wrong one */
static __attribute__((noinline)) u32 check_swizzled(u32 addr, u32 left,
                                                    u32 right, u32 *word)
{
    u32 bad = 0;
    *word = 0;
    for (u32 y = 0; y < C_H; y++) {
        for (u32 x = 0; x < C_W; x++) {
            u32 v = rd32(addr + swizzled(x, y) * 4);
            u32 want = x < C_W / 2 ? left : right;
            if (v != want && !bad++) {
                *word = v;
            }
        }
    }
    return bad;
}

/* The register is read into a volatile local, so every turn stores it to
 * the stack, as game code built without optimisation (or spilling) does:
 * the core must still see the loop as a wait (accel/tcg/cputlb.c
 * lr_poll_read). Taken for activity, it ran this ROM at 0.08x. */
static void wait_vblank(void)
{
    volatile u32 intr;

    do {
        intr = rd(PCRTC_INTR);
    } while (!(intr & 1));
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
    ram32(GPU_ZETA2, 0);
    ram32(GPU_SWZ2_BAD, 0xFFFFFFFFu);
    ram32(GPU_ZETA3, 0);
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
    cr(0x39, 0xFF); /* progressive scanout */
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
    ram32(GPU_SCANOUT, 0);

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
            u32 word;
            ram32(GPU_SWZ_BAD, check_swizzled(SURF_C, 0xFFFF0000u,
                                              0xFF0000FFu, &word));
            ram32(GPU_SWZ_WORD, word);
        }
        if (frame == 75) {
            u32 word;
            ram32(GPU_SWZ2_BAD, check_swizzled(SURF_E, 0xFF00FF00u,
                                               0xFF0000FFu, &word));
        }
        if (frame == 5) {
            draw_swizzled(SURF_E, 0xFF00FF00u, 0xFF0000FFu);
        }
        if (frame == 63) {
            ram32(GPU_ZETA0, rd32(ZETA_B));
            ram32(GPU_ZETA1, rd32(ZETA_B + PITCH * (HEIGHT - 1) + (WIDTH - 1) * 4));
        }
        if (frame == 60) {
            draw_swizzled(SURF_C, 0xFFFF0000u, 0xFF0000FFu);
        }
        if (frame == 64) {
            /* depth and stencil writes off (as frame 62 left them) */
            method(NV097_SET_SURFACE_COLOR_OFFSET, SURF_B);
            method(NV097_SET_ZSTENCIL_CLEAR_VALUE, 0x33333344u);
            method(NV097_SET_CLEAR_RECT_H, (u32)(WIDTH - 1) << 16);
            method(NV097_SET_CLEAR_RECT_V, (u32)(HEIGHT - 1) << 16);
            method(NV097_CLEAR_SURFACE, 0x03); /* Z, stencil */
        }
        if (frame == 65) {
            ram32(GPU_ZETA3, rd32(ZETA_B));
        }
        if (frame == 70) {
            write_zeta();
        }
        if (frame == 71) {
            clear_stencil();
        }
        if (frame == 72) {
            ram32(GPU_ZETA2, rd32(ZETA_B + PITCH * 100 + 200 * 4));
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
        if (frame >= 105 && frame < 125) {
            /* into D, shown: a draw, a clear of a corner's colour channels
             * but alpha (the surface left as it is), the same draw. Such a
             * clear is drawn with the clear's own pipeline; the draw after
             * it changes no other state and must still get a draw's, not
             * the clear's (which paints the clear colour where the triangle
             * is; Jet Set Radio Future crashed on it) */
            clear(SURF_D, 0xFF000000u);
            triangle();
            method(NV097_SET_COLOR_CLEAR_VALUE, 0xFF00FF00u);
            method(NV097_SET_CLEAR_RECT_H, 63u << 16);
            method(NV097_SET_CLEAR_RECT_V, 63u << 16);
            method(NV097_CLEAR_SURFACE, 0x70); /* R, G, B: drawn */
            triangle();
            if (frame == 105) {
                wr(PCRTC_START, SURF_D);
            }
        }
        if (frame == 125) {
            ram32(GPU_SCANOUT, rd32(GPU_SCANOUT) | 1);
            for (u32 i = 0; i < PITCH * HEIGHT; i += 4) {
                ram32(SURF_F + i, 0xFF0000FFu);
            }
            wr(PCRTC_START, SURF_F);
        }
        if (frame == 140) {
            ram32(GPU_SCANOUT, rd32(GPU_SCANOUT) | 2);
            for (u32 i = 0; i < PITCH * 64; i += 4) {
                ram32(SURF_F + i, 0xFFFFFF00u);
            }
        }
        if (frame == 146) {
            ram32(SURF_F + 20 * PITCH + 4 * 4, 0xFFFFFFFFu);
        }
        if (frame == 148) {
            ram32(SURF_F + 479 * PITCH + 639 * 4, 0xFF00FFFFu);
        }
        if (frame >= 125 && frame < 155) {
            method(NV097_SET_SURFACE_CLIP_H, 608u << 16);
            method(NV097_SET_SURFACE_CLIP_V, 176u << 16);
            clear(SURF_F + 64 * PITCH + 16 * 4, 0xFFFF0000u);
            clear(SURF_F + 240 * PITCH + 16 * 4, 0xFF00FF00u);
            method(NV097_SET_SURFACE_CLIP_H, (u32)WIDTH << 16);
            method(NV097_SET_SURFACE_CLIP_V, (u32)HEIGHT << 16);
        }
        if (frame == 155) {
            ram32(GPU_SCANOUT, rd32(GPU_SCANOUT) | 4);
            /* Raw-byte fallback: 32x16 Morton pixels occupy 512 contiguous
             * words in scanout row 120, not a 32x16 display rectangle. */
            method(NV097_SET_SURFACE_CLIP_H, 32u << 16);
            method(NV097_SET_SURFACE_CLIP_V, 16u << 16);
            method(NV097_SET_SURFACE_FORMAT, 0x08u | (2u << 4) | (2u << 8) |
                                                 (5u << 16) | (4u << 24));
            method(NV097_SET_SURFACE_COLOR_OFFSET, SURF_F + 120 * PITCH + 16 * 4);
            method(NV097_SET_COLOR_CLEAR_VALUE, 0xFF00FFFFu);
            method(NV097_SET_CLEAR_RECT_H, 31u << 16);
            method(NV097_SET_CLEAR_RECT_V, 15u << 16);
            method(NV097_CLEAR_SURFACE, 0xF0);
            method(NV097_SET_SURFACE_FORMAT, 0x08u | (2u << 4) | (1u << 8));
            method(NV097_SET_SURFACE_CLIP_H, (u32)WIDTH << 16);
            method(NV097_SET_SURFACE_CLIP_V, (u32)HEIGHT << 16);
            method(NV097_SET_SURFACE_COLOR_OFFSET, SURF_B);
        }
        if (frame == 175) {
            wr(PCRTC_START, SURF_A);
            ram32(GPU_SCANOUT, rd32(GPU_SCANOUT) | 8);
        }
        if (frame == 185) {
            wr(PCRTC_START, SURF_F);
            ram32(GPU_SCANOUT, rd32(GPU_SCANOUT) | 16);
        }
        if (frame == 255) {
            for (u32 i = 0; i < WIDTH * HEIGHT * 2; i += 4) {
                ram32(SURF_G + i, 0x001F001Fu);
            }
            cr(0x13, ((WIDTH * 2) / 8) & 0xFF);
            cr(0x19, 0);
            cr(0x28, 2); /* 15 bits: PRAMDAC ALT_MODE_SEL is zero */
            wr(PCRTC_START, SURF_G);
            ram32(GPU_SCANOUT, rd32(GPU_SCANOUT) | 32);
        }
        if (frame == 285) {
            wr(PRAMDAC_GENERAL, rd(PRAMDAC_GENERAL) | (1u << 12));
            ram32(GPU_SCANOUT, rd32(GPU_SCANOUT) | 64);
        }
        if (frame == 315 || frame == 335) {
            u32 general = rd(PRAMDAC_GENERAL);
            wr(PRAMDAC_GENERAL, frame == 315 ? general & ~(1u << 12) :
                                              general | (1u << 12));
        }
        if (frame == 355) {
            cr(0x13, (PITCH / 8) & 0xFF);
            cr(0x19, (((PITCH / 8) >> 8) & 7) << 5);
            cr(0x28, 3);
            wr(PCRTC_START, SURF_F);
            method(NV097_SET_SURFACE_FORMAT, 0x08u | (2u << 4) | (1u << 8));
            method(NV097_SET_SURFACE_PITCH, PITCH | ((u32)PITCH << 16));
        }
        if (frame >= 255 && frame < 355) {
            int mode15 = frame < 285 || (frame >= 315 && frame < 335);
            method(NV097_SET_SURFACE_FORMAT, (mode15 ? 0x01u : 0x03u) |
                                              (2u << 4) | (1u << 8));
            method(NV097_SET_SURFACE_PITCH, WIDTH * 2 | ((u32)WIDTH * 2 << 16));
            method(NV097_SET_SURFACE_CLIP_H, 608u << 16);
            method(NV097_SET_SURFACE_CLIP_V, 352u << 16);
            clear(SURF_G + 64 * WIDTH * 2 + 16 * 2,
                  mode15 ? 0x7C007C00u : 0xF800F800u);
            method(NV097_SET_SURFACE_CLIP_H, (u32)WIDTH << 16);
            method(NV097_SET_SURFACE_CLIP_V, (u32)HEIGHT << 16);
        }
        if (frame == 385) {
            for (u32 i = 0; i < PITCH * HEIGHT; i += 4) {
                ram32(SURF_H + i, 0xFF0000FFu);
            }
            wr(PCRTC_START, SURF_H);
            method(NV097_SET_SURFACE_CLIP_H, 608u << 16);
            method(NV097_SET_SURFACE_CLIP_V, 8u << 16);
            for (u32 i = 0; i < 32; i++) {
                clear(SURF_H + (64 + i * 8) * PITCH + 16 * 4,
                      i & 1 ? 0xFF00FF00u : 0xFFFF0000u);
            }
            method(NV097_SET_SURFACE_CLIP_H, (u32)WIDTH << 16);
            method(NV097_SET_SURFACE_CLIP_V, (u32)HEIGHT << 16);
        }
        if (frame == 405) {
            method(NV097_SET_SURFACE_CLIP_H, 608u << 16);
            method(NV097_SET_SURFACE_CLIP_V, 8u << 16);
            clear(SURF_H + 320 * PITCH + 16 * 4, 0xFFFF0000u);
            method(NV097_SET_SURFACE_CLIP_H, (u32)WIDTH << 16);
            method(NV097_SET_SURFACE_CLIP_V, (u32)HEIGHT << 16);
        }
        if (frame == 425) {
            for (u32 i = 0; i < PITCH * HEIGHT; i += 4) {
                ram32(SURF_H + i, 0xFF800080u);
            }
        }
        if (frame == 445) {
            method(NV097_SET_SURFACE_CLIP_H, 608u << 16);
            method(NV097_SET_SURFACE_CLIP_V, 64u << 16);
            clear(SURF_H + 100 * PITCH + 16 * 4, 0xFFFF0000u);
            method(NV097_SET_SURFACE_CLIP_H, (u32)WIDTH << 16);
            method(NV097_SET_SURFACE_CLIP_V, (u32)HEIGHT << 16);
        }
        if (frame == 465) {
            for (u32 i = 0; i < 64 * 64 * 2; i += 4) {
                ram32(VIDEO_DATA + i, 0x80FF80FFu);
            }
            wr(0x8900, VIDEO_DATA); /* PVIDEO BASE */
            wr(0x8908, 64 * 64 * 2); /* LIMIT */
            wr(0x8920, 0); /* OFFSET */
            wr(0x8928, 64u | (64u << 16)); /* SIZE_IN */
            wr(0x8930, 0); /* POINT_IN */
            wr(0x8938, 1u << 20); /* DS_DX */
            wr(0x8940, 1u << 20); /* DT_DY */
            wr(0x8948, 8u | (320u << 16)); /* POINT_OUT */
            wr(0x8950, 64u | (64u << 16)); /* SIZE_OUT */
            wr(0x8958, 128u | (1u << 16) | (1u << 20)); /* keyed YUY2 */
            wr(0x8B00, 0x00FF0000u); /* red color key */
            wr(0x8700, 1); /* BUFFER */
        }
        if (frame == 475) {
            wr(0x8958, 128u | (1u << 16)); /* unkeyed overlay */
        }
        if (frame == 482) {
            wr(0x8928, 32u | (64u << 16)); /* resize overlay input */
            wr(0x8958, 64u | (1u << 16));
        }
        /* Byte reinterpretation tests: nonuniform Morton colors, packed
         * 16-bit pairs shown as 32-bit pixels, 32-bit halves in 16/15-bit
         * scanout, and narrow R8/RG8 color surfaces. No CPU reads these. */
        if (frame == 500) {
            wr(0x8700, 0); /* disable PVIDEO */
            draw_swizzled(SURF_H + 120 * PITCH + 16 * 4,
                          0xFF39A5C7u, 0xFFDA630Bu);
        }
        if (frame == 520 || frame == 540 || frame == 580 || frame == 600 || frame == 620 || frame == 640) {
            u32 fmt = frame == 520 || frame == 640 ? 0x03u : frame == 540 ? 0x08u :
                      frame == 580 ? 0x01u : frame == 600 ? 0x0Au : 0x09u;
            u32 pitch = frame == 640 ? 2048 : frame == 540 || frame == 580 ? WIDTH * 2 : PITCH;
            u32 w = frame == 540 ? 304 : 608;
            if (frame == 540 || frame == 600) {
                cr(0x13, (pitch / 8) & 0xFF);
                cr(0x19, (((pitch / 8) >> 8) & 7) << 5);
                cr(0x28, frame == 540 ? 2 : 3);
                wr(PRAMDAC_GENERAL, rd(PRAMDAC_GENERAL) | (1u << 12));
            }
            if (frame == 580) wr(PRAMDAC_GENERAL, rd(PRAMDAC_GENERAL) | (1u << 12));
            method(NV097_SET_SURFACE_FORMAT, fmt | (2u << 4) | (1u << 8));
            method(NV097_SET_SURFACE_PITCH, pitch | (pitch << 16));
            method(NV097_SET_SURFACE_CLIP_H, w << 16);
            method(NV097_SET_SURFACE_CLIP_V, 32u << 16);
            u32 addr = frame == 640 ? SURF_H + 150 * PITCH + 128 * 4 :
                SURF_H + 200 * pitch + 16 * (frame == 540 || frame == 580 ? 2 : 4);
            if (frame < 600 || frame == 640) {
                clear(addr, frame == 520 || frame == 640 ? 0x39A539A5u : frame == 540 ? 0x7C0039A5u : 0x45674567u);
            } else {
                /* These narrow formats have no CLEAR_SURFACE command. A
                 * clipped draw keeps their uploaded byte pattern unchanged. */
                for (u32 i = 0; i < pitch * 32; i += 4)
                    ram32(addr + i, frame == 600 ? 0xC7A5C7A5u : 0x5A5A5A5Au);
                method(NV097_SET_SURFACE_COLOR_OFFSET, addr);
                triangle();
            }
            method(NV097_SET_SURFACE_CLIP_H, (u32)WIDTH << 16);
            method(NV097_SET_SURFACE_CLIP_V, (u32)HEIGHT << 16);
            method(NV097_SET_SURFACE_FORMAT, 0x08u | (2u << 4) | (1u << 8));
            method(NV097_SET_SURFACE_PITCH, PITCH | ((u32)PITCH << 16));
        }
        if (frame == 560) wr(PRAMDAC_GENERAL, rd(PRAMDAC_GENERAL) & ~(1u << 12));
        if (frame == 680) {
            for (u32 i = 0; i < 32u * 16u; i++) ram32(TEX_SOURCE + i * 4, 0xff0000ffu);
            texture_alias(0x12, 32, 0x3f000000u, 0xffff0000u);
        }
        if (frame == 700) {
            /* Keep the source live even on a renderer with short eviction
             * age, and make stale RAM observably wrong for the alias. */
            for (u32 i = 0; i < 32u * 16u; i++) ram32(TEX_SOURCE + i * 4, 0xff0000ffu);
            texture_alias(0x11, 64, 0x3f000000u, 0xffff0000u);
        }
        if (frame == 720) texture_alias(0x11, 64, 0x3fc00000u, 0);
        if (frame == 740) texture_alias(0x11, 64, 0x3f000000u, 0xffffffffu);
        if (frame == 760) texture_alias(0x11, 64, 0x3f000000u, 0xff0000ffu);
        if (frame == 780) texture_alias(0x11, 64, 0x3fc00000u, 0);
        if (frame == 800) {
            /* One-byte Y8 aliases the first blue byte of a GPU-cleared
             * ARGB surface. RAM stays zero until the surface is written
             * back, so an empty/one-byte overlap error displays black. */
            for (u32 i = 0; i < 32u * 16u; i++) ram32(TEX_SOURCE + i * 4, 0);
            texture_alias(0x00, 1, 0x3f000000u, 0xff0000ffu);
        }
        if (frame == 820) {
            /* Linear Y8: pitch 128, one row, exclusive end exactly VRAM_END. */
            ram32(VRAM_END - 128, 0x000000ffu);
            texture_alias_at(0x13, 1, 0x3f000000u, 0, VRAM_END - 128);
        }
        if (frame == 840) {
            /* Clear this page's dirty flag through another cached shape. */
            ram32(VRAM_END - 128, 0);
            texture_alias_at(0x00, 1, 0x3f000000u, 0, VRAM_END - 128);
        }
        if (frame == 860) {
            /* No CPU write: the previous query must have invalidated this view. */
            texture_alias_at(0x13, 1, 0x3f000000u, 0, VRAM_END - 128);
        }
        if (frame == 880) {
            /* 256 ARGB entries occupy the last 1024 bytes of VRAM. */
            ram32(TEX_SOURCE, 0);
            ram32(VRAM_END - 1024, 0xffffffffu);
            method(NV097_SET_TEXTURE_PALETTE, VRAM_END - 1024);
            texture_alias(0x0b, 1, 0x3f000000u, 0);
        }
        if (frame == 900) {
            ram32(VRAM_END - 1024, 0xff000000u);
            texture_alias(0x0b, 1, 0x3f000000u, 0);
        }
        if (frame == 920 || frame == 940) {
            /* GPU-rendered palette: RAM still contains the earlier black
             * entry. All 256 entries become red, then blue while cached. */
            method(NV097_SET_SURFACE_CLIP_H, 32u << 16);
            method(NV097_SET_SURFACE_CLIP_V, 8u << 16);
            method(NV097_SET_SURFACE_FORMAT, 0x128u);
            method(NV097_SET_SURFACE_PITCH, 128u | (128u << 16));
            method(NV097_SET_SURFACE_COLOR_OFFSET, VRAM_END - 1024);
            method(NV097_SET_COLOR_CLEAR_VALUE,
                   frame == 920 ? 0xffff0000u : 0xff0000ffu);
            method(NV097_SET_CLEAR_RECT_H, 31u << 16);
            method(NV097_SET_CLEAR_RECT_V, 7u << 16);
            method(NV097_CLEAR_SURFACE, 0xf0);
            texture_alias(0x0b, 1, 0x3f000000u, 0);
        }
        if (frame >= 960 && frame < 1000) {
            /* Rebind and draw each frame without changing the palette.
             * Dirty-only synchronization must keep this view blue. */
            texture_alias(0x0b, 1, 0x3f000000u, 0);
        }
        if (frame == 1020) {
            /* Two GPU palette versions consumed before the next flip. */
            for (u32 update = 0; update < 2; update++) {
                method(NV097_SET_SURFACE_CLIP_H, 32u << 16);
                method(NV097_SET_SURFACE_CLIP_V, 8u << 16);
                method(NV097_SET_SURFACE_FORMAT, 0x128u);
                method(NV097_SET_SURFACE_PITCH, 128u | (128u << 16));
                method(NV097_SET_SURFACE_COLOR_OFFSET, VRAM_END - 1024);
                method(NV097_SET_COLOR_CLEAR_VALUE,
                       update ? 0xff00ff00u : 0xffff0000u);
                method(NV097_SET_CLEAR_RECT_H, 31u << 16);
                method(NV097_SET_CLEAR_RECT_V, 7u << 16);
                method(NV097_CLEAR_SURFACE, 0xf0);
                texture_alias(0x0b, 1, 0x3f000000u, 0);
            }
        }
        if (frame == 1040) {
            /* The first blue byte of GPU ARGB pixels is also a one-byte
             * Y8 texture. Red -> blue must sample black -> white without
             * waiting for a flip between the two updates. */
            texture_alias(0x00, 1, 0x3f000000u, 0xffff0000u);
            texture_alias(0x00, 1, 0x3f000000u, 0xff0000ffu);
        }
        if (frame == 1060) {
            /* GPU writes index zero, then index 255. Palette zero remains
             * green from frame 1020; palette 255 is CPU-written white. */
            ram32(VRAM_END - 4, 0xffffffffu);
            texture_alias(0x0b, 1, 0x3f000000u, 0xffff0000u);
            texture_alias(0x0b, 1, 0x3f000000u, 0xff0000ffu);
        }
        if (frame == 1080) {
            /* Keep both draws in separate targets: updating the texture
             * for the second must not change pixels from the first. */
            texture_alias_target(0x00, 1, 0x3f000000u, 0xffff0000u, TEX_SOURCE, SURF_D);
            texture_alias_target(0x00, 1, 0x3f000000u, 0xff0000ffu, TEX_SOURCE, SURF_ORDER);
            wr(PCRTC_START, SURF_D);
        }
        if (frame == 1100) wr(PCRTC_START, SURF_ORDER);
        if (frame == 1120) {
            ram32(TEX_SOURCE, 0);
            for (u32 update = 0; update < 2; update++) {
                method(NV097_SET_SURFACE_CLIP_H, 32u << 16);
                method(NV097_SET_SURFACE_CLIP_V, 8u << 16);
                method(NV097_SET_SURFACE_FORMAT, 0x128u);
                method(NV097_SET_SURFACE_PITCH, 128u | (128u << 16));
                method(NV097_SET_SURFACE_COLOR_OFFSET, VRAM_END - 1024);
                method(NV097_SET_COLOR_CLEAR_VALUE,
                       update ? 0xff00ff00u : 0xffff0000u);
                method(NV097_SET_CLEAR_RECT_H, 31u << 16);
                method(NV097_SET_CLEAR_RECT_V, 7u << 16);
                method(NV097_CLEAR_SURFACE, 0xf0);
                texture_alias_target(0x0b, 1, 0x3f000000u, 0, TEX_SOURCE,
                                     update ? SURF_ORDER : SURF_D);
            }
            wr(PCRTC_START, SURF_D);
        }
        if (frame == 1140) wr(PCRTC_START, SURF_ORDER);
        if (frame == 1160) {
            ram32(VRAM_END - 128, 0x000000ffu);
            texture_alias_at(0x13, 1, 0x3f000000u, 0, VRAM_END - 128);
        }
        if (frame == 1180) {
            /* Only padding after the last row changes; the texel stays white. */
            ram32(VRAM_END - 4, 0x12345678u);
            texture_alias_at(0x13, 1, 0x3f000000u, 0, VRAM_END - 128);
        }
        if (frame == 1200 || frame == 1240) {
            ram32(TEX_YUV, 0x80808080u);
            texture_alias_at(frame == 1200 ? 0x24 : 0x25, 1,
                             0x3f000000u, 0, TEX_YUV);
        }
        if (frame == 1220) {
            /* YUY2 width one still consumes Cr at byte three. */
            ram32(TEX_YUV, 0xff808080u);
            texture_alias_at(0x24, 1, 0x3f000000u, 0, TEX_YUV);
        }
        if (frame == 1260) {
            /* UYVY width one still consumes Cr at byte two. */
            ram32(TEX_YUV, 0x80ff8080u);
            texture_alias_at(0x25, 1, 0x3f000000u, 0, TEX_YUV);
        }
        if (frame == 1280) {
            /* No texture state is resent before the second draw. */
            ram32(TEX_YUV, 0);
            texture_alias_at(0x80000013u, 1, 0x3f000000u, 0, TEX_YUV);
            texture_cpu_barrier();
            ram32(TEX_YUV, 0x000000ffu);
            triangle();
            disable_texture();
            kick();
        }
        if (frame == 1300) {
            ram32(TEX_YUV, 0);
            ram32(VRAM_END - 1024, 0xffff0000u);
            texture_alias_at(0x8000000bu, 1, 0x3f000000u, 0, TEX_YUV);
            texture_cpu_barrier();
            ram32(VRAM_END - 1024, 0xff00ff00u);
            triangle();
            disable_texture();
            kick();
        }
        if (frame == 1320) {
            texture_alias_at(0x80000000u, 1, 0x3f000000u, 0xffff0000u, TEX_SOURCE);
            texture_cpu_barrier();
            /* Rewrite the GPU producer, then draw with the same bound
             * texture and sampler; RAM still contains the old red pixels. */
            method(NV097_SET_SURFACE_CLIP_H, 32u << 16);
            method(NV097_SET_SURFACE_CLIP_V, 16u << 16);
            method(NV097_SET_SURFACE_FORMAT, 0x128u);
            method(NV097_SET_SURFACE_PITCH, 128u | (128u << 16));
            method(NV097_SET_SURFACE_COLOR_OFFSET, TEX_SOURCE);
            method(NV097_SET_COLOR_CLEAR_VALUE, 0xff0000ffu);
            method(NV097_SET_CLEAR_RECT_H, 31u << 16);
            method(NV097_SET_CLEAR_RECT_V, 15u << 16);
            method(NV097_CLEAR_SURFACE, 0xf0);
            method(NV097_SET_SURFACE_CLIP_H, WIDTH << 16);
            method(NV097_SET_SURFACE_CLIP_V, HEIGHT << 16);
            method(NV097_SET_SURFACE_PITCH, PITCH | (PITCH << 16));
            method(NV097_SET_SURFACE_COLOR_OFFSET, SURF_D);
            triangle();
            disable_texture();
            kick();
        }
        if (frame == 1340) {
            ram32(TEX_YUV, 0);
            for (u32 update = 0; update < 2; update++) {
                method(NV097_SET_SURFACE_CLIP_H, 32u << 16);
                method(NV097_SET_SURFACE_CLIP_V, 8u << 16);
                method(NV097_SET_SURFACE_FORMAT, 0x128u);
                method(NV097_SET_SURFACE_PITCH, 128u | (128u << 16));
                method(NV097_SET_SURFACE_COLOR_OFFSET, VRAM_END - 1024);
                method(NV097_SET_COLOR_CLEAR_VALUE,
                       update ? 0xff0000ffu : 0xffff0000u);
                method(NV097_SET_CLEAR_RECT_H, 31u << 16);
                method(NV097_SET_CLEAR_RECT_V, 7u << 16);
                method(NV097_CLEAR_SURFACE, 0xf0);
                if (!update) {
                    texture_alias_at(0x8000000bu, 1, 0x3f000000u, 0, TEX_YUV);
                    texture_cpu_barrier();
                } else {
                    /* Keep index data and sampling state unchanged. */
                    method(NV097_SET_SURFACE_CLIP_H, WIDTH << 16);
                    method(NV097_SET_SURFACE_CLIP_V, HEIGHT << 16);
                    method(NV097_SET_SURFACE_PITCH, PITCH | (PITCH << 16));
                    method(NV097_SET_SURFACE_COLOR_OFFSET, SURF_D);
                    triangle();
                    disable_texture();
                    kick();
                }
            }
        }
        if (frame == 1360 || frame == 1660) {
            u32 source = frame == 1360 ? TEX_RETIRED : TEX_COPIED;
            for (u32 update = 0; update < 2; update++) {
                method(NV097_SET_SURFACE_CLIP_H, 32u << 16);
                method(NV097_SET_SURFACE_CLIP_V, 16u << 16);
                method(NV097_SET_SURFACE_FORMAT, 0x128u);
                method(NV097_SET_SURFACE_PITCH, 128u | (128u << 16));
                method(NV097_SET_SURFACE_COLOR_OFFSET, source);
                method(NV097_SET_COLOR_CLEAR_VALUE,
                       update ? 0xff0000ffu : 0xffff0000u);
                method(NV097_SET_CLEAR_RECT_H, 31u << 16);
                method(NV097_SET_CLEAR_RECT_V, 15u << 16);
                method(NV097_CLEAR_SURFACE, 0xf0);
                if (!update) {
                    texture_alias_at(frame == 1360 ? 0x80000012u : 0x80000011u,
                                     frame == 1360 ? 32 : 64,
                                     0x3f000000u, 0, source);
                    texture_cpu_barrier();
                } else {
                    method(NV097_SET_SURFACE_CLIP_H, WIDTH << 16);
                    method(NV097_SET_SURFACE_CLIP_V, HEIGHT << 16);
                    method(NV097_SET_SURFACE_PITCH, PITCH | (PITCH << 16));
                    method(NV097_SET_SURFACE_COLOR_OFFSET, SURF_D);
                    triangle();
                    kick(); /* Continue sampling this GPU source next frame. */
                }
            }
        }
        if (frame >= 1361 && frame <= 1440) {
            method(NV097_SET_SURFACE_COLOR_OFFSET, SURF_D);
            triangle();
            if (frame == 1440) disable_texture();
            kick();
        }
        if (frame == 1460) {
            /* A later CPU reader must still get the retired GPU content. */
            ram32(GPU_READ0, rd32(TEX_RETIRED));
        }
        /* A sampler-only change must retain the GPU-produced RGB565 copy. */
        if (frame == 1700) method(NV097_SET_TEXTURE_FILTER, 0x02020000u);
        if (frame >= 1661 && frame <= 1740) {
            method(NV097_SET_SURFACE_COLOR_OFFSET, SURF_D);
            triangle();
            if (frame == 1740) disable_texture();
            kick();
        }
        if (frame == 1760) ram32(0x00300044u, rd32(TEX_COPIED));
        if (frame == 1800) {
            for (u32 face = 0; face < 6; face++) {
                method(NV097_SET_SURFACE_CLIP_H, 16u << 16);
                method(NV097_SET_SURFACE_CLIP_V, 16u << 16);
                method(NV097_SET_SURFACE_FORMAT, 0x04040228u);
                method(NV097_SET_SURFACE_PITCH, 64u | (64u << 16));
                method(NV097_SET_SURFACE_COLOR_OFFSET, 0x03600000u + face * 1024);
                method(NV097_SET_COLOR_CLEAR_VALUE, face ? 0xff0000ffu : 0xffff0000u);
                method(NV097_SET_CLEAR_RECT_H, 15u << 16);
                method(NV097_SET_CLEAR_RECT_V, 15u << 16);
                method(NV097_CLEAR_SURFACE, 0xf0);
            }
            texture_alias_at(0xc0000006u, 32, 0x3f000000u, 0, 0x03600000u);
            method(NV097_SET_TEXTURE_FORMAT, 1u | (1u << 2) | (2u << 4) |
                   (1u << 3) | (6u << 8) | (1u << 16) | (4u << 20) | (4u << 24));
            method(NV097_SET_SHADER_STAGE_PROGRAM, 3);
            method(0x1a00u + 9 * 16, 0x3f800000u);
            method(0x1a00u + 9 * 16 + 4, 0);
            method(0x1a00u + 9 * 16 + 8, 0);
            method(0x1a00u + 9 * 16 + 12, 0x3f800000u);
            triangle(); kick();
        }
        if (frame == 1840) method(NV097_SET_TEXTURE_FILTER, 0x02020000u);
        if (frame >= 1801 && frame <= 1880) {
            method(NV097_SET_SURFACE_COLOR_OFFSET, SURF_D);
            triangle(); if (frame == 1880) disable_texture(); kick();
        }
        if (frame == 1900) {
            for (u32 face = 0; face < 6; face++)
                ram32(0x00300048u + face * 4, rd32(0x03600000u + face * 1024));
        }
        if (frame == 1960 || frame == 2120) {
            u32 depth16 = frame == 1960;
            u32 source = depth16 ? 0x03700000u : 0x03900000u;
            method(NV097_SET_SURFACE_CLIP_H, 32u << 16);
            method(NV097_SET_SURFACE_CLIP_V, 16u << 16);
            method(NV097_SET_SURFACE_FORMAT, depth16 ? 0x118u : 0x128u);
            method(NV097_SET_SURFACE_PITCH, 128u | ((depth16 ? 64u : 128u) << 16));
            method(NV097_SET_SURFACE_COLOR_OFFSET, 0x03800000u);
            method(NV097_SET_SURFACE_ZETA_OFFSET, source);
            method(NV097_SET_ZSTENCIL_CLEAR_VALUE, depth16 ? 0x00008000u : 0x80000000u);
            method(NV097_SET_CLEAR_RECT_H, 31u << 16);
            method(NV097_SET_CLEAR_RECT_V, 15u << 16);
            method(NV097_SET_DEPTH_MASK, 1);
            method(NV097_CLEAR_SURFACE, 3);
            method(NV097_SET_DEPTH_MASK, 0);
            texture_alias_at(depth16 ? 0xc0000030u : 0xc000002eu,
                             32, 0x3f000000u, 0, source);
            method(0x1e6cu, 4); /* Shadow comparison: depth > zero. */
            method(NV097_SET_TEXTURE_CONTROL1, (depth16 ? 64u : 128u) << 16);
            method(NV097_SET_SURFACE_FORMAT, 0x128u);
            method(NV097_SET_SURFACE_ZETA_OFFSET, ZETA_B);
            triangle(); kick();
        }
        if (frame == 2000 || frame == 2160) method(NV097_SET_TEXTURE_FILTER, 0x02020000u);
        if ((frame >= 1961 && frame <= 2040) ||
            (frame >= 2121 && frame <= 2200)) {
            method(NV097_SET_SURFACE_COLOR_OFFSET, SURF_D);
            triangle(); if (frame == 2040 || frame == 2200) disable_texture(); kick();
        }
        if (frame == 2060) ram32(0x00300060u, rd32(0x03700000u));
        if (frame == 2220) ram32(0x00300064u, rd32(0x03900000u));
        /* Reuse a GPU depth copy as RAM data, then CPU and GPU rewrite it. */
        if (frame == 2280) {
            texture_alias_at(0xc0000030u, 32, 0x3f000000u, 0, 0x03700000u);
            method(0x1e6cu, 4);
            method(NV097_SET_TEXTURE_CONTROL1, 64u << 16);
            triangle(); kick();
        }
        if (frame == 2320) {
            texture_cpu_barrier();
            for (u32 offset = 0; offset < 32 * 16 * 2; offset += 4)
                ram32(0x03700000u + offset, 0);
        }
        if (frame == 2321) ram32(0x0030006cu, rd32(0x03700000u));
        if (frame == 2340) method(NV097_SET_TEXTURE_FILTER, 0x02020000u);
        if (frame == 2360) {
            method(NV097_SET_SURFACE_CLIP_H, 32u << 16);
            method(NV097_SET_SURFACE_CLIP_V, 16u << 16);
            method(NV097_SET_SURFACE_FORMAT, 0x118u);
            method(NV097_SET_SURFACE_PITCH, 128u | (64u << 16));
            method(NV097_SET_SURFACE_COLOR_OFFSET, 0x03800000u);
            method(NV097_SET_SURFACE_ZETA_OFFSET, 0x03700000u);
            method(NV097_SET_ZSTENCIL_CLEAR_VALUE, 0x00008000u);
            method(NV097_SET_CLEAR_RECT_H, 31u << 16);
            method(NV097_SET_CLEAR_RECT_V, 15u << 16);
            method(NV097_SET_DEPTH_MASK, 1);
            method(NV097_CLEAR_SURFACE, 3);
            method(NV097_SET_DEPTH_MASK, 0);
            method(NV097_SET_SURFACE_CLIP_H, WIDTH << 16);
            method(NV097_SET_SURFACE_CLIP_V, HEIGHT << 16);
            method(NV097_SET_SURFACE_FORMAT, 0x128u);
            method(NV097_SET_SURFACE_PITCH, PITCH | (PITCH << 16));
            method(NV097_SET_SURFACE_COLOR_OFFSET, SURF_D);
            method(NV097_SET_SURFACE_ZETA_OFFSET, ZETA_B);
        }
        if (frame >= 2281 && frame <= 2380) {
            method(NV097_SET_SURFACE_COLOR_OFFSET, SURF_D);
            triangle(); if (frame == 2380) disable_texture(); kick();
        }
        if (frame == 2400) ram32(0x00300068u, rd32(0x03700000u));
        /* Reuse the retired GPU cube as RAM, changing its faces independently. */
        if (frame == 2480) {
            texture_alias_at(0xc0000006u, 32, 0x3f000000u, 0, 0x03600000u);
            method(NV097_SET_TEXTURE_FORMAT, 1u | (1u << 2) | (2u << 4) |
                   (1u << 3) | (6u << 8) | (1u << 16) | (4u << 20) | (4u << 24));
            method(NV097_SET_SHADER_STAGE_PROGRAM, 3);
            method(0x1a00u + 9 * 16, 0x3f800000u);
            method(0x1a00u + 9 * 16 + 4, 0);
            method(0x1a00u + 9 * 16 + 8, 0);
            method(0x1a00u + 9 * 16 + 12, 0x3f800000u);
            triangle(); kick();
        }
        if (frame == 2520 || frame == 2580) {
            texture_cpu_barrier();
            u32 face = frame == 2520 ? 0 : 1;
            for (u32 offset = 0; offset < 1024; offset += 4)
                ram32(0x03600000u + face * 1024 + offset,
                      face ? 0xffffff00u : 0xff00ff00u);
        }
        if (frame == 2540) method(NV097_SET_TEXTURE_FILTER, 0x02020000u);
        if (frame == 2560) method(0x1a00u + 9 * 16, 0xbf800000u); /* -X */
        if (frame == 2600) method(0x1a00u + 9 * 16, 0x3f800000u); /* +X */
        if (frame >= 2481 && frame <= 2620) {
            method(NV097_SET_SURFACE_COLOR_OFFSET, SURF_D);
            triangle(); if (frame == 2620) disable_texture(); kick();
        }
        if (frame == 2640) {
            ram32(0x00300070u, rd32(0x03600000u));
            ram32(0x00300074u, rd32(0x03600400u));
            ram32(0x00300078u, rd32(0x03600800u));
        }
        if (frame == 2720) {
            method(NV097_SET_SURFACE_CLIP_H, 16u << 16);
            method(NV097_SET_SURFACE_CLIP_V, 16u << 16);
            method(NV097_SET_SURFACE_FORMAT, 0x04040228u);
            method(NV097_SET_SURFACE_PITCH, 64u | (64u << 16));
            method(NV097_SET_SURFACE_COLOR_OFFSET, 0x03600400u);
            method(NV097_SET_COLOR_CLEAR_VALUE, 0xffff00ffu);
            method(NV097_SET_CLEAR_RECT_H, 15u << 16);
            method(NV097_SET_CLEAR_RECT_V, 15u << 16);
            method(NV097_CLEAR_SURFACE, 0xf0);
            texture_alias_at(0xc0000006u, 32, 0x3f000000u, 0, 0x03600000u);
            method(NV097_SET_TEXTURE_FORMAT, 1u | (1u << 2) | (2u << 4) |
                   (1u << 3) | (6u << 8) | (1u << 16) | (4u << 20) | (4u << 24));
            method(NV097_SET_SHADER_STAGE_PROGRAM, 3);
            method(0x1a00u + 9 * 16, 0xbf800000u);
            method(0x1a00u + 9 * 16 + 4, 0);
            method(0x1a00u + 9 * 16 + 8, 0);
            method(0x1a00u + 9 * 16 + 12, 0x3f800000u);
            triangle(); kick();
        }
        if (frame == 2745) {
            texture_cpu_barrier();
            for (u32 offset = 0; offset < 1024; offset += 4)
                ram32(0x03600000u + offset, 0xffffff00u);
        }
        if (frame == 2760) {
            method(NV097_SET_SURFACE_CLIP_H, 16u << 16);
            method(NV097_SET_SURFACE_CLIP_V, 16u << 16);
            method(NV097_SET_SURFACE_FORMAT, 0x04040228u);
            method(NV097_SET_SURFACE_PITCH, 64u | (64u << 16));
            method(NV097_SET_SURFACE_COLOR_OFFSET, 0x03600400u);
            method(NV097_SET_COLOR_CLEAR_VALUE, 0xff00ffffu);
            method(NV097_SET_CLEAR_RECT_H, 15u << 16);
            method(NV097_SET_CLEAR_RECT_V, 15u << 16);
            method(NV097_CLEAR_SURFACE, 0xf0);
            method(NV097_SET_SURFACE_CLIP_H, WIDTH << 16);
            method(NV097_SET_SURFACE_CLIP_V, HEIGHT << 16);
            method(NV097_SET_SURFACE_FORMAT, 0x128u);
            method(NV097_SET_SURFACE_PITCH, PITCH | (PITCH << 16));
            method(0x1a00u + 9 * 16, 0xbf800000u); /* -X */
        }
        if (frame == 2780) method(0x1a00u + 9 * 16, 0x3f800000u); /* +X */
        if (frame == 2740) method(0x1a00u + 9 * 16, 0x3f800000u);
        if (frame >= 2721 && frame <= 2800) {
            method(NV097_SET_SURFACE_COLOR_OFFSET, SURF_D);
            triangle(); if (frame == 2800) disable_texture(); kick();
        }
        if (frame == 2820) {
            ram32(0x0030007cu, rd32(0x03600400u));
            ram32(0x00300080u, rd32(0x03600000u));
            ram32(0x00300084u, rd32(0x03600800u));
        }
        /* Rebind the exact swizzled view for a second full clear. */
        if (frame == 2860) {
            method(NV097_SET_SURFACE_CLIP_H, 16u << 16);
            method(NV097_SET_SURFACE_CLIP_V, 16u << 16);
            method(NV097_SET_SURFACE_FORMAT, 0x04040228u);
            method(NV097_SET_SURFACE_PITCH, 64u | (64u << 16));
            method(NV097_SET_SURFACE_COLOR_OFFSET, 0x03600400u);
            method(NV097_SET_COLOR_CLEAR_VALUE, 0xff0000ffu);
            method(NV097_SET_CLEAR_RECT_H, 15u << 16);
            method(NV097_SET_CLEAR_RECT_V, 15u << 16);
            method(NV097_CLEAR_SURFACE, 0xf0);
            method(NV097_SET_SURFACE_COLOR_OFFSET, SURF_D);
            method(NV097_SET_SURFACE_COLOR_OFFSET, 0x03600400u);
            method(NV097_SET_COLOR_CLEAR_VALUE, 0xffff00ffu);
            method(NV097_SET_CLEAR_RECT_H, 15u << 16);
            method(NV097_CLEAR_SURFACE, 0xf0);
            method(NV097_SET_SURFACE_CLIP_H, WIDTH << 16);
            method(NV097_SET_SURFACE_CLIP_V, HEIGHT << 16);
            method(NV097_SET_SURFACE_FORMAT, 0x128u);
            method(NV097_SET_SURFACE_PITCH, PITCH | (PITCH << 16));
            method(NV097_SET_SURFACE_COLOR_OFFSET, SURF_D);
            kick();
        }
        if (frame == 2880) {
            u32 bad = 0;
            for (u32 y = 0; y < 16; y++) for (u32 x = 0; x < 16; x++)
                bad += rd32(0x03600400u + swizzled(x, y) * 4) !=
                       0xffff00ffu;
            ram32(0x00300088u, bad);
            ram32(0x0030008cu, rd32(0x03600400u));
            ram32(0x00300090u, rd32(0x03600400u + swizzled(15, 15) * 4));
            ram32(0x00300094u, rd32(0x03600800u));
        }
        method(NV097_FLIP_INCREMENT_WRITE, 0);
        kick();
        if (frame >= 100 && frame < 125) {
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
