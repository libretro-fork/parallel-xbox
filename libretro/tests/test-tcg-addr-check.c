/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

typedef enum { TCG_TYPE_I32, TCG_TYPE_I64 } TCGType;
typedef struct { TCGType addr_type; } TCGContext;
static TCGContext context;
static unsigned fetches, assertions, failures;
static bool armed;
static jmp_buf mismatch;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "address check: %u: %s\n", __LINE__, #x); _Exit(1); } } while (0)
TCGContext *fixture_context(void);
void fixture_assert(bool value);
TCGContext *fixture_context(void)
{
    ++fetches;
    return &context;
}
void fixture_assert(bool value)
{
    ++assertions;
    if (!value) {
        CHECK(armed);
        ++failures;
        longjmp(mismatch, 1);
    }
}
#undef assert
#define assert(x) fixture_assert(!!(x))
#include "tcg/debug-assert.h"
#define tcg_ctx fixture_context()
/* PRODUCTION */
int main(void)
{
#if defined(CONFIG_DEBUG_TCG) || defined(QEMU_STATIC_ANALYSIS)
    const bool enabled = true;
#else
    const bool enabled = false;
#endif
    context.addr_type = TCG_TYPE_I32;
    tcg_debug_assert_addr_type(TCG_TYPE_I32);
    context.addr_type = TCG_TYPE_I64;
    tcg_debug_assert_addr_type(TCG_TYPE_I64);
    if (!setjmp(mismatch)) {
        armed = true;
        tcg_debug_assert_addr_type(TCG_TYPE_I32);
        armed = false;
        CHECK(!enabled);
    } else {
        armed = false;
        CHECK(enabled);
    }
    CHECK(fetches == (enabled ? 3u : 0u));
    CHECK(assertions == (enabled ? 3u : 0u));
    CHECK(failures == (enabled ? 1u : 0u));
    printf("TCG address checks: %s, TLS fetches=%u, mismatches caught=%u PASS\n",
           enabled ? "diagnostic" : "release", fetches, failures);
    return 0;
}
