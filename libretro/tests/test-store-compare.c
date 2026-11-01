/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "store compare: line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
#define MAKE_64BIT_MASK(shift, length) (((~0ULL) >> (64 - (length))) << (shift))
typedef uintptr_t ram_addr_t;
static unsigned maps, checks;
static void *qemu_map_ram_ptr(void *ignored, ram_addr_t addr)
{
    (void)ignored;
    CHECK(addr);
    maps++;
    return (void *)addr;
}
/* PRODUCTION */

static void test(const uint8_t *p, unsigned size)
{
    uint64_t current = 0;
    memcpy(&current, p, size);
    for (unsigned bit = 0; bit <= 64; bit++) {
        uint64_t value = current ^ (bit < 64 ? UINT64_C(1) << bit : 0);
        uint64_t truncated = value;
        if (size < 8) {
            truncated &= (UINT64_C(1) << (size * 8)) - 1;
        }
        for (unsigned known = 0; known <= 16; known++) {
            unsigned before = maps;
            bool expected = known != size || current != truncated;
            CHECK(store_changes_memory((uintptr_t)p, size, value, known) == expected);
            CHECK(maps == before + (known == size));
            checks++;
        }
    }
}

int main(void)
{
    size_t page;
    uint8_t *base;
#ifdef _WIN32
    SYSTEM_INFO info;
    DWORD old;
    GetSystemInfo(&info);
    page = info.dwPageSize;
    base = VirtualAlloc(NULL, 3 * page, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    CHECK(base && VirtualProtect(base + page, page, PAGE_READWRITE, &old));
#else
    page = sysconf(_SC_PAGESIZE);
    base = mmap(NULL, 3 * page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(base != MAP_FAILED);
    CHECK(!mprotect(base + page, page, PROT_READ | PROT_WRITE));
#endif
    uint8_t *data = base + page;
    for (unsigned pattern = 0; pattern < 4; pattern++) {
        for (size_t i = 0; i < page; i++) {
            data[i] = pattern == 0 ? 0 : pattern == 1 ? 255 :
                      pattern == 2 ? (uint8_t)(i * 37) : (uint8_t)(i ^ (i >> 3));
        }
        for (unsigned size = 1; size <= 8; size++) {
            for (unsigned offset = 0; offset < 32; offset++) {
                test(data + offset, size);
                test(data + page - size - offset, size);
            }
        }
    }
    /* Unknown/wide writes must not map or read RAM at all. */
    unsigned before = maps;
    for (unsigned size = 1; size <= 32; size++) {
        CHECK(store_changes_memory(0, size, 0, 0));
        if (size > 8) {
            CHECK(store_changes_memory(0, size, 0, size));
        }
    }
    CHECK(maps == before);
#ifdef _WIN32
    CHECK(VirtualFree(base, 0, MEM_RELEASE));
#else
    CHECK(!munmap(base, 3 * page));
#endif
    printf("store compare: %u value/width/alignment checks, guarded page edges and no-read paths PASS\n", checks);
}
