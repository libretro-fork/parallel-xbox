/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <retro_atomic.h>
#include "hw/xbox/nv2a/nv2a_regs.h"
#include "hw/xbox/nv2a/pgraph/gamma.h"
typedef uint64_t hwaddr;
typedef struct {
    struct {
        uint16_t write_mode_address;
        uint8_t palette[768];
        retro_atomic_int_t packed[256], dirty;
    } puserdac;
} NV2AState;
static NV2AState device, *g_nv2a = &device;
static unsigned reads;
static bool inject;
void prmdio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static int snapshot_read(retro_atomic_int_t *p)
{
    int value = retro_atomic_load_relaxed_int(p);
    reads++;
    if (inject && p == &device.puserdac.packed[0]) {
        inject = false;
        prmdio_write(&device, NV_USER_DAC_WRITE_MODE_ADDRESS, 0, 1);
        prmdio_write(&device, NV_USER_DAC_PALETTE_DATA, 123, 1);
    }
    return value;
}
#undef retro_atomic_load_relaxed_int
#define retro_atomic_load_relaxed_int(p) snapshot_read(p)
#define nv2a_reg_log_write(a,b,c,d) ((void)(a),(void)(b),(void)(c),(void)(d))
/* PRODUCTION */

static void write_ramp(unsigned kind)
{
    prmdio_write(&device, NV_USER_DAC_WRITE_MODE_ADDRESS, 0, 1);
    for (unsigned i = 0; i < 256; i++) for (unsigned c = 0; c < 3; c++) {
        unsigned value = kind == 0 ? i : kind == 1 ? 255-i :
                         kind == 2 ? (i+c*71)&255 : (i*i+c*47)&255;
        prmdio_write(&device, NV_USER_DAC_PALETTE_DATA, value, 1);
    }
}

int main(void)
{
    uint32_t ramp[256], untouched[256];PGRAPHGammaState state={0};
    memset(untouched, 0xa5, sizeof(untouched));memcpy(ramp, untouched, sizeof(ramp));
    assert(!nv2a_get_dac_palette_snapshot(ramp, false));
    assert(!reads && !memcmp(ramp, untouched, sizeof(ramp)));
    unsigned cases=0;
    for (unsigned n=0;n<1024;n++) {
        write_ramp(n%4);reads=0;
        assert(nv2a_get_dac_palette_snapshot(ramp, false) && reads==256);
        for (unsigned i=0;i<256;i++) {
            uint32_t expected=0;
            for(unsigned c=0;c<3;c++)expected|=(uint32_t)device.puserdac.palette[i*3+c]<<(c*8);
            assert(ramp[i]==expected);
        }
        assert(pgraph_gamma_update(&state,ramp));
        assert(state.enabled==(n%4!=0));
        uint64_t version=state.version;
        assert(!pgraph_gamma_update(&state,ramp)&&state.version==version);
        reads=0;assert(!nv2a_get_dac_palette_snapshot(ramp,false)&&!reads);
        /* Rewriting the same guest ramp changes neither shader table nor version. */
        write_ramp(n%4);assert(nv2a_get_dac_palette_snapshot(ramp,false));
        assert(!pgraph_gamma_update(&state,ramp)&&state.version==version);cases++;
    }
    write_ramp(0);inject=true;
    assert(nv2a_get_dac_palette_snapshot(ramp,false));
    assert((ramp[0]&255)==0); /* The write happened behind the read frontier. */
    assert(nv2a_get_dac_palette_snapshot(ramp,false));
    assert((ramp[0]&255)==123);
    assert(!nv2a_get_dac_palette_snapshot(ramp,false));
    assert(nv2a_get_dac_palette_snapshot(ramp,true));
    /* End-of-palette wrap retains the original byte-address convention. */
    prmdio_write(&device,NV_USER_DAC_WRITE_MODE_ADDRESS,255,1);
    for(unsigned i=0;i<4;i++)prmdio_write(&device,NV_USER_DAC_PALETTE_DATA,17+i,1);
    assert(nv2a_get_dac_palette_snapshot(ramp,false));
    assert(ramp[255]==(17u|(18u<<8)|(19u<<16))&&(ramp[0]&255)==20);
    state.version=UINT64_MAX;assert(pgraph_gamma_update(&state,ramp)&&state.version==1);
    for(size_t a=1;a<=65536;a*=2)for(size_t atom=1;atom<=65536;atom*=2) {
        size_t stride=pgraph_gamma_stride(a,atom);
        assert(stride>=1024&&stride%a==0&&stride%atom==0);
        for(unsigned i=0;i<PGRAPH_GAMMA_SLOTS;i++)assert(i*stride+1024<=(i+1)*stride);
    }
    printf("PASS: %u palette sequences, repeated/identity/forced snapshots, concurrent-write retry, byte wrap, version wrap and isolated GPU strides\n",cases);
}
