/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "opcode byte: line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
#define likely(c) (c)
#define unlikely(c) (c)
#define tcg_debug_assert(c) CHECK(c)
#define assert(c) CHECK(c)
#define TARGET_PAGE_SIZE 4096
#define TARGET_PAGE_MASK (~UINT64_C(4095))
#define QEMU_IS_ALIGNED(v, a) (!((v) & ((a) - 1)))
static unsigned atomic_reads;
#define retro_atomic_load_relaxed_u16(p) (atomic_reads++, *(p))
#define retro_atomic_load_relaxed_int(p) (atomic_reads++, *(p))
#define stw_he_p(p, v) do { uint16_t t_ = (v); memcpy(p, &t_, 2); } while (0)
#define stl_he_p(p, v) do { uint32_t t_ = (v); memcpy(p, &t_, 4); } while (0)
typedef uint16_t retro_atomic_u16_t;
typedef uint32_t retro_atomic_int_t;
typedef uint64_t vaddr;
typedef uint64_t tb_page_addr_t;
typedef unsigned MemOpIdx;
typedef unsigned MemOp;
#define MO_UB 0
#define MO_UW 1
#define MO_UL 2
#define MO_BSWAP 4
static uint16_t lduw_he_p(const void *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static uint32_t ldl_he_p(const void *p) { uint32_t v; memcpy(&v, p, 4); return v; }
#define bswap16(v) __builtin_bswap16(v)
#define bswap32(v) __builtin_bswap32(v)
typedef struct { int unused; } CPUArchState;
typedef struct { tb_page_addr_t page[2]; } TranslationBlock;
typedef struct { TranslationBlock *tb; vaddr pc_first; void *host_addr[2]; unsigned max_insns, num_insns, code_mmuidx; } DisasContextBase;
static tb_page_addr_t tb_page_addr0(TranslationBlock *t) { return t->page[0]; }
static tb_page_addr_t tb_page_addr1(TranslationBlock *t) { return t->page[1]; }
static void tb_set_page_addr0(TranslationBlock *t, tb_page_addr_t p) { t->page[0] = p; }
static void tb_set_page_addr1(TranslationBlock *t, tb_page_addr_t p) { t->page[1] = p; }
static unsigned slow, maps, locks, unlocks, unlock_all, mmio, records;
static bool fault;
static jmp_buf escape;
static uint8_t *second;
static tb_page_addr_t second_phys;
static vaddr expected_pc;
static unsigned expected_size = 1;
static uint32_t mmio_value = 0xa5;
static tb_page_addr_t get_page_addr_code_hostp(CPUArchState *env, vaddr pc, void **host) { CHECK(env && pc == 0x101000); maps++; if (fault) longjmp(escape, 1); *host = second; return second_phys; }
static void tb_unlock_pages(TranslationBlock *tb) { CHECK(tb); unlock_all++; }
static void tb_unlock_page1(tb_page_addr_t p0, tb_page_addr_t p1) { CHECK(p0 == 0x200000 && p1 == 0x400000); unlocks++; }
static void tb_lock_page1(tb_page_addr_t p0, tb_page_addr_t p1) { CHECK(p0 == 0x200000 && p1 == second_phys); locks++; }
static MemOpIdx make_memop_idx(unsigned op, unsigned idx) { CHECK(op <= MO_UL && (1U << op) == expected_size && idx == 3); return idx; }
static uint8_t cpu_ldb_code_mmu(CPUArchState *env, vaddr pc, MemOpIdx oi, uintptr_t ra) { CHECK(env && pc == expected_pc && oi == 3 && !ra); mmio++; return 0xa5; }
static uint16_t cpu_ldw_code_mmu(CPUArchState *env, vaddr pc, MemOpIdx oi, uintptr_t ra) { CHECK(env && pc == expected_pc && oi == 3 && !ra); mmio++; return mmio_value; }
static uint32_t cpu_ldl_code_mmu(CPUArchState *env, vaddr pc, MemOpIdx oi, uintptr_t ra) { CHECK(env && pc == expected_pc && oi == 3 && !ra); mmio++; return mmio_value; }
static void record_save(DisasContextBase *db, vaddr pc, const void *p, int size) { CHECK(db && pc == expected_pc && size == (int)expected_size && !memcmp(p, &mmio_value, size)); records++; }
/* LOADER */
static bool translator_ld(CPUArchState *env, DisasContextBase *db, void *dest, vaddr pc, size_t len) { slow++; return translator_ld_impl(env, db, dest, pc, len); }
/* BYTE */
static void clear(void) { slow = maps = locks = unlocks = unlock_all = mmio = records = 0; fault = false; }
int main(void)
{
    CPUArchState env = {0};
    size_t page;
    uint8_t *base;
#ifdef _WIN32
    SYSTEM_INFO info; DWORD old;
    GetSystemInfo(&info); page = info.dwPageSize;
    CHECK(page == TARGET_PAGE_SIZE);
    base = VirtualAlloc(NULL, 4 * page, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    CHECK(base && VirtualProtect(base + page, 2 * page, PAGE_READWRITE, &old));
#else
    page = sysconf(_SC_PAGESIZE); CHECK(page == TARGET_PAGE_SIZE);
    base = mmap(NULL, 4 * page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(base != MAP_FAILED && !mprotect(base + page, 2 * page, PROT_READ | PROT_WRITE));
#endif
    uint8_t *first = base + 2 * page;
    second = base + page; /* Deliberately noncontiguous in guest order. */
    unsigned cases = 0;
    for (unsigned pattern = 0; pattern < 4; pattern++) {
        for (unsigned i = 0; i < page; i++) { first[i] = (uint8_t)(i * 37 + pattern); second[i] = (uint8_t)(i * 11 + pattern); }
        for (unsigned start = 0; start < page; start += 137) {
            TranslationBlock tb = { .page = {0x200000, (tb_page_addr_t)-1} };
            DisasContextBase db = { .tb = &tb, .pc_first = 0x100000 + start, .host_addr = {first + start, NULL}, .max_insns = 32, .num_insns = 2, .code_mmuidx = 3 };
            clear();
            for (unsigned i = start; i < page; i++) { CHECK(translator_ldub(&env, &db, 0x100000 + i) == first[i]); cases++; }
            CHECK(!slow && !maps && !locks && !mmio && !records);
        }
        for (unsigned old_page = 0; old_page < 3; old_page++) {
            TranslationBlock tb = { .page = {0x200000, old_page == 0 ? (tb_page_addr_t)-1 : old_page == 1 ? 0x300000 : 0x400000} };
            DisasContextBase db = { .tb = &tb, .pc_first = 0x100fff, .host_addr = {first + page - 1, NULL}, .max_insns = 32, .num_insns = 2, .code_mmuidx = 3 };
            clear(); second_phys = 0x300000;
            for (unsigned i = 0; i < page; i++) { CHECK(translator_ldub(&env, &db, 0x101000 + i) == second[i]); cases++; }
            CHECK(slow == page && maps == 1 && locks == (old_page != 1) && unlocks == (old_page == 2) && !mmio && !records);
        }
    }
    TranslationBlock tb = { .page = {(tb_page_addr_t)-1, (tb_page_addr_t)-1} };
    DisasContextBase db = { .tb = &tb, .pc_first = 0x100fff, .host_addr = {NULL, NULL}, .max_insns = 1, .num_insns = 1, .code_mmuidx = 3 };
    clear(); expected_pc = db.pc_first;
    CHECK(translator_ldub(&env, &db, expected_pc) == 0xa5);
    CHECK(slow == 1 && mmio == 1 && records == 1 && !maps);
    clear(); tb.page[0] = 0x200000; db.host_addr[0] = first + page - 1; db.max_insns = 32; db.num_insns = 2;
    second_phys = (tb_page_addr_t)-1; expected_pc = 0x101000;
    CHECK(translator_ldub(&env, &db, expected_pc) == 0xa5);
    CHECK(slow == 1 && maps == 1 && unlock_all == 1 && mmio == 1 && records == 1 && tb.page[0] == (tb_page_addr_t)-1 && db.max_insns == db.num_insns);
    clear(); tb.page[0] = 0x200000; db.host_addr[1] = NULL; fault = true;
    if (!setjmp(escape)) { translator_ldub(&env, &db, 0x101000); CHECK(false); }
    CHECK(slow == 1 && maps == 1 && !locks && !mmio && !records);
    unsigned wide_cases = 0;
    for (unsigned size = 2; size <= 4; size *= 2)
    for (unsigned endian = 0; endian <= MO_BSWAP; endian += MO_BSWAP)
    for (unsigned start = 0; start < 16; start++) {
        clear(); atomic_reads = 0; second_phys = 0x300000;
        tb.page[0] = 0x200000; tb.page[1] = (tb_page_addr_t)-1;
        db.pc_first = 0x100000 + start; db.host_addr[0] = first + start; db.host_addr[1] = NULL;
        for (unsigned i = start; i <= 2 * page - size; i++) {
            uint8_t bytes[4];
            for (unsigned j = 0; j < size; j++) bytes[j] = i + j < page ? first[i + j] : second[i + j - page];
            uint32_t expected = size == 2 ? lduw_he_p(bytes) : ldl_he_p(bytes);
            if (endian) expected = size == 2 ? bswap16(expected) : bswap32(expected);
            unsigned before = slow, atom_before = atomic_reads;
            uint32_t got = size == 2 ? translator_lduw_end(&env, &db, 0x100000 + i, endian) : translator_ldl_end(&env, &db, 0x100000 + i, endian);
            CHECK(got == expected);
            CHECK(slow == before + (i + size > page));
            if (i + size <= page) CHECK(atomic_reads == atom_before + !(i & (size - 1)));
            wide_cases++;
        }
        CHECK(maps == 1 && locks == 1 && !mmio && !records);
    }
    for (expected_size = 2; expected_size <= 4; expected_size *= 2)
    for (unsigned endian = 0; endian <= MO_BSWAP; endian += MO_BSWAP)
    for (unsigned second_mmio = 0; second_mmio < 2; second_mmio++) {
        clear(); db.max_insns = second_mmio ? 32 : 1;
        tb.page[0] = second_mmio ? 0x200000 : (tb_page_addr_t)-1;
        tb.page[1] = (tb_page_addr_t)-1; db.host_addr[1] = NULL;
        db.pc_first = 0x100fff; db.host_addr[0] = first + page - 1;
        expected_pc = 0x100fff; second_phys = (tb_page_addr_t)-1;
        mmio_value = expected_size == 2 ? 0x1234 : 0x12345678;
        uint32_t expected = endian ? expected_size == 2 ? bswap16(mmio_value) : bswap32(mmio_value) : mmio_value;
        uint32_t got = expected_size == 2 ? translator_lduw_end(&env, &db, expected_pc, endian) : translator_ldl_end(&env, &db, expected_pc, endian);
        CHECK(got == expected && slow == 1 && mmio == 1 && records == 1 && maps == second_mmio && unlock_all == second_mmio);
        wide_cases++;
    }
#ifdef _WIN32
    CHECK(VirtualFree(base, 0, MEM_RELEASE));
#else
    CHECK(!munmap(base, 4 * page));
#endif
    printf("opcode byte: %u first/second-page reads, guarded edge, remap/lock, MMIO record and fault paths PASS\n", cases);
    printf("wide fetch: %u endian/alignment/page-crossing/MMIO cases; first-page atomic reads preserved PASS\n", wide_cases);
}
