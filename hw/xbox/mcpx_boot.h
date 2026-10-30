/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Independent MCPX 1.0 boot helpers. No console ROM code is included.
 * Behavior: https://xboxdevwiki.net/Boot_Process#Xcodes
 * Kept independent of QEMU so the interpreter can be tested with fake buses.
 */
#ifndef XBOX_MCPX_BOOT_H
#define XBOX_MCPX_BOOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct McpxBootBus {
    void *opaque;
    uint32_t (*read)(void *opaque, uint32_t addr, unsigned size, bool io);
    void (*write)(void *opaque, uint32_t addr, uint32_t value,
                  unsigned size, bool io);
} McpxBootBus;

typedef struct McpxXcodes {
    uint32_t pc, result, scratch;
} McpxXcodes;

/* One nine-byte instruction, including the indirect (0x07) form. */
static inline bool mcpx_xcode_step(McpxXcodes *s, const McpxBootBus *bus)
{
    uint32_t op = bus->read(bus->opaque, s->pc, 1, false);
    uint32_t a = bus->read(bus->opaque, s->pc + 1, 4, false);
    uint32_t b = bus->read(bus->opaque, s->pc + 5, 4, false);

    s->pc += 9;
    if (op == 0x07) {
        op = a & 0xff;
        a = b;
        b = s->result;
    }
    switch (op) {
    case 0x02:
        s->result = bus->read(bus->opaque, a & 0x0fffffff, 4, false);
        break;
    case 0x03:
        bus->write(bus->opaque, a, b, 4, false);
        break;
    case 0x04:
        /* Xcodes must not unmap the ROM they are running under. */
        if (a == 0x80000880) {
            b &= ~2u;
        }
        bus->write(bus->opaque, 0xcf8, a, 4, true);
        bus->write(bus->opaque, 0xcfc, b, 4, true);
        break;
    case 0x05:
        bus->write(bus->opaque, 0xcf8, a, 4, true);
        s->result = bus->read(bus->opaque, 0xcfc, 4, true);
        break;
    case 0x06:
        s->result = (s->result & a) | b;
        break;
    case 0x08:
        if (s->result != a) {
            s->pc += b;
        }
        break;
    case 0x09:
        s->pc += b;
        break;
    case 0x10:
        s->scratch = (s->scratch & a) | b;
        s->result = s->scratch;
        break;
    case 0x11:
        bus->write(bus->opaque, a & 0xffff, b & 0xff, 1, true);
        break;
    case 0x12:
        s->result = bus->read(bus->opaque, a & 0xffff, 1, true);
        break;
    case 0xee:
        return false;
    default:
        /* Undefined instructions are ignored by the MCPX interpreter. */
        break;
    }
    return true;
}

/* RC4 with the 16-byte MCPX 1.0 key. The caller supplies the key so the
 * algorithm can also be checked against independent published test vectors.
 * Leave the permutation and final indices available for guest scratch RAM.
 */
static inline void mcpx_boot_rc4(uint8_t state[258], const uint8_t key[16],
                                 uint8_t *data, size_t size)
{
    unsigned i;
    uint8_t j = 0, x = 0, t;

    for (i = 0; i < 256; i++) {
        state[i] = i;
    }
    for (i = 0; i < 256; i++) {
        j += state[i] + key[i & 15];
        t = state[i];
        state[i] = state[j];
        state[j] = t;
    }
    j = 0;
    for (i = 0; i < size; i++) {
        x++;
        j += state[x];
        t = state[x];
        state[x] = state[j];
        state[j] = t;
        data[i] ^= state[(uint8_t)(state[x] + state[j])];
    }
    state[256] = x;
    state[257] = j;
}

#endif
