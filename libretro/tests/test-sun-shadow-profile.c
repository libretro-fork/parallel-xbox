/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <streams/file_stream.h>
#include "hw/xbox/nv2a/pgraph/sun-shadow-profile.h"
#include "hw/xbox/nv2a/pgraph/sun-shadow-scene.h"
#include "hw/xbox/nv2a/pgraph/sun-shadow-fifo.h"

/* The production streaming hash must never fall back to file I/O. */
RFILE *filestream_open(const char *p, unsigned m, unsigned h)
{ (void)p; (void)m; (void)h; abort(); }
int64_t filestream_read(RFILE *f, void *p, int64_t n)
{ (void)f; (void)p; (void)n; abort(); }
int64_t filestream_view(RFILE *f, int64_t n, const uint8_t **p)
{ (void)f; (void)n; (void)p; abort(); }
int filestream_close(RFILE *f) { (void)f; abort(); }
#ifdef _WIN32
void retro_sleep_us(unsigned us) { (void)us; abort(); }
#endif

static uint8_t ram[0x40000];
static unsigned reads, fail_at;
static bool read_ram(void *opaque, uint32_t address, void *dst, size_t size)
{
    (void)opaque;
    if (++reads == fail_at || address > sizeof(ram) ||
        size > sizeof(ram) - address) {
        return false;
    }
    memcpy(dst, ram + address, size);
    return true;
}

static void put(uint32_t address, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++) ram[address + i] = value >> (8 * i);
}

static PgraphSunProfile fixture(void)
{
    memset(ram, 0, sizeof(ram));
    PgraphSunProfile p = {
        .title_id = 0x12345678, .timestamp = 3, .version = 5,
        .text_address = 0x11000, .text_size = 8193,
    };
    put(0x10000, 0x48454258);
    put(0x10104, 0x10000); put(0x10108, 4096); put(0x1010c, 0x30000);
    put(0x10114, p.timestamp); put(0x10118, 0x10200);
    put(0x1011c, 1); put(0x10120, 0x10400);
    put(0x10200, 0x1dc); put(0x10208, p.title_id); put(0x102ac, p.version);
    put(0x10400, 0x16); put(0x10404, p.text_address);
    put(0x10408, p.text_size); put(0x10410, p.text_size);
    for (unsigned i = 0; i < p.text_size; i++) ram[p.text_address + i] = i * 73;
    struct sha256_state hash;
    sha256_stream_init(&hash, 0);
    sha256_stream_update(&hash, ram + p.text_address, p.text_size);
    sha256_stream_final(&hash, p.text_sha256);
    reads = fail_at = 0;
    return p;
}

static void synthetic(void)
{
    PgraphSunProfile p = fixture();
    assert(pgraph_sun_validate(&p, read_ram, NULL));
    unsigned complete_reads = reads;
    for (unsigned i = 1; i <= complete_reads; i++) {
        reads = 0; fail_at = i;
        assert(!pgraph_sun_validate(&p, read_ram, NULL));
    }
    fail_at = 0;
    /* Every authenticated byte, including the partial final chunk. */
    for (unsigned i = 0; i < p.text_size; i++) {
        ram[p.text_address + i] ^= 1;
        assert(!pgraph_sun_validate(&p, read_ram, NULL));
        ram[p.text_address + i] ^= 1;
    }
    static const uint32_t fields[] = {
        0x10000, 0x10104, 0x10108, 0x1010c, 0x10114, 0x10118,
        0x1011c, 0x10120, 0x10200, 0x10208, 0x102ac,
        0x10400, 0x10404, 0x10408, 0x10410,
    };
    for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        uint32_t old = pgraph_sun_u32(ram + fields[i]);
        put(fields[i], UINT32_MAX);
        assert(!pgraph_sun_validate(&p, read_ram, NULL));
        put(fields[i], old);
    }
    /* Writable code, missing code, duplicate sections and wrapped ranges. */
    put(0x10400, 0x17); assert(!pgraph_sun_validate(&p, read_ram, NULL));
    p = fixture(); put(0x10400, 0x12);
    assert(!pgraph_sun_validate(&p, read_ram, NULL));
    p = fixture(); put(0x1011c, 2);
    memcpy(ram + 0x10438, ram + 0x10400, 56);
    assert(!pgraph_sun_validate(&p, read_ram, NULL));
    p = fixture(); p.text_address = UINT32_MAX - 1;
    assert(!pgraph_sun_validate(&p, read_ram, NULL));
    p = fixture(); p.text_size = UINT32_MAX;
    assert(!pgraph_sun_validate(&p, read_ram, NULL));
    p = fixture();
    assert(!pgraph_sun_validate(NULL, read_ram, NULL));
    assert(!pgraph_sun_validate(&p, NULL, NULL));
    assert(!pgraph_sun_validate(&pgraph_sun_doaxvb_us, read_ram, NULL));
    reads = 0;
    assert(!pgraph_sun_select(false, pgraph_sun_doaxvb_us.title_id, read_ram, NULL));
    assert(!pgraph_sun_select(true, 0, read_ram, NULL));
    assert(reads == 0);
    p = fixture();
    p.transform_address = 0x15000; p.transform_size = 256;
    struct sha256_state hash;
    sha256_stream_init(&hash, 0);
    sha256_stream_update(&hash, ram + p.transform_address, p.transform_size);
    sha256_stream_final(&hash, p.transform_sha256);
    assert(pgraph_sun_validate(&p, read_ram, NULL));
    for (unsigned i = 0; i < p.transform_size; i++) {
        ram[p.transform_address + i] ^= 1;
        assert(!pgraph_sun_validate(&p, read_ram, NULL));
        ram[p.transform_address + i] ^= 1;
    }
    reads = 0;
    assert(pgraph_sun_validate(&p, read_ram, NULL));
    unsigned transform_read = reads;
    reads = 0; fail_at = transform_read;
    assert(!pgraph_sun_validate(&p, read_ram, NULL));
    fail_at = 0;
    p.transform_address = UINT32_MAX;
    assert(!pgraph_sun_validate(&p, read_ram, NULL));
    puts("Sun profile: 8193 content mutations, malformed headers and read failures passed");
}

static void put_float(uint32_t address, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put(address, bits);
}

static void scene(void)
{
    PgraphSunProfile profile = fixture();
    profile.device_address = 0x14000; profile.sun_address = 0x14010;
    put(profile.device_address, 0x18000);
    for (unsigned i = 0; i < 16; i++) {
        put_float(0x18810 + i * 4, i % 5 == 0 ? 1 : 0);
        put_float(0x18850 + i * 4, i % 5 == 0 ? 1 : 0);
    }
    put_float(profile.sun_address + 4, 3);
    PgraphSunScene out;
    assert(pgraph_sun_read_scene(&profile, read_ram, NULL, &out));
    assert(out.light_travel[1] == -1);
    unsigned complete_reads = reads;
    for (unsigned i = 0; i < 16; i++) {
        assert(out.inverse_view_projection[i] == (i % 5 == 0 ? 1 : 0));
    }
    /* Own scalar values: later guest writes cannot change this scene. */
    put_float(0x18810, 2);
    assert(out.view[0] == 1);
    assert(pgraph_sun_read_scene(&profile, read_ram, NULL, &out));
    assert(out.inverse_view_projection[0] == 0.5f);
    for (unsigned i = 1; i <= complete_reads; i++) {
        reads = 0; fail_at = i;
        memset(&out, 0x55, sizeof(out));
        assert(!pgraph_sun_read_scene(&profile, read_ram, NULL, &out));
        PgraphSunScene zero = { 0 };
        assert(memcmp(&zero, &out, sizeof(out)) == 0);
    }
    fail_at = 0;
    const float bad[] = {0, -1, NAN, INFINITY, -INFINITY};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        put_float(profile.sun_address + 4, bad[i]);
        assert(!pgraph_sun_read_scene(&profile, read_ram, NULL, &out));
    }
    put_float(profile.sun_address + 4, 3);
    const uint32_t pointers[] = {0, 1, 0xfffffffcu, 0x40000};
    for (unsigned i = 0; i < sizeof(pointers) / sizeof(pointers[0]); i++) {
        put(profile.device_address, pointers[i]);
        assert(!pgraph_sun_read_scene(&profile, read_ram, NULL, &out));
    }
    put(profile.device_address, 0x18000);
    put_float(0x18810, NAN);
    assert(!pgraph_sun_read_scene(&profile, read_ram, NULL, &out));
    put_float(0x18810, 0);
    assert(!pgraph_sun_read_scene(&profile, read_ram, NULL, &out));
    /* Validate row-vector multiplication order using noncommuting scale and
     * translation, and invert pivot swaps with 256 signed scale choices. */
    for (unsigned test = 1; test <= 256; test++) {
        float m[16] = {0}, inverse[16];
        m[1] = test; m[4] = -2; m[10] = 3; m[12] = 5; m[13] = 7; m[15] = 1;
        assert(pgraph_sun_inverse(m, inverse));
        for (unsigned r = 0; r < 4; r++) for (unsigned c = 0; c < 4; c++) {
            double value = 0;
            for (unsigned k = 0; k < 4; k++) value += (double)m[r * 4 + k] * inverse[k * 4 + c];
            assert(fabs(value - (r == c ? 1 : 0)) < 1e-5);
        }
        for (unsigned i = 0; i < 16; i++) {
            put_float(0x18810 + i * 4, i % 5 == 0 ? (i == 0 ? 2 : 1) : 0);
            put_float(0x18850 + i * 4, i % 5 == 0 ? 1 : (i == 12 ? test : 0));
        }
        assert(pgraph_sun_read_scene(&profile, read_ram, NULL, &out));
        assert(out.inverse_view_projection[0] == 0.5f);
        assert(out.inverse_view_projection[12] == -(float)test * 0.5f);
    }
    puts("Sun scene: owned constants, invalid inputs, read failures and 256 matrix pairs passed");
}

static void fit(void)
{
    PgraphSunScene scene = {0};
    for (unsigned i = 0; i < 16; i += 5) scene.inverse_view_projection[i] = 1;
    scene.light_travel[1] = -1;
    float matrix[16];
    assert(pgraph_sun_fit(&scene, 4096, 64, 20, 120, matrix));
    assert(matrix[0] == 1.0f / 32 && matrix[9] == 1.0f / 32);
    assert(fabsf(matrix[6] + 1.0f / 120) < 1e-8f);
    assert(matrix[13] == -20.0f / 32 && matrix[14] == 0.5f && matrix[15] == 1);
    for (unsigned test = 0; test < 4096; test++) {
        scene.inverse_view_projection[12] = (float)test / 1024;
        scene.inverse_view_projection[13] = (float)test / 2048;
        scene.inverse_view_projection[14] = -(float)test / 4096;
        assert(pgraph_sun_fit(&scene, 4096, 64, 20, 120, matrix));
        double center[3] = {scene.inverse_view_projection[12],
                            scene.inverse_view_projection[13],
                            scene.inverse_view_projection[14] + 20};
        for (unsigned c = 0; c < 3; c++) {
            double projected = matrix[12 + c];
            for (unsigned r = 0; r < 3; r++) projected += center[r] * matrix[r * 4 + c];
            assert(fabs(projected - (c == 2 ? 0.5 : 0)) < 1.0 / 4096 + 1e-7);
        }
        /* The same texel cell gives exactly the same XY projection. */
        if ((test & 15) == 0) {
            float shifted[16]; scene.inverse_view_projection[12] += 1.0f / 4096;
            assert(pgraph_sun_fit(&scene, 4096, 64, 20, 120, shifted));
            assert(shifted[12] == matrix[12]);
            scene.inverse_view_projection[12] -= 1.0f / 4096;
        }
    }
    const float bad[] = {0, -1, NAN, INFINITY};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        assert(!pgraph_sun_fit(&scene, 4096, bad[i], 20, 120, matrix));
        assert(!pgraph_sun_fit(&scene, 4096, 64, 20, bad[i], matrix));
    }
    assert(!pgraph_sun_fit(&scene, 0, 64, 20, 120, matrix));
    scene.light_travel[1] = 1;
    assert(!pgraph_sun_fit(&scene, 4096, 64, 20, 120, matrix));
    float zero[16] = {0}; assert(!memcmp(matrix, zero, sizeof(matrix)));
    scene.light_travel[1] = -1; scene.inverse_view_projection[15] = 0;
    assert(!pgraph_sun_fit(&scene, 4096, 64, 20, 120, matrix));
    scene.inverse_view_projection[15] = 1;
    for (unsigned test = 0; test < 256; test++) {
        scene.light_travel[0] = test == 0 ? 1e-8f : (float)test / 128;
        scene.light_travel[1] = -1;
        scene.light_travel[2] = test == 0 ? 1e-8f : -(float)test / 256;
        assert(pgraph_sun_fit(&scene, 4096, 64, 20, 120, matrix));
        double axes[3][3];
        for (unsigned axis = 0; axis < 3; axis++) {
            for (unsigned r = 0; r < 3; r++) axes[axis][r] = matrix[r * 4 + axis] * (axis == 2 ? 120 : 32);
            double length = 0;
            for (unsigned r = 0; r < 3; r++) length += axes[axis][r] * axes[axis][r];
            assert(fabs(length - 1) < 1e-6);
        }
        for (unsigned a = 0; a < 3; a++) for (unsigned b = a + 1; b < 3; b++) {
            double dot = 0;
            for (unsigned r = 0; r < 3; r++) dot += axes[a][r] * axes[b][r];
            assert(fabs(dot) < 1e-6);
        }
    }
    puts("Sun fit: 4096 camera translations, texel stability and invalid-fit rejection passed");
}

typedef struct Image { uint8_t *data; size_t size; } Image;
static bool read_xbe(void *opaque, uint32_t address, void *dst, size_t size)
{
    Image *image = opaque;
    uint8_t *data = image->data;
    uint32_t base = pgraph_sun_u32(data + 0x104);
    uint32_t headers = pgraph_sun_u32(data + 0x108);
    if (address >= base && address - base <= headers &&
        size <= headers - (address - base)) {
        memcpy(dst, data + address - base, size);
        return true;
    }
    uint32_t table = pgraph_sun_u32(data + 0x120) - base;
    uint32_t count = pgraph_sun_u32(data + 0x11c);
    for (uint32_t i = 0; i < count; i++) {
        uint8_t *s = data + table + i * 56;
        uint32_t va = pgraph_sun_u32(s + 4), raw = pgraph_sun_u32(s + 12);
        uint32_t length = pgraph_sun_u32(s + 16);
        if (address >= va && address - va <= length &&
            size <= length - (address - va)) {
            if (raw > image->size || length > image->size - raw) return false;
            memcpy(dst, data + raw + address - va, size);
            return true;
        }
    }
    return false;
}

static void retail(const char *path)
{
    FILE *file = fopen(path, "rb"); assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file); assert(size >= 0x178 && size < 64 * 1024 * 1024);
    rewind(file);
    Image image = {.size = (size_t)size, .data = malloc(size)}; assert(image.data);
    assert(fread(image.data, 1, size, file) == (size_t)size); fclose(file);
    /* Refuse malformed local inputs before the small section-mapping reader. */
    uint32_t base = pgraph_sun_u32(image.data + 0x104);
    uint32_t headers = pgraph_sun_u32(image.data + 0x108);
    uint32_t table = pgraph_sun_u32(image.data + 0x120);
    uint32_t count = pgraph_sun_u32(image.data + 0x11c);
    assert(base == 0x10000 && headers <= image.size && table >= base);
    assert(table - base <= headers && count <= (headers - (table - base)) / 56);
    assert(pgraph_sun_validate(&pgraph_sun_doaxvb_us, read_xbe, &image));
    /* Confirm a loaded-text mutation is rejected even with intact metadata. */
    for (uint32_t i = 0; i < count; i++) {
        uint8_t *s = image.data + table - base + i * 56;
        if (pgraph_sun_u32(s + 4) == pgraph_sun_doaxvb_us.text_address) {
            image.data[pgraph_sun_u32(s + 12)] ^= 1;
            break;
        }
    }
    assert(!pgraph_sun_validate(&pgraph_sun_doaxvb_us, read_xbe, &image));
    for (uint32_t i = 0; i < count; i++) {
        uint8_t *s = image.data + table - base + i * 56;
        uint32_t va = pgraph_sun_u32(s + 4), raw = pgraph_sun_u32(s + 12);
        if (va == pgraph_sun_doaxvb_us.text_address) image.data[raw] ^= 1;
    }
    assert(pgraph_sun_validate(&pgraph_sun_doaxvb_us, read_xbe, &image));
    for (uint32_t i = 0; i < count; i++) {
        uint8_t *s = image.data + table - base + i * 56;
        uint32_t va = pgraph_sun_u32(s + 4), raw = pgraph_sun_u32(s + 12);
        uint32_t length = pgraph_sun_u32(s + 16);
        if (va <= pgraph_sun_doaxvb_us.transform_address &&
            pgraph_sun_doaxvb_us.transform_address - va < length) {
            image.data[raw + pgraph_sun_doaxvb_us.transform_address - va] ^= 1;
            break;
        }
    }
    assert(!pgraph_sun_validate(&pgraph_sun_doaxvb_us, read_xbe, &image));
    free(image.data);
    puts("Local XBE: supported code accepted; modified game and D3D code rejected");
}

typedef struct Snapshot { Image ram; uint32_t cr3; } Snapshot;
static bool snapshot_u32(const Snapshot *s, uint32_t offset, uint32_t *out)
{
    if (offset > s->ram.size || s->ram.size - offset < 4) return false;
    *out = pgraph_sun_u32(s->ram.data + offset); return true;
}
static bool snapshot_physical(const Snapshot *s, uint32_t address, uint32_t *physical)
{
    uint32_t pde,pte;
    if (!snapshot_u32(s, (s->cr3 & 0xfffff000u) + (address >> 22) * 4, &pde) || !(pde & 1)) return false;
    if (pde & 128) *physical = (pde & 0xffc00000u) + (address & 0x3fffff);
    else {
        if (!snapshot_u32(s, (pde & 0xfffff000u) + ((address >> 12) & 1023) * 4, &pte) || !(pte & 1)) return false;
        *physical = (pte & 0xfffff000u) + (address & 4095);
    }
    return *physical < s->ram.size;
}
static bool read_snapshot(void *opaque, uint32_t address, void *destination, size_t length)
{
    Snapshot *s = opaque;
    if (length > (uint64_t)UINT32_MAX + 1 - address) return false;
    uint8_t *out = destination;
    while (length) {
        uint32_t physical;
        size_t size = 4096 - (address & 4095);
        if (size > length) size = length;
        if (!snapshot_physical(s,address,&physical) || size > s->ram.size - physical) return false;
        memcpy(out,s->ram.data + physical,size);
        out += size; address += size; length -= size;
    }
    return true;
}
static void snapshot(const char *path, uint32_t cr3)
{
    FILE *file = fopen(path,"rb"); assert(file);
    assert(!fseek(file,0,SEEK_END)); long size = ftell(file); rewind(file);
    assert(size == 64 * 1024 * 1024);
    Snapshot s = {.ram = {.size = (size_t)size, .data = malloc(size)}, .cr3 = cr3};
    assert(s.ram.data && fread(s.ram.data,1,size,file) == (size_t)size); fclose(file);
    const PgraphSunProfile *profile = pgraph_sun_select(true,0x54430007,read_snapshot,&s);
    assert(profile == &pgraph_sun_doaxvb_us);
    assert(pgraph_sun_validate_draw_seams(read_snapshot,&s));
    for (unsigned window=0;window<2;window++) {
        uint32_t start=window?0x1e78b0:0x1e7750;
        for (unsigned byte=0;byte<256;byte++) {
            uint32_t address;
            assert(snapshot_physical(&s,start+byte,&address));
            s.ram.data[address]^=1;
            assert(!pgraph_sun_validate_draw_seams(read_snapshot,&s));
            if (!byte) assert(!pgraph_sun_select(true,0x54430007,read_snapshot,&s));
            s.ram.data[address]^=1;
        }
    }
    PgraphSunScene scene;
    assert(pgraph_sun_read_scene(profile,read_snapshot,&s,&scene));
    float fit[16]; assert(pgraph_sun_fit(&scene,4096,64,20,120,fit));
    PgraphSunScene owned = scene;
    uint8_t pointer[4]; assert(read_snapshot(&s,profile->device_address,pointer,4));
    uint32_t physical;
    assert(snapshot_physical(&s,pgraph_sun_u32(pointer)+0x810,&physical));
    uint8_t original[4]; memcpy(original,s.ram.data+physical,4);
    s.ram.data[physical]=0; s.ram.data[physical+1]=0;
    s.ram.data[physical+2]=0xc0; s.ram.data[physical+3]=0x7f;
    assert(!memcmp(&scene,&owned,sizeof(scene)));
    assert(!pgraph_sun_read_scene(profile,read_snapshot,&s,&scene));
    PgraphSunScene zero={0}; assert(!memcmp(&scene,&zero,sizeof(scene)));
    memcpy(s.ram.data+physical,original,4);
    assert(pgraph_sun_read_scene(profile,read_snapshot,&s,&scene));
    assert(snapshot_physical(&s,0x10114,&physical));
    s.ram.data[physical]^=1;
    assert(!pgraph_sun_select(true,0x54430007,read_snapshot,&s));
    s.ram.data[physical]^=1;
    assert(pgraph_sun_select(true,0x54430007,read_snapshot,&s));
    free(s.ram.data);
    puts("Local snapshot: virtual build, 512 draw-seam mutations rejected, live camera/sun, fitted map, owned constants and incompatible timestamp rejection passed");
}

int main(int argc, char **argv)
{
    if (argc==4 && !strcmp(argv[1],"--ram")) {
        snapshot(argv[2],(uint32_t)strtoul(argv[3],NULL,0)); return 0;
    }
    synthetic();
    scene();
    fit();
    if (argc == 2) retail(argv[1]);
    return 0;
}
