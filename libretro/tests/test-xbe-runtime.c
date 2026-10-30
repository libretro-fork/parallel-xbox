/* Firmware-free validation of the runtime compatibility plan. */
#include "hw/xbox/xbe-runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void put(uint8_t *p, uint32_t value)
{
    p[0] = value; p[1] = value >> 8; p[2] = value >> 16; p[3] = value >> 24;
}

int main(void)
{
    uint8_t h[0x178] = { 0 }, cert[0x1dc] = { 0 };
    put(h, 0x48454258); put(h + 0x104, 0x10000);
    put(h + 0x108, 4096); put(h + 0x10c, 65536);
    put(h + 0x118, 0x10200); put(h + 0x128, 0x12345 ^ 0xa8fc57ab);
    put(cert, 0x1dc); put(cert + 8, 0x46530002);
    put(cert + 0x1d8, 0xa5a50003);
    XbeRuntimePlan p;
    assert(xbe_runtime_plan(h, sizeof(h), cert, sizeof(cert), false, &p));
    assert(p.entry == 0x12345 && p.certificate == 0x10200 && p.flags == 0x103d8);
    assert(xbe_runtime_u32(cert + 0x1d8) == 0xa5a50003); /* no mutation */
    put(h + 0x128, 0x12345 ^ 0x94859d4b);
    assert(xbe_runtime_plan(h, sizeof(h), cert, sizeof(cert), false, &p));
    assert(p.entry == 0x12345);
    put(h + 0x128, 0x12345);
    assert(!xbe_runtime_plan(h, sizeof(h), cert, sizeof(cert), false, &p));
    assert(xbe_runtime_plan(h, sizeof(h), cert, sizeof(cert), true, &p));
    put(h + 0x128, 0x12345 ^ 0x94859d4b);
    assert(!xbe_runtime_plan(h, sizeof(h) - 1, cert, sizeof(cert), false, &p));
    assert(!xbe_runtime_plan(h, sizeof(h), cert, sizeof(cert) - 1, false, &p));
    put(cert + 8, 0x54430006);
    assert(!xbe_runtime_plan(h, sizeof(h), cert, sizeof(cert), false, &p));
    put(cert + 8, 0x46530002);
    put(h + 0x118, 0xfffffff0);
    assert(!xbe_runtime_plan(h, sizeof(h), cert, sizeof(cert), false, &p));
    put(h + 0x118, 0x10200); put(cert, 0xffffffff);
    assert(!xbe_runtime_plan(h, sizeof(h), cert, sizeof(cert), false, &p));
    put(cert, 0x1dc); put(h + 0x10c, 0xffffffff);
    assert(!xbe_runtime_plan(h, sizeof(h), cert, sizeof(cert), false, &p));
    put(h + 0x10c, 65536); put(h + 0x128, 0x10010 ^ 0xa8fc57ab);
    assert(!xbe_runtime_plan(h, sizeof(h), cert, sizeof(cert), false, &p));
    puts("XBE runtime plan, title guard, entry decoding and bounds: PASS");
}
