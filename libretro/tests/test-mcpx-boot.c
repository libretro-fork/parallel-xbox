/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hw/xbox/mcpx_boot.h"

static uint8_t instruction[9];
static uint32_t last_addr, last_value, pci_addr;
static unsigned last_size, writes;
static bool last_io;

static uint32_t read_bus(void *opaque, uint32_t addr, unsigned size, bool io)
{
    uint32_t v = 0;
    if (!io && addr < 9) {
        for (unsigned n = 0; n < size; n++) {
            assert(addr + n < 9);
            v |= (uint32_t)instruction[addr + n] << (8 * n);
        }
        return v;
    }
    last_addr = addr;
    last_size = size;
    last_io = io;
    return 0x12345678 & (size == 1 ? 0xff : UINT32_MAX);
}

static void write_bus(void *opaque, uint32_t addr, uint32_t value,
                      unsigned size, bool io)
{
    writes++;
    if (io && addr == 0xcf8) {
        pci_addr = value;
    }
    last_addr = addr;
    last_value = value;
    last_size = size;
    last_io = io;
}

static bool step(McpxXcodes *s, unsigned op, uint32_t a, uint32_t b)
{
    const McpxBootBus bus = { .read = read_bus, .write = write_bus };
    instruction[0] = op;
    for (unsigned i = 0; i < 4; i++) {
        instruction[1 + i] = a >> (8 * i);
        instruction[5 + i] = b >> (8 * i);
    }
    s->pc = 0;
    return mcpx_xcode_step(s, &bus);
}

static void test_xcodes(void)
{
    McpxXcodes s = { 0 };
    assert(step(&s, 0x02, 0xf1234567, 0));
    assert(s.result == 0x12345678 && last_addr == 0x01234567);
    assert(last_size == 4 && !last_io);
    step(&s, 0x03, 0xfd000000, 0xaabbccdd);
    assert(last_addr == 0xfd000000 && last_value == 0xaabbccdd && !last_io);
    step(&s, 0x06, 0xffff, 0x80000000);
    assert(s.result == 0x80005678);
    step(&s, 0x07, 0x03, 0x10000);
    assert(last_addr == 0x10000 && last_value == s.result);
    writes = 0;
    step(&s, 0x04, 0x80000880, 0xffffffff);
    assert(writes == 2 && pci_addr == 0x80000880);
    assert(last_addr == 0xcfc && last_value == 0xfffffffd && last_io);
    step(&s, 0x04, 0x80000884, 2);
    assert(last_value == 2);
    step(&s, 0x05, 0x80000800, 0);
    assert(pci_addr == 0x80000800 && s.result == 0x12345678);
    step(&s, 0x08, s.result, 100);
    assert(s.pc == 9);
    step(&s, 0x08, 0, (uint32_t)-18);
    assert(s.pc == (uint32_t)-9);
    step(&s, 0x09, 0, (uint32_t)-9);
    assert(s.pc == 0);
    step(&s, 0x10, 0, 0xabc);
    step(&s, 0x06, 0, 0);
    step(&s, 0x10, 0xff, 0x1000);
    assert(s.result == 0x10bc && s.scratch == 0x10bc);
    step(&s, 0x11, 0x12345678, 0xaabbccdd);
    assert(last_addr == 0x5678 && last_value == 0xdd && last_size == 1 && last_io);
    step(&s, 0x12, 0x12345678, 0);
    assert(last_addr == 0x5678 && s.result == 0x78 && last_size == 1 && last_io);
    assert(step(&s, 0, 0, 0));
    assert(!step(&s, 0xee, 0, 0));
    assert(!step(&s, 0x07, 0xee, 0));
}

static void test_rc4(void)
{
    /* RFC 6229's 128-bit key, offsets 0 and 4096, exercises wraparound. */
    const uint8_t first[16] = {
        0x9a, 0xc7, 0xcc, 0x9a, 0x60, 0x9d, 0x1e, 0xf7,
        0xb2, 0x93, 0x28, 0x99, 0xcd, 0xe4, 0x1b, 0x97,
    };
    const uint8_t later[16] = {
        0xa3, 0x6a, 0x4c, 0x30, 0x1a, 0xe8, 0xac, 0x13,
        0x61, 0x0c, 0xcb, 0xc1, 0x22, 0x56, 0xca, 0xcc,
    };
    uint8_t key[16], data[0x6000] = { 0 }, state[258];
    for (unsigned i = 0; i < 16; i++) {
        key[i] = i + 1;
    }
    mcpx_boot_rc4(state, key, data, sizeof(data));
    assert(memcmp(data, first, 16) == 0);
    assert(memcmp(data + 4096, later, 16) == 0);
    mcpx_boot_rc4(state, key, data, sizeof(data));
    for (unsigned i = 0; i < sizeof(data); i++) {
        assert(data[i] == 0);
    }
}

int main(void)
{
    test_xcodes();
    test_rc4();
    puts("MCPX Xcodes and RC4: PASS");
    return 0;
}
