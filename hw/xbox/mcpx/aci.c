/*
 * QEMU MCPX Audio Codec Interface implementation
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2020-2021 Matt Borgerson
 *
 * The AC'97 link (codec mixer registers and bus masters) is QEMU's AC97
 * emulation (Copyright (C) 2006 InnoTek Systemberatung GmbH, from
 * VirtualBox OSE; GPL-2.0-or-later after 2012-01-13), cut down to what the
 * Xbox's MCPX has.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * The core plays nothing that goes through here: the game's sound goes
 * from the APU to the frontend (apu/monitor.c). What the guest can see of
 * the PCM out bus master is how fast it takes samples: its position, its
 * buffer-complete status and interrupt. So the samples are never read
 * from guest memory; the bus master is credited 48 kHz (or the codec's
 * rate) worth of bytes in virtual time, every 10 ms, at most 1024 frames
 * ahead, and its position moves by what it takes. PCM in, mic in and
 * S/PDIF out keep their registers and never move, as before.
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "migration/vmstate.h"
#include "qemu/timer.h"
#include "system/dma.h"

#define SR_FIFOE 16             /* rwc */
#define SR_BCIS  8              /* rwc */
#define SR_LVBCI 4              /* rwc */
#define SR_CELV  2              /* ro */
#define SR_DCH   1              /* ro */
#define SR_WCLEAR_MASK (SR_FIFOE | SR_BCIS | SR_LVBCI)
#define SR_RO_MASK (SR_DCH | SR_CELV)
#define SR_INT_MASK (SR_FIFOE | SR_BCIS | SR_LVBCI)

#define CR_IOCE  16             /* rw */
#define CR_FEIE  8              /* rw */
#define CR_LVBIE 4              /* rw */
#define CR_RR    2              /* rw */
#define CR_RPBM  1              /* rw */
#define CR_VALID_MASK ((1 << 5) - 1)
#define CR_DONT_CLEAR_MASK (CR_IOCE | CR_FEIE | CR_LVBIE)

#define GC_WR    4              /* rw */
#define GC_CR    2              /* rw */
#define GC_VALID_MASK ((1 << 6) - 1)

#define GS_MD3   (1 << 17)      /* rw */
#define GS_AD3   (1 << 16)      /* rw */
#define GS_RCS   (1 << 15)      /* rwc */
#define GS_B3S12 (1 << 14)      /* ro */
#define GS_B2S12 (1 << 13)      /* ro */
#define GS_B1S12 (1 << 12)      /* ro */
#define GS_S1R1  (1 << 11)      /* rwc */
#define GS_S0R1  (1 << 10)      /* rwc */
#define GS_S1CR  (1 << 9)       /* ro */
#define GS_S0CR  (1 << 8)       /* ro */
#define GS_MINT  (1 << 7)       /* ro */
#define GS_POINT (1 << 6)       /* ro */
#define GS_PIINT (1 << 5)       /* ro */
#define GS_SOINT (1 << 4)       /* ro */
#define GS_RSRVD (1 << 3)
#define GS_MOINT (1 << 2)       /* ro */
#define GS_MIINT (1 << 1)       /* ro */
#define GS_GSCI  1              /* rwc */
#define GS_RO_MASK (GS_B3S12 | GS_B2S12 | GS_B1S12 | GS_S1CR | GS_S0CR | \
                    GS_MINT | GS_POINT | GS_PIINT | GS_SOINT | GS_RSRVD | \
                    GS_MOINT | GS_MIINT)
#define GS_VALID_MASK ((1 << 18) - 1)
#define GS_WCLEAR_MASK (GS_RCS | GS_S1R1 | GS_S0R1 | GS_GSCI)

#define BD_IOC (1u << 31)       /* interrupt on completion */

#define REC_MASK 7

/* codec registers */
enum {
    AC97_Reset                     = 0x00,
    AC97_Master_Volume_Mute        = 0x02,
    AC97_Headphone_Volume_Mute     = 0x04,
    AC97_Master_Volume_Mono_Mute   = 0x06,
    AC97_Master_Tone_RL            = 0x08,
    AC97_PC_BEEP_Volume_Mute       = 0x0A,
    AC97_Phone_Volume_Mute         = 0x0C,
    AC97_Mic_Volume_Mute           = 0x0E,
    AC97_Line_In_Volume_Mute       = 0x10,
    AC97_CD_Volume_Mute            = 0x12,
    AC97_Video_Volume_Mute         = 0x14,
    AC97_Aux_Volume_Mute           = 0x16,
    AC97_PCM_Out_Volume_Mute       = 0x18,
    AC97_Record_Select             = 0x1A,
    AC97_Record_Gain_Mute          = 0x1C,
    AC97_Record_Gain_Mic_Mute      = 0x1E,
    AC97_General_Purpose           = 0x20,
    AC97_3D_Control                = 0x22,
    AC97_Powerdown_Ctrl_Stat       = 0x26,
    AC97_Extended_Audio_ID         = 0x28,
    AC97_Extended_Audio_Ctrl_Stat  = 0x2A,
    AC97_PCM_Front_DAC_Rate        = 0x2C,
    AC97_PCM_Surround_DAC_Rate     = 0x2E,
    AC97_PCM_LFE_DAC_Rate          = 0x30,
    AC97_PCM_LR_ADC_Rate           = 0x32,
    AC97_MIC_ADC_Rate              = 0x34,
    AC97_Sigmatel_Analog           = 0x6c, /* a Sigmatel codec */
    AC97_Sigmatel_Dac2Invert       = 0x6e,
    AC97_Vendor_ID1                = 0x7c,
    AC97_Vendor_ID2                = 0x7e
};

#define EACS_VRA 1
#define EACS_VRM 8

/* bus masters */
enum {
    PI_INDEX = 0,   /* PCM in */
    PO_INDEX,       /* PCM out */
    MC_INDEX,       /* mic in */
    SO_INDEX = 7,   /* S/PDIF out */
    LAST_INDEX
};

#define MKREGS(prefix, start)                   \
enum {                                          \
    prefix ## _BDBAR = start,                   \
    prefix ## _CIV = start + 4,                 \
    prefix ## _LVI = start + 5,                 \
    prefix ## _SR = start + 6,                  \
    prefix ## _PICB = start + 8,                \
    prefix ## _PIV = start + 10,                \
    prefix ## _CR = start + 11                  \
}

MKREGS(PI, PI_INDEX * 16);
MKREGS(PO, PO_INDEX * 16);
MKREGS(MC, MC_INDEX * 16);
MKREGS(SO, SO_INDEX * 16);

enum {
    GLOB_CNT = 0x2c,
    GLOB_STA = 0x30,
    CAS      = 0x34
};

#define GET_BM(index) (((index) >> 4) & 7)

/* PCM out pacing: QEMU's audio timer period, the old backend's depth */
#define PO_PERIOD_NS     (10 * SCALE_MS)
#define PO_DEPTH_FRAMES  1024
#define PO_DEPTH_FREQ    48000
#define PO_FRAME_BYTES   4      /* 16-bit stereo */

typedef struct BD {
    uint32_t addr;
    uint32_t ctl_len;
} BD;

typedef struct AC97BusMasterRegs {
    uint32_t bdbar;             /* rw 0, buffer descriptor list base address */
    uint8_t civ;                /* ro 0, current index value */
    uint8_t lvi;                /* rw 0, last valid index */
    uint16_t sr;                /* rw 1, status register */
    uint16_t picb;              /* ro 0, position in current buffer */
    uint8_t piv;                /* ro 0, prefetched index value */
    uint8_t cr;                 /* rw 0, control register */
    unsigned int bd_valid;
    BD bd;
} AC97BusMasterRegs;

typedef struct MCPXACIState {
    PCIDevice dev;

    AddressSpace *as;
    uint32_t glob_cnt;
    uint32_t glob_sta;
    uint32_t cas;               /* codec access semaphore */
    AC97BusMasterRegs bm_regs[LAST_INDEX];
    uint8_t mixer_data[256];

    /* PCM out pacing (above) */
    QEMUTimer *po_timer;
    bool po_open;               /* the codec's rate is valid */
    bool po_active;             /* the bus master is running */
    int64_t po_freq;
    int64_t po_cap;             /* credit limit, bytes */
    int64_t po_credit;          /* bytes it may take now */
    int64_t po_last_ns;
    int64_t po_rem;             /* frame remainder, 1/NANOSECONDS_PER_SECOND */

    MemoryRegion io_nam, io_nabm;
    MemoryRegion mmio;
} MCPXACIState;

#define MCPX_ACI_DEVICE(obj) \
    OBJECT_CHECK(MCPXACIState, (obj), "mcpx-aci")

/* --- PCM out pacing ------------------------------------------------------ */

static void po_open(MCPXACIState *s, int freq)
{
    if (freq <= 0) {
        s->po_open = false;
        s->po_active = false;
        timer_del(s->po_timer);
        return;
    }
    if (!s->po_open) {
        s->po_open = true;
        s->po_active = false;
        s->po_credit = 0;
    }
    s->po_freq = freq;
    s->po_cap = MAX(1, PO_DEPTH_FRAMES * s->po_freq / PO_DEPTH_FREQ)
                * PO_FRAME_BYTES;
    s->po_credit = MIN(s->po_credit, s->po_cap);
}

static void po_set_active(MCPXACIState *s, bool on)
{
    int64_t now;

    if (!s->po_open || s->po_active == on) {
        return;
    }
    s->po_active = on;
    if (!on) {
        timer_del(s->po_timer);
        return;
    }
    /* a freshly started voice has an empty buffer to fill */
    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    s->po_credit = s->po_cap;
    s->po_last_ns = now;
    s->po_rem = 0;
    timer_mod(s->po_timer, now + PO_PERIOD_NS);
}

static void po_earn(MCPXACIState *s, int64_t now)
{
    int64_t ns = now - s->po_last_ns;
    int64_t t;

    s->po_last_ns = now;
    if (ns <= 0) {
        /* a loaded state can put virtual time behind the last update */
        return;
    }
    if (s->po_credit >= s->po_cap) {
        s->po_rem = 0;
        return;
    }
    t = ns * s->po_freq + s->po_rem;
    s->po_rem = t % NANOSECONDS_PER_SECOND;
    s->po_credit = MIN(s->po_cap, s->po_credit +
                       t / NANOSECONDS_PER_SECOND * PO_FRAME_BYTES);
}

/* bytes the bus master takes of up to n: whole frames of its credit */
static uint32_t po_take(MCPXACIState *s, uint32_t n)
{
    int64_t got = MIN((int64_t)n, s->po_credit);

    got -= got % PO_FRAME_BYTES;
    s->po_credit -= got;
    return got;
}

/* --- Bus masters --------------------------------------------------------- */

static void fetch_bd(MCPXACIState *s, AC97BusMasterRegs *r)
{
    uint8_t b[8];

    dma_memory_read(s->as, r->bdbar + r->civ * 8, b, 8, MEMTXATTRS_UNSPECIFIED);
    r->bd_valid = 1;
    r->bd.addr = ldl_le_p(&b[0]) & ~3;
    r->bd.ctl_len = ldl_le_p(&b[4]);
    r->picb = r->bd.ctl_len & 0xffff;
}

static void update_sr(MCPXACIState *s, AC97BusMasterRegs *r, uint32_t new_sr)
{
    /* (QEMU's read past a 3-entry table for S/PDIF out) */
    static const uint32_t masks[LAST_INDEX] = {
        [PI_INDEX] = GS_PIINT, [PO_INDEX] = GS_POINT, [MC_INDEX] = GS_MINT,
        [SO_INDEX] = GS_SOINT,
    };
    int event = 0;
    int level = 0;
    uint32_t new_mask = new_sr & SR_INT_MASK;
    uint32_t old_mask = r->sr & SR_INT_MASK;

    if (new_mask ^ old_mask) {
        if (!new_mask) {
            event = 1;
            level = 0;
        } else {
            if ((new_mask & SR_LVBCI) && (r->cr & CR_LVBIE)) {
                event = 1;
                level = 1;
            }
            if ((new_mask & SR_BCIS) && (r->cr & CR_IOCE)) {
                event = 1;
                level = 1;
            }
        }
    }

    r->sr = new_sr;

    if (!event) {
        return;
    }
    if (level) {
        s->glob_sta |= masks[r - s->bm_regs];
        pci_irq_assert(&s->dev);
    } else {
        s->glob_sta &= ~masks[r - s->bm_regs];
        pci_irq_deassert(&s->dev);
    }
}

static void voice_set_active(MCPXACIState *s, int bm_index, bool on)
{
    if (bm_index == PO_INDEX) {
        po_set_active(s, on);
    }
}

static void reset_bm_regs(MCPXACIState *s, AC97BusMasterRegs *r)
{
    r->bdbar = 0;
    r->civ = 0;
    r->lvi = 0;
    update_sr(s, r, SR_DCH);
    r->picb = 0;
    r->piv = 0;
    r->cr = r->cr & CR_DONT_CLEAR_MASK;
    r->bd_valid = 0;

    voice_set_active(s, r - s->bm_regs, false);
}

/* PCM out: the bus master takes what it may of the current buffer, up to
 * max bytes; the samples themselves are never read */
static void transfer_po(MCPXACIState *s, int elapsed)
{
    AC97BusMasterRegs *r = &s->bm_regs[PO_INDEX];
    int stop = 0;

    if (r->sr & SR_DCH) {
        if (r->cr & CR_RPBM) {
            /* halted while running: underrun, the codec plays on */
            po_take(s, elapsed);
        }
        return;
    }

    while ((elapsed >> 1) && !stop) {
        uint32_t temp;

        if (!r->bd_valid) {
            fetch_bd(s, r);
        }

        if (!r->picb) {
            if (r->civ == r->lvi) {
                r->sr |= SR_DCH; /* CELV? */
                break;
            }
            r->sr &= ~SR_CELV;
            r->civ = r->piv;
            r->piv = (r->piv + 1) % 32;
            fetch_bd(s, r);
            return;
        }

        temp = MIN((uint32_t)r->picb << 1, (uint32_t)elapsed);
        temp = po_take(s, temp);
        if (!temp) {
            break;
        }
        r->bd.addr += temp;
        elapsed -= temp;
        r->picb -= temp >> 1;

        if (!r->picb) {
            uint32_t new_sr = r->sr & ~SR_CELV;

            if (r->bd.ctl_len & BD_IOC) {
                new_sr |= SR_BCIS;
            }

            if (r->civ == r->lvi) {
                new_sr |= SR_LVBCI | SR_DCH | SR_CELV;
                stop = 1;
            } else {
                r->civ = r->piv;
                r->piv = (r->piv + 1) % 32;
                fetch_bd(s, r);
            }

            update_sr(s, r, new_sr);
        }
    }
}

static void po_tick(void *opaque)
{
    MCPXACIState *s = opaque;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    if (!s->po_active) {
        return;
    }
    po_earn(s, now);
    if (s->po_credit >= PO_FRAME_BYTES) {
        transfer_po(s, s->po_credit);
    }
    if (s->po_active) {
        timer_mod(s->po_timer, now + PO_PERIOD_NS);
    }
}

/* --- Codec (native audio mixer) ------------------------------------------ */

static void mixer_store(MCPXACIState *s, uint32_t i, uint16_t v)
{
    if (i + 2 > sizeof(s->mixer_data)) {
        return;
    }
    s->mixer_data[i + 0] = v & 0xff;
    s->mixer_data[i + 1] = v >> 8;
}

static uint16_t mixer_load(MCPXACIState *s, uint32_t i)
{
    if (i + 2 > sizeof(s->mixer_data)) {
        return 0xffff;
    }
    return s->mixer_data[i + 0] | (s->mixer_data[i + 1] << 8);
}

/* the volumes are kept for the guest to read back; nothing is played */
static void set_volume(MCPXACIState *s, int index, uint32_t val)
{
    switch (index) {
    case AC97_Master_Volume_Mute:
        mixer_store(s, index, val & 0xbf3f);
        break;
    case AC97_PCM_Out_Volume_Mute:
        mixer_store(s, index, val & 0x9f1f);
        break;
    case AC97_Record_Gain_Mute:
        mixer_store(s, index, val & 0x8f0f);
        break;
    }
}

static void record_select(MCPXACIState *s, uint32_t val)
{
    uint8_t rs = val & REC_MASK;
    uint8_t ls = (val >> 8) & REC_MASK;
    mixer_store(s, AC97_Record_Select, rs | (ls << 8));
}

static void mixer_reset(MCPXACIState *s)
{
    memset(s->mixer_data, 0, sizeof(s->mixer_data));
    mixer_store(s, AC97_Powerdown_Ctrl_Stat, 0x000f);

    /* Sigmatel 9700 (STAC9700) */
    mixer_store(s, AC97_Vendor_ID1, 0x8384);
    mixer_store(s, AC97_Vendor_ID2, 0x7600); /* 7608 */

    mixer_store(s, AC97_Extended_Audio_ID, 0x0809);
    mixer_store(s, AC97_Extended_Audio_Ctrl_Stat, 0x0009);
    mixer_store(s, AC97_PCM_Front_DAC_Rate, 0xbb80);
    mixer_store(s, AC97_PCM_Surround_DAC_Rate, 0xbb80);
    mixer_store(s, AC97_PCM_LFE_DAC_Rate, 0xbb80);
    mixer_store(s, AC97_PCM_LR_ADC_Rate, 0xbb80);
    mixer_store(s, AC97_MIC_ADC_Rate, 0xbb80);

    record_select(s, 0);
    set_volume(s, AC97_Master_Volume_Mute, 0x8000);
    set_volume(s, AC97_PCM_Out_Volume_Mute, 0x8808);
    set_volume(s, AC97_Record_Gain_Mute, 0x8808);

    po_open(s, mixer_load(s, AC97_PCM_Front_DAC_Rate));
    po_set_active(s, false);
}

static void nam_writew(MCPXACIState *s, uint32_t addr, uint32_t val)
{
    switch (addr) {
    case AC97_Reset:
        mixer_reset(s);
        break;
    case AC97_Powerdown_Ctrl_Stat:
        val &= ~0x800f;
        val |= mixer_load(s, addr) & 0xf;
        mixer_store(s, addr, val);
        break;
    case AC97_PCM_Out_Volume_Mute:
    case AC97_Master_Volume_Mute:
    case AC97_Record_Gain_Mute:
        set_volume(s, addr, val);
        break;
    case AC97_Record_Select:
        record_select(s, val);
        break;
    case AC97_Vendor_ID1:
    case AC97_Vendor_ID2:
    case AC97_Extended_Audio_ID:
        break;
    case AC97_Extended_Audio_Ctrl_Stat:
        if (!(val & EACS_VRA)) {
            mixer_store(s, AC97_PCM_Front_DAC_Rate, 0xbb80);
            mixer_store(s, AC97_PCM_LR_ADC_Rate,    0xbb80);
            po_open(s, 48000);
        }
        if (!(val & EACS_VRM)) {
            mixer_store(s, AC97_MIC_ADC_Rate, 0xbb80);
        }
        mixer_store(s, AC97_Extended_Audio_Ctrl_Stat, val);
        break;
    case AC97_PCM_Front_DAC_Rate:
        if (mixer_load(s, AC97_Extended_Audio_Ctrl_Stat) & EACS_VRA) {
            mixer_store(s, addr, val);
            po_open(s, val);
        }
        break;
    case AC97_MIC_ADC_Rate:
        if (mixer_load(s, AC97_Extended_Audio_Ctrl_Stat) & EACS_VRM) {
            mixer_store(s, addr, val);
        }
        break;
    case AC97_PCM_LR_ADC_Rate:
        if (mixer_load(s, AC97_Extended_Audio_Ctrl_Stat) & EACS_VRA) {
            mixer_store(s, addr, val);
        }
        break;
    case AC97_Headphone_Volume_Mute:
    case AC97_Master_Volume_Mono_Mute:
    case AC97_Master_Tone_RL:
    case AC97_PC_BEEP_Volume_Mute:
    case AC97_Phone_Volume_Mute:
    case AC97_Mic_Volume_Mute:
    case AC97_Line_In_Volume_Mute:
    case AC97_CD_Volume_Mute:
    case AC97_Video_Volume_Mute:
    case AC97_Aux_Volume_Mute:
    case AC97_Record_Gain_Mic_Mute:
    case AC97_General_Purpose:
    case AC97_3D_Control:
    case AC97_Sigmatel_Analog:
    case AC97_Sigmatel_Dac2Invert:
        /* none of the features in these registers are emulated: read-only */
        break;
    default:
        mixer_store(s, addr, val);
        assert(0);
        break;
    }
}

static uint64_t nam_read(void *opaque, hwaddr addr, unsigned size)
{
    MCPXACIState *s = opaque;

    if ((addr / size) > 256) {
        return -1;
    }
    s->cas = 0;
    return size == 2 ? mixer_load(s, addr) : ~0U;
}

static void nam_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MCPXACIState *s = opaque;

    if ((addr / size) > 256) {
        return;
    }
    s->cas = 0;
    if (size == 2) {
        nam_writew(s, addr, val);
    }
}

/* --- Bus master registers ------------------------------------------------ */

static uint32_t nabm_readb(MCPXACIState *s, uint32_t addr)
{
    AC97BusMasterRegs *r = &s->bm_regs[GET_BM(addr)];
    uint32_t val = ~0U;

    switch (addr) {
    case CAS:
        val = s->cas;
        s->cas = 1;
        break;
    case PI_CIV:
    case PO_CIV:
    case MC_CIV:
    case SO_CIV:
        val = r->civ;
        break;
    case PI_LVI:
    case PO_LVI:
    case MC_LVI:
    case SO_LVI:
        val = r->lvi;
        break;
    case PI_PIV:
    case PO_PIV:
    case MC_PIV:
    case SO_PIV:
        val = r->piv;
        break;
    case PI_CR:
    case PO_CR:
    case MC_CR:
    case SO_CR:
        val = r->cr;
        break;
    case PI_SR:
    case PO_SR:
    case MC_SR:
    case SO_SR:
        val = r->sr & 0xff;
        break;
    default:
        assert(0);
        break;
    }
    return val;
}

static uint32_t nabm_readw(MCPXACIState *s, uint32_t addr)
{
    AC97BusMasterRegs *r = &s->bm_regs[GET_BM(addr)];

    switch (addr) {
    case PI_SR:
    case PO_SR:
    case MC_SR:
    case SO_SR:
        return r->sr;
    case PI_PICB:
    case PO_PICB:
    case MC_PICB:
    case SO_PICB:
        return r->picb;
    default:
        return nabm_readb(s, addr) | (nabm_readb(s, addr + 1) << 8);
    }
}

static uint32_t nabm_readl(MCPXACIState *s, uint32_t addr)
{
    AC97BusMasterRegs *r = &s->bm_regs[GET_BM(addr)];

    switch (addr) {
    case PI_BDBAR:
    case PO_BDBAR:
    case MC_BDBAR:
    case SO_BDBAR:
        return r->bdbar;
    case PI_CIV:
    case PO_CIV:
    case MC_CIV:
    case SO_CIV:
        return r->civ | (r->lvi << 8) | (r->sr << 16);
    case PI_PICB:
    case PO_PICB:
    case MC_PICB:
    case SO_PICB:
        return r->picb | (r->piv << 16) | (r->cr << 24);
    case GLOB_CNT:
        return s->glob_cnt;
    case GLOB_STA:
        return s->glob_sta | GS_S0CR;
    default:
        return nabm_readw(s, addr) | (nabm_readw(s, addr + 2) << 16);
    }
}

static void nabm_writeb(MCPXACIState *s, uint32_t addr, uint32_t val)
{
    AC97BusMasterRegs *r = &s->bm_regs[GET_BM(addr)];

    switch (addr) {
    case PI_LVI:
    case PO_LVI:
    case MC_LVI:
    case SO_LVI:
        if ((r->cr & CR_RPBM) && (r->sr & SR_DCH)) {
            r->sr &= ~(SR_DCH | SR_CELV);
            r->civ = r->piv;
            r->piv = (r->piv + 1) % 32;
            fetch_bd(s, r);
        }
        r->lvi = val % 32;
        break;
    case PI_CR:
    case PO_CR:
    case MC_CR:
    case SO_CR:
        if (val & CR_RR) {
            reset_bm_regs(s, r);
        } else {
            r->cr = val & CR_VALID_MASK;
            if (!(r->cr & CR_RPBM)) {
                voice_set_active(s, r - s->bm_regs, false);
                r->sr |= SR_DCH;
            } else {
                r->civ = r->piv;
                r->piv = (r->piv + 1) % 32;
                fetch_bd(s, r);
                r->sr &= ~SR_DCH;
                voice_set_active(s, r - s->bm_regs, true);
            }
        }
        break;
    case PI_SR:
    case PO_SR:
    case MC_SR:
    case SO_SR:
        r->sr |= val & ~(SR_RO_MASK | SR_WCLEAR_MASK);
        update_sr(s, r, r->sr & ~(val & SR_WCLEAR_MASK));
        break;
    }
}

static void nabm_writew(MCPXACIState *s, uint32_t addr, uint32_t val)
{
    AC97BusMasterRegs *r = &s->bm_regs[GET_BM(addr)];

    switch (addr) {
    case PI_SR:
    case PO_SR:
    case MC_SR:
    case SO_SR:
        r->sr |= val & ~(SR_RO_MASK | SR_WCLEAR_MASK);
        update_sr(s, r, r->sr & ~(val & SR_WCLEAR_MASK));
        break;
    default:
        nabm_writeb(s, addr, val & 0xff);
        nabm_writeb(s, addr + 1, (val >> 8) & 0xff);
        break;
    }
}

static void nabm_writel(MCPXACIState *s, uint32_t addr, uint32_t val)
{
    AC97BusMasterRegs *r = &s->bm_regs[GET_BM(addr)];

    switch (addr) {
    case PI_BDBAR:
    case PO_BDBAR:
    case MC_BDBAR:
    case SO_BDBAR:
        r->bdbar = val & ~3;
        break;
    case GLOB_CNT:
        /* TODO: Handle WR or CR being set (warm/cold reset requests) */
        if (!(val & (GC_WR | GC_CR))) {
            s->glob_cnt = val & GC_VALID_MASK;
        }
        break;
    case GLOB_STA:
        s->glob_sta &= ~(val & GS_WCLEAR_MASK);
        s->glob_sta |= (val & ~(GS_WCLEAR_MASK | GS_RO_MASK)) & GS_VALID_MASK;
        break;
    default:
        nabm_writew(s, addr, val & 0xffff);
        nabm_writew(s, addr + 2, (val >> 16) & 0xffff);
        break;
    }
}

static uint64_t nabm_read(void *opaque, hwaddr addr, unsigned size)
{
    switch (size) {
    case 1:
        return nabm_readb(opaque, addr);
    case 2:
        return nabm_readw(opaque, addr);
    case 4:
        return nabm_readl(opaque, addr);
    default:
        return -1;
    }
}

static void nabm_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    switch (size) {
    case 1:
        nabm_writeb(opaque, addr, val);
        break;
    case 2:
        nabm_writew(opaque, addr, val);
        break;
    case 4:
        nabm_writel(opaque, addr, val);
        break;
    }
}

static const MemoryRegionOps nam_ops = {
    .read = nam_read,
    .write = nam_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const MemoryRegionOps nabm_ops = {
    .read = nabm_read,
    .write = nabm_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/* --- Device -------------------------------------------------------------- */

static void mcpx_aci_realize(PCIDevice *dev, Error **errp)
{
    MCPXACIState *d = MCPX_ACI_DEVICE(dev);

    dev->config[PCI_INTERRUPT_PIN] = 0x01;

    /* BAR 2: the codec's registers at 0, the bus masters' at 0x100 */
    memory_region_init(&d->mmio, OBJECT(dev), "mcpx-aci-mmio", 0x1000);
    memory_region_init_io(&d->io_nam, OBJECT(dev), &nam_ops, d,
                          "mcpx-aci-nam", 0x100);
    memory_region_init_io(&d->io_nabm, OBJECT(dev), &nabm_ops, d,
                          "mcpx-aci-nabm", 0x80);
    memory_region_add_subregion(&d->mmio, 0x0, &d->io_nam);
    memory_region_add_subregion(&d->mmio, 0x100, &d->io_nabm);
    pci_register_bar(&d->dev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->mmio);

    d->as = pci_get_address_space(dev);
    d->po_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, po_tick, d);

    reset_bm_regs(d, &d->bm_regs[PI_INDEX]);
    reset_bm_regs(d, &d->bm_regs[PO_INDEX]);
    reset_bm_regs(d, &d->bm_regs[MC_INDEX]);
    mixer_reset(d);
}

static void mcpx_aci_exit(PCIDevice *dev)
{
    MCPXACIState *d = MCPX_ACI_DEVICE(dev);

    timer_free(d->po_timer);
    d->po_timer = NULL;
}

static const VMStateDescription vmstate_aci_bm_regs = {
    .name = "mcpx-aci-bm",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(bdbar, AC97BusMasterRegs),
        VMSTATE_UINT8(civ, AC97BusMasterRegs),
        VMSTATE_UINT8(lvi, AC97BusMasterRegs),
        VMSTATE_UINT16(sr, AC97BusMasterRegs),
        VMSTATE_UINT16(picb, AC97BusMasterRegs),
        VMSTATE_UINT8(piv, AC97BusMasterRegs),
        VMSTATE_UINT8(cr, AC97BusMasterRegs),
        VMSTATE_UINT32(bd_valid, AC97BusMasterRegs),
        VMSTATE_UINT32(bd.addr, AC97BusMasterRegs),
        VMSTATE_UINT32(bd.ctl_len, AC97BusMasterRegs),
        VMSTATE_END_OF_LIST()
    },
};

/* Version 1 had the PCI device alone: a state from then loads with the
 * AC97 link as the running session has it */
static const VMStateDescription vmstate_mcpx_aci = {
    .name = "mcpx-aci",
    .version_id = 2,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, MCPXACIState),
        VMSTATE_UINT32_V(glob_cnt, MCPXACIState, 2),
        VMSTATE_UINT32_V(glob_sta, MCPXACIState, 2),
        VMSTATE_UINT32_V(cas, MCPXACIState, 2),
        VMSTATE_STRUCT_ARRAY(bm_regs, MCPXACIState, LAST_INDEX, 2,
                             vmstate_aci_bm_regs, AC97BusMasterRegs),
        VMSTATE_BUFFER_V(mixer_data, MCPXACIState, 2),
        VMSTATE_BOOL_V(po_open, MCPXACIState, 2),
        VMSTATE_BOOL_V(po_active, MCPXACIState, 2),
        VMSTATE_INT64_V(po_freq, MCPXACIState, 2),
        VMSTATE_INT64_V(po_cap, MCPXACIState, 2),
        VMSTATE_INT64_V(po_credit, MCPXACIState, 2),
        VMSTATE_INT64_V(po_last_ns, MCPXACIState, 2),
        VMSTATE_INT64_V(po_rem, MCPXACIState, 2),
        VMSTATE_TIMER_PTR_V(po_timer, MCPXACIState, 2),
        VMSTATE_END_OF_LIST()
    },
};

static void mcpx_aci_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_NVIDIA;
    k->device_id = PCI_DEVICE_ID_NVIDIA_MCPX_ACI;
    k->revision = 177;
    k->class_id = PCI_CLASS_MULTIMEDIA_AUDIO;
    k->realize = mcpx_aci_realize;
    k->exit = mcpx_aci_exit;

    dc->desc = "MCPX Audio Codec Interface";
    dc->vmsd = &vmstate_mcpx_aci;
}

static const TypeInfo mcpx_aci_info = {
    .name          = "mcpx-aci",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(MCPXACIState),
    .class_init    = mcpx_aci_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void mcpx_aci_register(void)
{
    type_register_static(&mcpx_aci_info);
}

type_init(mcpx_aci_register);
