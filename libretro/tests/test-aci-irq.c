/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* CONSTANTS */
/* REGISTERS */
/* TYPES */
typedef struct { bool irq; } PCIDevice;
typedef struct {
    PCIDevice dev;
    uint32_t glob_sta, glob_cnt;
    AC97BusMasterRegs bm_regs[LAST_INDEX];
    unsigned int fetches, voice_updates;
} MCPXACIState;
static void pci_irq_assert(PCIDevice *d) { d->irq = true; }
static void pci_irq_deassert(PCIDevice *d) { d->irq = false; }
static void voice_set_active(MCPXACIState *s, int index, bool active)
{ (void)index; (void)active; s->voice_updates++; }
static void fetch_bd(MCPXACIState *s, AC97BusMasterRegs *r)
{ s->fetches++; r->bd_valid = 1; }
/* PRODUCTION */
static unsigned checks;
static const unsigned banks[] = {PI_INDEX, PO_INDEX, MC_INDEX, SO_INDEX};
static const uint32_t bits[] = {GS_PIINT, GS_POINT, GS_MINT, GS_SOINT};
static const uint32_t stream_bits = GS_PIINT | GS_POINT | GS_MINT | GS_SOINT;
#define REQUIRE(condition) do { if (!(condition)) { \
    fprintf(stderr, "ACI IRQ: line %u: %s\n", __LINE__, #condition); exit(1); \
} } while (0)
static void verify(const MCPXACIState *s)
{
    uint32_t expected = 0;
    for (unsigned i = 0; i < 4; i++) {
        const AC97BusMasterRegs *r = &s->bm_regs[banks[i]];
        if (((r->sr & 8) && (r->cr & 16)) ||
            ((r->sr & 4) && (r->cr & 4)) ||
            ((r->sr & 16) && (r->cr & 8))) expected |= bits[i];
    }
    if ((s->glob_sta & stream_bits) != expected) {
        fprintf(stderr, "ACI IRQ: check=%u stream bits=%x expected=%x\n", checks, s->glob_sta & stream_bits, expected);
        exit(1);
    }
    REQUIRE(s->dev.irq == (expected != 0));
    REQUIRE((s->glob_sta & (GS_MD3 | GS_RCS)) == (GS_MD3 | GS_RCS));
    checks++;
}
static MCPXACIState fresh(void)
{
    MCPXACIState s = {0};
    s.glob_sta = GS_MD3 | GS_RCS;
    return s;
}
static void status_matrix(void)
{
    for (unsigned b = 0; b < 4; b++) {
        for (unsigned cr = 0; cr < 32; cr++) {
            for (unsigned old = 0; old < 32; old++) {
                for (unsigned next = 0; next < 32; next++) {
                    MCPXACIState s = fresh();
                    AC97BusMasterRegs *r = &s.bm_regs[banks[b]];
                    r->cr = cr;
                    update_sr(&s, r, old); verify(&s);
                    update_sr(&s, r, next); verify(&s);
                    REQUIRE(r->sr == next);
                }
            }
        }
    }
    /* Reserved bank statuses cannot invent a stream interrupt. */
    for (unsigned b = 3; b < 7; b++) {
        MCPXACIState s = fresh();
        s.bm_regs[b].cr = 31;
        update_sr(&s, &s.bm_regs[b], 31); verify(&s);
    }
}
static void control_and_acknowledge(void)
{
    for (unsigned b = 0; b < 4; b++) {
        unsigned base = banks[b] * 16;
        for (unsigned status = 0; status < 32; status++) {
            for (unsigned cr = 0; cr < 32; cr++) {
                MCPXACIState s = fresh();
                AC97BusMasterRegs *r = &s.bm_regs[banks[b]];
                update_sr(&s, r, status);
                nabm_writeb(&s, base + 11, cr); verify(&s);
                nabm_writeb(&s, base + 11, 0); verify(&s);
            }
        }
    }
    for (unsigned pending = 0; pending < 16; pending++) {
        for (unsigned a = 0; a < 4; a++) for (unsigned b = 0; b < 4; b++) {
            if (b == a) continue;
            for (unsigned c = 0; c < 4; c++) {
                if (c == a || c == b) continue;
                unsigned d = 6 - a - b - c;
                unsigned order[] = {a,b,c,d};
                MCPXACIState s = fresh();
                for (unsigned i = 0; i < 4; i++) {
                    AC97BusMasterRegs *r = &s.bm_regs[banks[i]];
                    r->cr = CR_IOCE | CR_FEIE | CR_LVBIE;
                    update_sr(&s, r, pending & (1u << i) ? SR_INT_MASK : 0);
                }
                verify(&s);
                for (unsigned i = 0; i < 4; i++) {
                    unsigned base = banks[order[i]] * 16;
                    nabm_writeb(&s, base + 6, SR_BCIS); verify(&s);
                    nabm_writew(&s, base + 6, SR_LVBCI); verify(&s);
                    nabm_writel(&s, base + 4, SR_FIFOE << 16); verify(&s);
                }
            }
        }
    }
}
static void reset_paths(void)
{
    MCPXACIState s = fresh();
    memset(s.bm_regs, 0xA5, sizeof(s.bm_regs));
    for (unsigned i = 0; i < 4; i++) {
        s.bm_regs[banks[i]].sr = 0;
        s.bm_regs[banks[i]].cr = 0;
    }
    for (unsigned i = 0; i < 4; i++) {
        AC97BusMasterRegs *r = &s.bm_regs[banks[i]];
        r->cr = CR_VALID_MASK;
        update_sr(&s, r, SR_INT_MASK); verify(&s);
    }
    reset_bus_masters(&s); verify(&s);
    for (unsigned i = 0; i < 4; i++) {
        AC97BusMasterRegs *r = &s.bm_regs[banks[i]];
        REQUIRE(r->bdbar == 0 && r->civ == 0 && r->lvi == 0);
        REQUIRE(r->picb == 0 && r->piv == 0 && r->bd_valid == 0);
        REQUIRE(r->sr == SR_DCH && r->cr == CR_DONT_CLEAR_MASK);
    }
    REQUIRE(s.bm_regs[3].bdbar == 0xA5A5A5A5);
    /* Reset S/PDIF while PCM still owns the shared interrupt. */
    s = fresh();
    s.bm_regs[PO_INDEX].cr = s.bm_regs[SO_INDEX].cr = CR_IOCE;
    update_sr(&s, &s.bm_regs[PO_INDEX], SR_BCIS);
    update_sr(&s, &s.bm_regs[SO_INDEX], SR_BCIS);
    nabm_writeb(&s, SO_CR, CR_RR); verify(&s);
    REQUIRE(s.dev.irq && s.bm_regs[SO_INDEX].sr == SR_DCH);
    nabm_writeb(&s, PO_CR, CR_RR); verify(&s);
    REQUIRE(!s.dev.irq);
}
int main(void)
{
    status_matrix(); control_and_acknowledge(); reset_paths();
    printf("ACI IRQ: %u status/control/acknowledge/reset checks passed\n", checks);
    return 0;
}
