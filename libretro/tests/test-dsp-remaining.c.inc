/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Independent mathematical oracles for the remaining DSP56300 families. */
static bool remaining_smoke;
static bool remaining_smoke_port(unsigned p) { return p == 0 || p == 31 || p == 32 || p == 63; }
static bool remaining_smoke_word(unsigned p) { return p == 0 || p == 2 || p == 3 || p == 4; }
static bool remaining_smoke_acc(unsigned p) { return p == 0 || p >= 3; }

static int64_t remaining_signed(uint64_t value, unsigned bits)
{
    uint64_t sign = UINT64_C(1) << (bits - 1);
    return value & sign ? (int64_t)value - (INT64_C(1) << bits) : (int64_t)value;
}

static uint64_t remaining_acc(dsp_core_t *c, unsigned d)
{
    return c->registers[DSP_REG_A0 + d] | ((uint64_t)c->registers[DSP_REG_A1 + d] << 24) |
           ((uint64_t)c->registers[DSP_REG_A2 + d] << 48);
}

static void remaining_set_acc(dsp_core_t *c, unsigned d, uint64_t value)
{
    c->registers[DSP_REG_A0 + d] = value & 0xffffff;
    c->registers[DSP_REG_A1 + d] = (value >> 24) & 0xffffff;
    c->registers[DSP_REG_A2 + d] = (value >> 48) & 255;
}

static uint32_t remaining_flags(uint32_t sr, uint64_t raw, bool overflow, unsigned mode)
{
    uint32_t expected = (sr & 0x1ffff & ~0x3e) | (overflow << DSP_SR_V) | (overflow << DSP_SR_L);
    int64_t signed_value = remaining_signed(raw, 56);
    if (mode != 3) {
        unsigned bit = mode == 1 ? 48 : mode == 2 ? 46 : 47;
        int64_t limit = INT64_C(1) << bit;
        unsigned top = (raw >> (bit - 1)) & 3;
        expected |= ((signed_value < -limit || signed_value >= limit) << DSP_SR_E) |
                    ((top == 0 || top == 3) << DSP_SR_U) |
                    ((raw == 0) << DSP_SR_Z) | ((signed_value < 0) << DSP_SR_N);
    }
    return expected;
}

static void remaining_check_result(Machine *m, unsigned engine, uint32_t op, uint32_t regs[DSP_REG_MAX],
                                   uint32_t stack[2][16], uint64_t result, uint32_t status, unsigned d)
{
    dsp_core_t *c = m->dsp->backend;
    regs[DSP_REG_A0 + d] = result & 0xffffff;
    regs[DSP_REG_A1 + d] = (result >> 24) & 0xffffff;
    regs[DSP_REG_A2 + d] = result >> 48;
    regs[DSP_REG_SR] = status;
    if (!engine) { m->dsp->ops->step(m->dsp); c->cycle_count += c->instr_cycle; }
    else m->dsp->ops->run(m->dsp, 1);
    if (c->pc != 1 || c->cycle_count != 2 || memcmp(regs, c->registers, sizeof(c->registers)) ||
        memcmp(stack, c->stack, sizeof(c->stack))) {
        fprintf(stderr, "remaining arithmetic engine=%u op=%06x PC=%x result=%llx/%llx SR=%x/%x\n",
                engine, op, c->pc, (unsigned long long)remaining_acc(c, d), (unsigned long long)result,
                c->registers[DSP_REG_SR], status);
        exit(1);
    }
}

static void check_remaining_arithmetic(Machine *a, Machine *b, unsigned mode)
{
    const uint64_t mask = UINT64_C(0xffffffffffffff);
    static const uint32_t words[] = {0, 1, 0x7fffff, 0x800000, 0xffffff, 0x400000, 0xc00000};
    static const uint64_t accum[] = {0, 1, 0x800000, UINT64_C(0x7fffffffffffff),
                                     UINT64_C(0x80000000000000), UINT64_C(0xfffffffffffffe)};
    static const unsigned shifted_sources[] = {DSP_REG_Y1, DSP_REG_X0, DSP_REG_Y0, DSP_REG_X1};
    for (unsigned kind = 0; kind < 4; kind++) {
        bool accumulate = kind >= 2, rounded = kind & 1;
        for (unsigned n = 0; n < 32; n++) for (unsigned q = 0; q < 4; q++)
        for (unsigned d = 0; d < 2; d++) for (unsigned minus = 0; minus < 2; minus++)
        for (unsigned v = 0; v < ARRAY_SIZE(words); v++)
        for (unsigned ai = 0; ai < (accumulate ? ARRAY_SIZE(accum) : 1); ai++)
        for (unsigned profile = 0; profile < 2; profile++) {
            if (remaining_smoke && ((n != 0 && n != 1 && n != 23 && n != 24 && n != 31) || !remaining_smoke_word(v) || !remaining_smoke_acc(ai) || profile)) continue;
            uint32_t op = 0x0100c0 | (n << 8) | (q << 4) | (d << 3) | (minus << 2) | kind;
            int64_t product = n >= 1 && n <= 23 ? remaining_signed(words[v], 24) * (INT64_C(1) << (24 - n)) : 0;
            if (minus) product = -product;
            int64_t sum = product + (accumulate ? remaining_signed(accum[ai], 56) : 0);
            bool overflow = sum < -(INT64_C(1) << 55) || sum >= (INT64_C(1) << 55);
            uint64_t result = (uint64_t)sum & mask;
            if (rounded) {
                int64_t value = sum;
                int64_t unit = INT64_C(1) << (mode & 1 ? 25 : mode == 2 ? 23 : 24);
                int64_t quotient = value / unit, remainder = value % unit;
                if (remainder < 0) { quotient--; remainder += unit; }
                if (remainder > unit / 2 || (remainder == unit / 2 && quotient % 2)) quotient++;
                result = (uint64_t)(quotient * unit) & mask;
                overflow = quotient * unit < -(INT64_C(1) << 55) || quotient * unit >= (INT64_C(1) << 55);
            }
            Machine *machines[] = {a, b};
            dspjit_test_reset_fallback_count();
            for (unsigned engine = 0; engine < 2; engine++) {
                Machine *m = machines[engine]; prepare(m, op, 0x30, 1, mode, false);
                dsp_core_t *c = m->dsp->backend;
                remaining_set_acc(c, d, accum[ai]); c->registers[shifted_sources[q]] = words[v];
                c->registers[DSP_REG_SR] = (mode << DSP_SR_S0) | 0x100be | (profile ? 0x41 : 0);
                uint32_t regs[DSP_REG_MAX], stack[2][16];
                memcpy(regs, c->registers, sizeof(regs)); memcpy(stack, c->stack, sizeof(stack));
                remaining_check_result(m, engine, op, regs, stack, result, remaining_flags(c->registers[DSP_REG_SR], result, overflow, mode), d);
            }
            if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
            cases++;
        }
    }
    static const unsigned first[] = {4,6,5,7,4,6,5,7,5,7,4,6,7,4,6,5};
    static const unsigned second[] = {4,6,4,6,7,4,6,5,5,7,5,7,4,6,5,7};
    for (unsigned kind = 0; kind < 3; kind++) for (unsigned sign = 0; sign < (kind == 2 ? 4 : 2); sign++)
    for (unsigned q = 0; q < 16; q++) for (unsigned d = 0; d < 2; d++)
    for (unsigned minus = 0; minus < 2; minus++) for (unsigned x = 0; x < ARRAY_SIZE(words); x++)
    for (unsigned y = 0; y < ARRAY_SIZE(words); y++)
    for (unsigned ai = 0; ai < (kind ? ARRAY_SIZE(accum) : 1); ai++)
    for (unsigned profile = 0; profile < 2; profile++) {
        if (remaining_smoke && ((q != 0 && q != 1 && q != 4 && q != 5 && q != 8 && q != 12 && q != 15) || !remaining_smoke_word(x) || !remaining_smoke_word(y) || !remaining_smoke_acc(ai) || profile)) continue;
        uint32_t op = (kind == 0 ? 0x012780 : kind == 1 ? 0x012680 : 0x012480) |
                      (kind == 2 ? ((sign & 2) << 7) | ((sign & 1) << 6) : sign << 6) |
                      (d << 5) | (minus << 4) | q;
        unsigned sign_mode = kind == 2 ? sign : sign ? 3 : 2;
        Machine *machines[] = {a, b};
        dspjit_test_reset_fallback_count();
        for (unsigned engine = 0; engine < 2; engine++) {
            Machine *m = machines[engine]; prepare(m, op, 0x30, 1, mode, false);
            dsp_core_t *c = m->dsp->backend;
            remaining_set_acc(c, d, accum[ai]);
            c->registers[first[q]] = words[x]; c->registers[second[q]] = words[y];
            c->registers[DSP_REG_SR] = (mode << DSP_SR_S0) | 0x100be | (profile ? 0x41 : 0);
            int64_t sx = c->registers[first[q]], sy = c->registers[second[q]];
            if (!(sign_mode & 1)) sx = remaining_signed(sx, 24);
            if (!(sign_mode & 2)) sy = remaining_signed(sy, 24);
            int64_t product = sx * sy * 2; if (minus) product = -product;
            int64_t initial = kind ? remaining_signed(accum[ai], 56) : 0;
            if (kind == 2) {
                int64_t divisor = INT64_C(1) << 24;
                initial = initial >= 0 ? initial / divisor : -((-initial + divisor - 1) / divisor);
            }
            int64_t sum = initial + product;
            bool overflow = sum < -(INT64_C(1) << 55) || sum >= (INT64_C(1) << 55);
            uint64_t result = (uint64_t)sum & mask;
            uint32_t regs[DSP_REG_MAX], stack[2][16];
            memcpy(regs, c->registers, sizeof(regs)); memcpy(stack, c->stack, sizeof(stack));
            remaining_check_result(m, engine, op, regs, stack, result, remaining_flags(c->registers[DSP_REG_SR], result, overflow, mode), d);
        }
        if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
        cases++;
    }
}

static void check_remaining_normf(Machine *a, Machine *b, unsigned mode)
{
    static const unsigned sources[] = {DSP_REG_X0, DSP_REG_Y0, DSP_REG_X1, DSP_REG_Y1, DSP_REG_A1, DSP_REG_B1};
    static const uint64_t accum[] = {0, 1, UINT64_C(0x7fffffffffffff), UINT64_C(0x80000000000000),
        UINT64_C(0xffffffffffffff), UINT64_C(0x400000800000), UINT64_C(0x1800000), UINT64_C(0xabcdef12345678)};
    const uint64_t mask = UINT64_C(0xffffffffffffff);
    for (unsigned source = 0; source < 6; source++) for (unsigned d = 0; d < 2; d++)
    for (unsigned left = 0; left < 2; left++) for (unsigned n = 0; n < 64; n++)
    for (unsigned v = 0; v < ARRAY_SIZE(accum); v++) for (unsigned profile = 0; profile < 2; profile++) {
        if (remaining_smoke && ((n != 0 && n != 1 && n != 23 && n != 24 && n != 31 && n != 32 && n != 55 && n != 56 && n != 63) || (v != 0 && v != 2 && v != 3 && v != 4) || profile)) continue;
        uint32_t op = 0x0c1e20 | ((source + 2) << 1) | d;
        Machine *machines[] = {a, b}; dspjit_test_reset_fallback_count();
        for (unsigned engine = 0; engine < 2; engine++) {
            Machine *m = machines[engine]; prepare(m, op, 0x30, 1, mode, false);
            dsp_core_t *c = m->dsp->backend;
            remaining_set_acc(c, d, accum[v]);
            c->registers[sources[source]] = (left ? -n : n) & 0xffffff;
            c->registers[DSP_REG_SR] = (mode << DSP_SR_S0) | 0x100be | (profile ? 0x41 : 0);
            uint64_t result = remaining_acc(c, d); bool overflow = false;
            for (unsigned bit = 0; bit < n; bit++) {
                bool sign = (result >> 55) != 0;
                if (left) {
                    result = (result << 1) & mask;
                    overflow |= sign != ((result >> 55) != 0);
                } else result = (result >> 1) | ((uint64_t)sign << 55);
            }
            uint32_t regs[DSP_REG_MAX], stack[2][16];
            memcpy(regs, c->registers, sizeof(regs)); memcpy(stack, c->stack, sizeof(stack));
            remaining_check_result(m, engine, op, regs, stack, result,
                                   remaining_flags(c->registers[DSP_REG_SR], result, overflow, mode), d);
        }
        if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
        cases++;
    }
}

static void check_remaining_vsl(Machine *a, Machine *b, unsigned mode)
{
    static const uint64_t values[] = {0, 1, UINT64_C(0xffffffffffffff), UINT64_C(0x800000800000), UINT64_C(0x123456abcdef)};
    for (unsigned ea = 0; ea < 64; ea++) {
        unsigned am = ea >> 3, r = ea & 7;
        if (am == 6 && r != 0) continue;
        for (unsigned d = 0; d < 2; d++) for (unsigned insert = 0; insert < 2; insert++)
        for (unsigned v = 0; v < ARRAY_SIZE(values); v++) for (unsigned profile = 0; profile < 2; profile++) {
            uint32_t op = 0x0ac0c0 | (d << 16) | (ea << 8) | (insert << 4);
            uint32_t addr = am == 5 ? 0x43 : 0x40;
            uint32_t next = am == 0 ? 0x3d : am == 1 ? 0x43 : am == 2 ? 0x3f : am == 3 ? 0x41 : 0x40;
            Machine *machines[] = {a, b}; dspjit_test_reset_fallback_count();
            for (unsigned engine = 0; engine < 2; engine++) {
                Machine *m = machines[engine]; prepare(m, op, 0x40, 1, mode, false);
                dsp_core_t *c = m->dsp->backend;
                remaining_set_acc(c, d, values[v]);
                c->registers[DSP_REG_R0 + r] = am == 7 ? 0x41 : 0x40;
                c->registers[DSP_REG_N0 + r] = 3; c->registers[DSP_REG_M0 + r] = 0xffff;
                c->registers[DSP_REG_SR] = (mode << DSP_SR_S0) | (profile ? 0x100ff : 0);
                uint32_t regs[DSP_REG_MAX], stack[2][16], x[DSP_XRAM_SIZE], y[DSP_YRAM_SIZE];
                memcpy(regs, c->registers, sizeof(regs)); memcpy(stack, c->stack, sizeof(stack));
                memcpy(x, c->xram, sizeof(x)); memcpy(y, c->yram, sizeof(y));
                regs[DSP_REG_R0 + r] = next;
                x[addr] = (values[v] >> 24) & 0xffffff;
                y[addr] = ((values[v] << 1) | insert) & 0xffffff;
                if (!engine) { m->dsp->ops->step(m->dsp); c->cycle_count += c->instr_cycle; }
                else m->dsp->ops->run(m->dsp, 1);
                if (c->pc != (am == 6 ? 2 : 1) || c->cycle_count != (am >= 5 ? 4 : 2) ||
                    memcmp(regs, c->registers, sizeof(regs)) || memcmp(stack, c->stack, sizeof(stack)) ||
                    memcmp(x, c->xram, sizeof(x)) || memcmp(y, c->yram, sizeof(y))) {
                    fprintf(stderr, "VSL engine=%u ea=%u d=%u i=%u v=%u PC=%x cycles=%u\n", engine, ea, d, insert, v, c->pc, c->cycle_count);
                    exit(1);
                }
            }
            if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
            cases++;
        }
    }
}

static void check_remaining_y_register(Machine *a, Machine *b, unsigned mode)
{
    static const uint32_t values[] = {0, 0xffffff, 0xaaaaaa, 0x555555};
    static const uint32_t status_inputs[] = {0, 0xff, 0x1234, 0x3100bf};
    static const int64_t accum[] = {-(INT64_C(1) << 55), -(INT64_C(1) << 47), -1, 0,
                                  1, INT64_C(1) << 46, (INT64_C(1) << 47) - 1, (INT64_C(1) << 55) - 1};
    for (unsigned reg = DSP_REG_X0; reg <= DSP_REG_LC; reg++) {
        if (reg > DSP_REG_M7 && reg < DSP_REG_SR) continue;
        bool full_acc = reg == DSP_REG_A || reg == DSP_REG_B;
        for (unsigned port = 0; port < 64; port++) {
            if (remaining_smoke && !remaining_smoke_port(port)) continue;
            for (unsigned out = 0; out < 2; out++) {
                uint32_t op = 0x044020 | (out << 15) | (reg << 8) | (port & 31) | ((port & 32) << 1);
                for (unsigned v = 0; v < (full_acc && out ? ARRAY_SIZE(accum) : ARRAY_SIZE(values)); v++) {
                    for (unsigned profile = 0; profile < 2; profile++) {
                        uint32_t input = reg == DSP_REG_SR ? status_inputs[v] : reg == DSP_REG_SP ? (v & 1 ? 14 : 2) : values[v & 3];
                        Machine *machines[] = {a, b};
                        dspjit_test_reset_fallback_count();
                        for (unsigned engine = 0; engine < 2; engine++) {
                            Machine *m = machines[engine];
                            prepare(m, op, 0x30, 1, mode, false);
                            dsp_core_t *c = m->dsp->backend;
                            c->exception_debugging = false;
                            c->registers[DSP_REG_SR] = (mode << DSP_SR_S0) | (profile ? 0x3100bf : 0);
                            unsigned sp = reg == DSP_REG_SP && out && (v & 1) ? 14 : 2;
                            c->registers[DSP_REG_SP] = sp;
                            for (unsigned slot = 0; slot < 16; slot++) {
                                c->stack[0][slot] = slot ? 0x1100 + slot : 0;
                                c->stack[1][slot] = slot ? 0x2200 + slot : 0;
                            }
                            c->registers[DSP_REG_SSH] = c->stack[0][sp];
                            c->registers[DSP_REG_SSL] = c->stack[1][sp];
                            uint32_t written = 0, extra_flags = 0;
                            if (out && full_acc) {
                                int64_t raw = accum[v]; unsigned d = reg & 1;
                                c->registers[DSP_REG_A0 + d] = (uint64_t)raw & 0xffffff;
                                c->registers[DSP_REG_A1 + d] = ((uint64_t)raw >> 24) & 0xffffff;
                                c->registers[DSP_REG_A2 + d] = ((uint64_t)raw >> 48) & 255;
                                int64_t scaled = mode == 1 ? raw >> 25 : mode == 2 ? raw >> 23 : raw >> 24;
                                if (scaled > 0x7fffff) { written = 0x7fffff; extra_flags = 1u << DSP_SR_L; }
                                else if (scaled < -0x800000) { written = 0x800000; extra_flags = 1u << DSP_SR_L; }
                                else written = scaled & 0xffffff;
                            } else if (out) {
                                if (reg != DSP_REG_SR && reg != DSP_REG_SP) {
                                    uint32_t mask = reg == DSP_REG_A2 || reg == DSP_REG_B2 || reg == DSP_REG_OMR ? 255 :
                                                    reg >= DSP_REG_R0 ? 0xffff : 0xffffff;
                                    c->registers[reg] = values[v] & mask;
                                }
                                if (reg == DSP_REG_SSH) c->stack[0][sp] = c->registers[reg];
                                if (reg == DSP_REG_SSL) c->stack[1][sp] = c->registers[reg];
                                written = c->registers[reg];
                            }
                            uint32_t expected[DSP_REG_MAX], stack[2][16];
                            memcpy(expected, c->registers, sizeof(expected));
                            memcpy(stack, c->stack, sizeof(stack));
                            expected[DSP_REG_SR] |= extra_flags;
                            if (out && reg == DSP_REG_SSH) {
                                expected[DSP_REG_SP] = sp - 1;
                                expected[DSP_REG_SSH] = stack[0][sp - 1];
                                expected[DSP_REG_SSL] = stack[1][sp - 1];
                            } else if (!out) {
                                if (full_acc) {
                                    unsigned d = reg & 1;
                                    expected[DSP_REG_A0 + d] = 0;
                                    expected[DSP_REG_A1 + d] = input;
                                    expected[DSP_REG_A2 + d] = input & 0x800000 ? 255 : 0;
                                } else if (reg == DSP_REG_SR) expected[reg] = input & 0x1af7f;
                                else if (reg == DSP_REG_OMR) expected[reg] = input & 0xc7;
                                else if (reg == DSP_REG_SP) {
                                    expected[reg] = input;
                                    expected[DSP_REG_SSH] = stack[0][input];
                                    expected[DSP_REG_SSL] = stack[1][input];
                                } else if (reg == DSP_REG_SSH) {
                                    expected[DSP_REG_SP] = sp + 1;
                                    expected[reg] = stack[0][sp + 1] = input & 0xffff;
                                    expected[DSP_REG_SSL] = stack[1][sp + 1];
                                } else if (reg == DSP_REG_SSL) expected[reg] = stack[1][sp] = input & 0x1ffff;
                                else expected[reg] = input & ((reg == DSP_REG_A2 || reg == DSP_REG_B2 || reg == DSP_REG_OMR) ? 255 :
                                                              reg >= DSP_REG_R0 ? 0xffff : 0xffffff);
                            }
                            uint32_t (*saved_read)(dsp_core_t *, uint32_t) = c->read_y_peripheral;
                            void (*saved_write)(dsp_core_t *, uint32_t, uint32_t) = c->write_y_peripheral;
                            c->read_y_peripheral = qq_test_read; c->write_y_peripheral = qq_test_write;
                            qq_callback_value = input; qq_reads = qq_writes = 0;
                            qq_read_address = qq_write_address = 0; qq_written_value = UINT32_MAX;
                            if (!engine) { m->dsp->ops->step(m->dsp); c->cycle_count += c->instr_cycle; }
                            else m->dsp->ops->run(m->dsp, 1);
                            c->read_y_peripheral = saved_read; c->write_y_peripheral = saved_write;
                            if (c->pc != 1 || c->cycle_count != 4 || memcmp(expected, c->registers, sizeof(expected)) ||
                                memcmp(stack, c->stack, sizeof(stack)) || qq_reads != !out || qq_writes != out ||
                                (out ? qq_write_address != 0xffff80 + port || qq_written_value != written : qq_read_address != 0xffff80 + port)) {
                                fprintf(stderr, "Y:qq MOVEP engine=%u reg=%u port=%u out=%u v=%u profile=%u PC=%x SP=%x SR=%x/%x regval=%x/%x\n",
                                        engine, reg, port, out, v, profile, c->pc, c->registers[DSP_REG_SP], c->registers[DSP_REG_SR], expected[DSP_REG_SR], c->registers[reg], expected[reg]);
                                exit(1);
                            }
                        }
                        if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
                        cases++;
                    }
                }
            }
        }
    }
    static const unsigned ports[] = {0, 1, 31, 32, 0x33, 63};
    static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000, 0x40000000};
    for (unsigned reg = DSP_REG_X0; reg <= DSP_REG_LC; reg++) {
        if (reg > DSP_REG_M7 && reg < DSP_REG_SR) continue;
        for (unsigned p = 0; p < ARRAY_SIZE(ports); p++) {
            for (unsigned out = 0; out < 2; out++) {
                for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                    for (unsigned seed = 0; seed < 2; seed++)
                        check(a, b, 0x044020 | (out << 15) | (reg << 8) | (ports[p] & 31) | ((ports[p] & 32) << 1),
                              0x30, 0x400000 | 0x10000 | 0x1000000 | contexts[ctx] | seed, mode, seed != 0);
                }
            }
        }
    }
}

static void check_remaining_y_program(Machine *a, Machine *b, unsigned mode)
{
    static const uint32_t values[] = {0, 0xffffff, 0xaaaaaa, 0x555555};
    for (unsigned ea = 0; ea < 64; ea++) {
        unsigned am = ea >> 3, n = ea & 7;
        if (am == 6 && n != 0) continue;
        for (unsigned port = 0; port < 64; port++) {
            if (remaining_smoke && !remaining_smoke_port(port)) continue;
            for (unsigned out = 0; out < 2; out++) {
                uint32_t op = 0x008040 | (out << 14) | (ea << 8) | port;
                for (unsigned v = 0; v < ARRAY_SIZE(values); v++) {
                    for (unsigned profile = 0; profile < 2; profile++) {
                        uint32_t address = am == 5 ? 0x43 : 0x40;
                        uint32_t next_r = am == 0 ? 0x3d : am == 1 ? 0x43 : am == 2 ? 0x3f : am == 3 ? 0x41 : 0x40;
                        Machine *machines[] = {a, b};
                        dspjit_test_reset_fallback_count();
                        for (unsigned engine = 0; engine < 2; engine++) {
                            Machine *m = machines[engine];
                            prepare(m, op, 0x40, 1, mode, false);
                            dsp_core_t *c = m->dsp->backend;
                            c->registers[DSP_REG_SR] = (mode << DSP_SR_S0) | (profile ? 0x3100bf : 0);
                            c->registers[DSP_REG_R0 + n] = am == 7 ? 0x41 : 0x40;
                            c->registers[DSP_REG_N0 + n] = 3;
                            c->registers[DSP_REG_M0 + n] = 0xffff;
                            uint32_t expected[DSP_REG_MAX], stack[2][16];
                            memcpy(expected, c->registers, sizeof(expected));
                            memcpy(stack, c->stack, sizeof(stack));
                            expected[DSP_REG_R0 + n] = next_r;
                            m->dsp->ops->write_memory(m->dsp, 'P', address, out ? values[v] : values[v] ^ 0xffffff);
                            uint32_t (*saved_read)(dsp_core_t *, uint32_t) = c->read_y_peripheral;
                            void (*saved_write)(dsp_core_t *, uint32_t, uint32_t) = c->write_y_peripheral;
                            c->read_y_peripheral = qq_test_read; c->write_y_peripheral = qq_test_write;
                            qq_callback_value = values[v]; qq_reads = qq_writes = 0;
                            qq_read_address = qq_write_address = 0; qq_written_value = UINT32_MAX;
                            if (!engine) { m->dsp->ops->step(m->dsp); c->cycle_count += c->instr_cycle; }
                            else { m->dsp->save_cycles = 0; m->dsp->ops->run(m->dsp, 1); }
                            c->read_y_peripheral = saved_read; c->write_y_peripheral = saved_write;
                            if (c->pc != (am == 6 ? 2 : 1) || c->cycle_count != (am >= 5 ? 8 : 6) ||
                                memcmp(expected, c->registers, sizeof(expected)) || memcmp(stack, c->stack, sizeof(stack)) ||
                                m->dsp->ops->read_memory(m->dsp, 'P', address) != values[v] ||
                                qq_reads != !out || qq_writes != out ||
                                (out ? qq_write_address != 0xffff80 + port || qq_written_value != values[v] : qq_read_address != 0xffff80 + port)) {
                                fprintf(stderr, "Y:qq P MOVEP engine=%u ea=%u port=%u out=%u v=%u PC=%x R=%x/%x cycles=%u\n",
                                        engine, ea, port, out, v, c->pc, c->registers[DSP_REG_R0 + n], next_r, (unsigned)c->cycle_count);
                                exit(1);
                            }
                        }
                        if (dspjit_test_fallback_count() || compare_machines(a, b)) { fprintf(stderr, "P MOVEP fallback/state mismatch cases=%u fallback=%u\n", cases, (unsigned)dspjit_test_fallback_count()); exit(1); }
                        cases++;
                    }
                }
            }
        }
    }
    /* Each run below is a separate single-instruction observation: discard
     * caller budget debt while preserving translated blocks between runs. */
    static const unsigned ports[] = {0, 31, 32, 63};
    for (unsigned absolute = 0; absolute < 2; absolute++) {
        for (unsigned self = 0; self < 2; self++) {
            for (unsigned p = 0; p < ARRAY_SIZE(ports); p++) {
                uint32_t target = self ? 0 : 0x40;
                uint32_t op = 0x008040 | ((absolute ? 48 : 32) << 8) | ports[p];
                Machine *machines[] = {a, b};
                dspjit_test_reset_fallback_count();
                for (unsigned engine = 0; engine < 2; engine++) {
                    Machine *m = machines[engine];
                    prepare(m, op, target, 1, mode, false);
                    dsp_core_t *c = m->dsp->backend;
                    c->registers[DSP_REG_R0] = target;
                    if (!self) {
                        m->dsp->ops->write_memory(m->dsp, 'P', target, 0x200013);
                        c->pc = target;
                        if (!engine) { m->dsp->ops->step(m->dsp); c->cycle_count += c->instr_cycle; }
                        else { m->dsp->save_cycles = 0; m->dsp->ops->run(m->dsp, 1); }
                    }
                    c->pc = 0; c->registers[DSP_REG_SR] = mode << DSP_SR_S0;
                    c->registers[DSP_REG_A0] = 0x123456; c->registers[DSP_REG_A1] = 0x234567; c->registers[DSP_REG_A2] = 0;
                    c->registers[DSP_REG_B0] = 0x345678; c->registers[DSP_REG_B1] = 0x456789; c->registers[DSP_REG_B2] = 0;
                    uint32_t (*saved_read)(dsp_core_t *, uint32_t) = c->read_y_peripheral;
                    void (*saved_write)(dsp_core_t *, uint32_t, uint32_t) = c->write_y_peripheral;
                    c->read_y_peripheral = qq_test_read; c->write_y_peripheral = qq_test_write;
                    qq_callback_value = 0x20001b; qq_reads = qq_writes = 0; qq_read_address = 0;
                    if (!engine) { m->dsp->ops->step(m->dsp); c->cycle_count += c->instr_cycle; }
                    else { m->dsp->save_cycles = 0; m->dsp->ops->run(m->dsp, 1); }
                    if (c->pc != (absolute ? 2 : 1)) { fprintf(stderr, "P MOVEP cache first PC=%x engine=%u absolute=%u self=%u\n", c->pc, engine, absolute, self); exit(1); }
                    c->pc = target;
                    if (!engine) { m->dsp->ops->step(m->dsp); c->cycle_count += c->instr_cycle; }
                    else { m->dsp->save_cycles = 0; m->dsp->ops->run(m->dsp, 1); }
                    c->read_y_peripheral = saved_read; c->write_y_peripheral = saved_write;
                    if (c->pc != target + 1 || c->cycle_count != (self ? 0 : 2) + (absolute ? 8 : 6) + 2 ||
                        c->registers[DSP_REG_A0] != 0x123456 || c->registers[DSP_REG_A1] != 0x234567 || c->registers[DSP_REG_A2] ||
                        c->registers[DSP_REG_B0] || c->registers[DSP_REG_B1] || c->registers[DSP_REG_B2] ||
                        c->pram[target] != 0x20001b || qq_reads != 1 || qq_writes || qq_read_address != 0xffff80 + ports[p]) {
                        fprintf(stderr, "Y:qq P cache engine=%u absolute=%u self=%u port=%u PC=%x reads=%u\n",
                                engine, absolute, self, ports[p], c->pc, qq_reads);
                        exit(1);
                    }
                }
                if (dspjit_test_fallback_count() || compare_machines(a, b)) { fprintf(stderr, "P MOVEP fallback/state mismatch cases=%u fallback=%u\n", cases, (unsigned)dspjit_test_fallback_count()); exit(1); }
                cases++;
            }
        }
    }
    static const unsigned context_ports[] = {0, 1, 31, 32, 0x33, 63};
    static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000, 0x40000000};
    for (unsigned ea = 0; ea < 64; ea++) {
        if ((ea >> 3) == 6 && (ea & 7) != 0) continue;
        for (unsigned p = 0; p < ARRAY_SIZE(context_ports); p++) {
            for (unsigned out = 0; out < 2; out++) {
                for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                    for (unsigned seed = 0; seed < 2; seed++)
                        check(a, b, 0x008040 | (out << 14) | (ea << 8) | context_ports[p], 0x40,
                              0x400000 | 0x10000 | 0x1000000 | contexts[ctx] | seed, mode, seed != 0);
                }
            }
        }
    }
}


static void check_remaining_control(Machine *a, Machine *b, unsigned mode)
{
    for (unsigned kind = 0; kind < 3; kind++) for (unsigned cc = 0; cc < 16; cc++)
    for (unsigned flags = 0; flags < 256; flags++) for (unsigned profile = 0; profile < 2; profile++) {
        if (remaining_smoke && flags != 0 && flags != 1 && flags != 2 && flags != 4 && flags != 8 && flags != 16 && flags != 32 && flags != 64 && flags != 128 && flags != 255) continue;
        uint32_t op = (kind == 0 ? 0x210 : kind == 1 ? 0x300 : 0x10) | cc;
        bool taken = register_branch_condition(cc, flags);
        Machine *machines[] = {a, b}; dspjit_test_reset_fallback_count();
        for (unsigned engine = 0; engine < 2; engine++) {
            Machine *m = machines[engine]; prepare(m, op, 0x40, 1, mode, false);
            dsp_core_t *c = m->dsp->backend;
            c->exception_debugging = false;
            c->registers[DSP_REG_SR] = (mode << DSP_SR_S0) | flags | (kind == 0 ? (1u << DSP_SR_LF) : 0) | (profile ? 1u << DSP_SR_FV : 0);
            if (!kind) {
                c->registers[DSP_REG_SP] = 4;
                c->registers[DSP_REG_LA] = 0x40; c->registers[DSP_REG_LC] = 0;
                c->stack[0][3] = 0x60; c->stack[1][3] = 5;
                c->stack[0][4] = 2; c->stack[1][4] = profile ? (1u << DSP_SR_LF) | (1u << DSP_SR_FV) : 0;
                c->registers[DSP_REG_SSH] = c->stack[0][4]; c->registers[DSP_REG_SSL] = c->stack[1][4];
            }
            uint32_t regs[DSP_REG_MAX], stack[2][16];
            memcpy(regs, c->registers, sizeof(regs)); memcpy(stack, c->stack, sizeof(stack));
            if (!kind && taken) {
                regs[DSP_REG_SP] = 2; regs[DSP_REG_SSH] = stack[0][2]; regs[DSP_REG_SSL] = stack[1][2];
                regs[DSP_REG_LA] = 0x60; regs[DSP_REG_LC] = 5;
                regs[DSP_REG_SR] = (regs[DSP_REG_SR] & ~((1u << DSP_SR_LF) | (1u << DSP_SR_FV))) |
                                   (stack[1][4] & ((1u << DSP_SR_LF) | (1u << DSP_SR_FV)));
            }
            if (!engine) { m->dsp->ops->step(m->dsp); c->cycle_count += c->instr_cycle; }
            else m->dsp->ops->run(m->dsp, 1);
            unsigned cycles = kind == 0 ? 2 : kind == 1 ? 10 : 18;
            if (c->pc != (!kind && taken ? 0x41 : 1) || c->cycle_count != cycles ||
                c->is_idle != (kind == 1 && taken) || memcmp(regs, c->registers, sizeof(regs)) ||
                memcmp(stack, c->stack, sizeof(stack)) ||
                (kind == 2 && (c->interrupt_state != (taken ? DSP_INTERRUPT_DISABLED : DSP_INTERRUPT_NONE) ||
                               c->interrupt_instr_fetch != (taken ? 8 : 0xffff)))) {
                fprintf(stderr, "remaining control engine=%u kind=%u cc=%u flags=%x profile=%u pc=%x cycles=%u irq=%u vector=%x\n",
                        engine, kind, cc, flags, profile, c->pc, c->cycle_count, c->interrupt_state, c->interrupt_instr_fetch);
                exit(1);
            }
        }
        if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
        cases++;
    }
    static const uint32_t cache[] = {1,2,3,14,15,0x0bf081,0x0af081,0x0be081,0x0ae081};
    for (unsigned i = 0; i < ARRAY_SIZE(cache) + 2; i++) {
        uint32_t op = i < ARRAY_SIZE(cache) ? cache[i] : i == ARRAY_SIZE(cache) ? 6 : 0x200;
        Machine *machines[] = {a, b}; dspjit_test_reset_fallback_count();
        for (unsigned engine = 0; engine < 2; engine++) {
            Machine *m = machines[engine]; prepare(m, op, 0x40, 1, mode, false);
            dsp_core_t *c = m->dsp->backend;
            c->exception_debugging = false;
            uint32_t regs[DSP_REG_MAX], stack[2][16];
            memcpy(regs, c->registers, sizeof(regs)); memcpy(stack, c->stack, sizeof(stack));
            if (!engine) { m->dsp->ops->step(m->dsp); c->cycle_count += c->instr_cycle; }
            else m->dsp->ops->run(m->dsp, 1);
            bool cache_op = i < ARRAY_SIZE(cache);
            unsigned length = op == 14 || op == 15 || op == 0x0bf081 || op == 0x0af081 ? 2 : 1;
            unsigned cycles = !cache_op ? op == 6 ? 18 : 2 : op <= 3 ? 2 : op <= 15 ? 8 : 4;
            if (c->pc != length || c->cycle_count != cycles || c->is_idle != (op == 0x200) ||
                memcmp(regs, c->registers, sizeof(regs)) || memcmp(stack, c->stack, sizeof(stack)) ||
                (op != 0x200 && (c->interrupt_state != DSP_INTERRUPT_DISABLED || c->interrupt_instr_fetch != (cache_op ? 4 : 8)))) {
                fprintf(stderr, "remaining cache/trap/debug engine=%u op=%x pc=%x cycles=%u irq=%u vector=%x\n", engine, op, c->pc, c->cycle_count, c->interrupt_state, c->interrupt_instr_fetch);
                exit(1);
            }
        }
        if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
        cases++;
    }
    static const unsigned counts[] = {0,1,0xffff};
    for (unsigned relative = 0; relative < 2; relative++) for (unsigned n = 0; n < ARRAY_SIZE(counts); n++) {
        Machine *machines[] = {a,b};
        for (unsigned engine = 0; engine < 2; engine++) {
            Machine *m = machines[engine]; prepare(m, relative ? 0x202 : 0x203, 3, 1, mode, false);
            dsp_core_t *c = m->dsp->backend;
            c->registers[DSP_REG_LC] = counts[n]; c->registers[DSP_REG_X0] = 1;
            c->registers[DSP_REG_SR] = mode << DSP_SR_S0;
            remaining_set_acc(c, 0, 0);
            m->dsp->ops->write_memory(m->dsp, 'P', 2, 0x200040);
            m->dsp->ops->write_memory(m->dsp, 'P', 3, 0);
        }
        for (unsigned step = 0; step < 7; step++) {
            dspjit_test_reset_fallback_count();
            for (unsigned engine = 0; engine < 2; engine++) {
                Machine *m = machines[engine]; dsp_core_t *c = m->dsp->backend;
                if (!engine) { m->dsp->ops->step(m->dsp); c->cycle_count += c->instr_cycle; }
                else { m->dsp->save_cycles = 0; m->dsp->ops->run(m->dsp, 1); }
                unsigned completed = step / 2, additions = (step + 1) / 2;
                if (c->pc != (step & 1 ? 3 : 2) || c->registers[DSP_REG_LC] != ((counts[n] - completed) & 0xffff) ||
                    c->registers[DSP_REG_SP] != 4 || c->registers[DSP_REG_LA] != 3 ||
                    (c->registers[DSP_REG_SR] & ((1u << DSP_SR_FV) | (1u << DSP_SR_LF))) != ((1u << DSP_SR_FV) | (1u << DSP_SR_LF)) ||
                    c->registers[DSP_REG_A1] != additions || c->cycle_count != (relative ? 10 : 8) + step * 2) {
                    fprintf(stderr, "forever engine=%u relative=%u count=%x step=%u pc=%x LC=%x SR=%x A1=%x cycles=%u\n", engine, relative, counts[n], step, c->pc, c->registers[DSP_REG_LC], c->registers[DSP_REG_SR], c->registers[DSP_REG_A1], c->cycle_count);
                    exit(1);
                }
            }
            if (dspjit_test_fallback_count() || compare_machines(a,b)) exit(1);
            cases++;
        }
    }
}

static void check_remaining_y_ea(Machine *a, Machine *b, unsigned mode)
{
    static const uint32_t values[] = {0,0xffffff,0xaaaaaa,0x555555};
    for (unsigned ea = 0; ea < 64; ea++) {
        unsigned am = ea >> 3, r = ea & 7;
        if (am == 6 && r != 0 && r != 4) continue;
        for (unsigned port = 0; port < 64; port++) for (unsigned out = 0; out < 2; out++)
        for (unsigned space = 0; space < 2; space++) for (unsigned v = 0; v < ARRAY_SIZE(values); v++) {
            if (remaining_smoke && !remaining_smoke_port(port)) continue;
            bool immediate = ea == 52;
            if (immediate && !out) continue;
            uint32_t op = 0x070080 | (out << 15) | (ea << 8) | (space << 6) | port;
            uint32_t addr = am == 5 ? 0x43 : 0x40;
            uint32_t next = am == 0 ? 0x3d : am == 1 ? 0x43 : am == 2 ? 0x3f : am == 3 ? 0x41 : 0x40;
            Machine *machines[] = {a,b}; dspjit_test_reset_fallback_count();
            for (unsigned engine = 0; engine < 2; engine++) {
                Machine *m = machines[engine]; prepare(m, op, immediate ? values[v] : 0x40, 1, mode, false);
                dsp_core_t *c = m->dsp->backend;
                c->registers[DSP_REG_R0 + r] = am == 7 ? 0x41 : 0x40;
                c->registers[DSP_REG_N0 + r] = 3; c->registers[DSP_REG_M0 + r] = 0xffff;
                c->registers[DSP_REG_SR] = (mode << DSP_SR_S0) | 0x100ff;
                uint32_t *ram = space ? c->yram : c->xram;
                ram[addr] = out ? values[v] : values[v] ^ 0xffffff;
                uint32_t regs[DSP_REG_MAX], stack[2][16], x[DSP_XRAM_SIZE], y[DSP_YRAM_SIZE];
                memcpy(regs,c->registers,sizeof(regs)); memcpy(stack,c->stack,sizeof(stack));
                memcpy(x,c->xram,sizeof(x)); memcpy(y,c->yram,sizeof(y));
                regs[DSP_REG_R0+r] = next;
                if (!out) (space ? y : x)[addr] = values[v];
                uint32_t (*saved_read)(dsp_core_t*,uint32_t) = c->read_y_peripheral;
                void (*saved_write)(dsp_core_t*,uint32_t,uint32_t) = c->write_y_peripheral;
                c->read_y_peripheral=qq_test_read; c->write_y_peripheral=qq_test_write;
                qq_reads=qq_writes=0; qq_callback_value=values[v]; qq_read_address=qq_write_address=0; qq_written_value=UINT32_MAX;
                if (!engine) { m->dsp->ops->step(m->dsp); c->cycle_count+=c->instr_cycle; }
                else m->dsp->ops->run(m->dsp,1);
                c->read_y_peripheral=saved_read; c->write_y_peripheral=saved_write;
                if (c->pc != (am==6 ? 2:1) || c->cycle_count != (am>=5 ? 6:4) ||
                    memcmp(regs,c->registers,sizeof(regs)) || memcmp(stack,c->stack,sizeof(stack)) ||
                    memcmp(x,c->xram,sizeof(x)) || memcmp(y,c->yram,sizeof(y)) ||
                    qq_reads != !out || qq_writes != out ||
                    (out ? qq_write_address != 0xffff80+port || qq_written_value != values[v] : qq_read_address != 0xffff80+port)) {
                    fprintf(stderr,"Y MOVEP EA engine=%u ea=%u port=%u out=%u space=%u PC=%x cycles=%u reads=%u writes=%u\n",engine,ea,port,out,space,c->pc,c->cycle_count,qq_reads,qq_writes);
                    exit(1);
                }
            }
            if (dspjit_test_fallback_count() || compare_machines(a,b)) exit(1);
            cases++;
        }
    }
}

static void check_remaining_forever_setup(Machine *a, Machine *b, unsigned mode)
{
    static const unsigned counts[] = {0,1,0xffff}, slots[] = {0,1,2,6,12,13};
    for (unsigned relative=0; relative<2; relative++) for (unsigned location=0; location<2; location++)
    for (unsigned endpoint=0; endpoint<2; endpoint++) for (unsigned n=0; n<ARRAY_SIZE(counts); n++)
    for (unsigned profile=0; profile<4; profile++) for (unsigned sp=0; sp<ARRAY_SIZE(slots); sp++) {
        uint32_t pc=location?0x20:0, ext=endpoint?0xffffff:0x40, op=relative?0x202:0x203;
        Machine *machines[]={a,b}; dspjit_test_reset_fallback_count();
        for (unsigned engine=0; engine<2; engine++) {
            Machine *m=machines[engine]; prepare(m,op,ext,1,mode,false); dsp_core_t *c=m->dsp->backend;
            c->pc=pc; m->dsp->ops->write_memory(m->dsp,'P',pc,op); m->dsp->ops->write_memory(m->dsp,'P',pc+1,ext);
            c->registers[DSP_REG_SR]=(mode<<DSP_SR_S0)|0xef|((profile&1)?1u<<DSP_SR_FV:0)|((profile&2)?1u<<DSP_SR_LF:0);
            c->registers[DSP_REG_LA]=0x60; c->registers[DSP_REG_LC]=counts[n]; c->registers[DSP_REG_SP]=slots[sp];
            c->registers[DSP_REG_SSH]=c->stack[0][slots[sp]]; c->registers[DSP_REG_SSL]=c->stack[1][slots[sp]];
            uint32_t regs[DSP_REG_MAX],stack[2][16]; memcpy(regs,c->registers,sizeof(regs)); memcpy(stack,c->stack,sizeof(stack));
            unsigned top=slots[sp]+2;
            stack[0][top-1]=0x60; stack[1][top-1]=counts[n]; stack[0][top]=pc+2; stack[1][top]=regs[DSP_REG_SR];
            regs[DSP_REG_SP]=top; regs[DSP_REG_SSH]=pc+2; regs[DSP_REG_SSL]=regs[DSP_REG_SR];
            regs[DSP_REG_LA]=((relative?pc:0)+ext)&0xffff; regs[DSP_REG_SR]|=(1u<<DSP_SR_LF)|(1u<<DSP_SR_FV);
            if (!engine) {m->dsp->ops->step(m->dsp);c->cycle_count+=c->instr_cycle;} else m->dsp->ops->run(m->dsp,1);
            if(c->pc!=pc+2 || c->cycle_count!=(relative?10:8) || memcmp(regs,c->registers,sizeof(regs)) || memcmp(stack,c->stack,sizeof(stack))) {
                fprintf(stderr,"forever setup engine=%u relative=%u PC=%x/%x ext=%x slot=%u SR=%x/%x\n",engine,relative,c->pc,pc+2,ext,slots[sp],c->registers[DSP_REG_SR],regs[DSP_REG_SR]);exit(1);
            }
        }
        if (dspjit_test_fallback_count() || compare_machines(a,b)) exit(1);
        cases++;
    }
    for(unsigned exit_outer=0;exit_outer<2;exit_outer++) {
        Machine *machines[]={a,b};
        for(unsigned engine=0;engine<2;engine++) {
            Machine *m=machines[engine]; prepare(m,0x203,9,1,mode,false);dsp_core_t *c=m->dsp->backend;
            uint32_t code[]={0x203,9,0x060180,5,0x200040,0,exit_outer?0x210:0x200040,0,0,0,0x200};
            for(unsigned i=0;i<ARRAY_SIZE(code);i++)m->dsp->ops->write_memory(m->dsp,'P',i,code[i]);
            c->registers[DSP_REG_SR]=mode<<DSP_SR_S0;c->registers[DSP_REG_LC]=1;c->registers[DSP_REG_X0]=1;remaining_set_acc(c,0,0);
        }
        static const unsigned pcs[]={2,4,5,6,7,8,9,2,4,5,6,7,8,9,2};
        for(unsigned step=0;step<(exit_outer?5:15);step++) {
            dspjit_test_reset_fallback_count();
            for(unsigned engine=0;engine<2;engine++) {
                Machine *m=machines[engine];dsp_core_t *c=m->dsp->backend;
                if(!engine){m->dsp->ops->step(m->dsp);c->cycle_count+=c->instr_cycle;}
                else{m->dsp->save_cycles=0;m->dsp->ops->run(m->dsp,1);}
                bool inner=step==1||step==2||step==8||step==9, exited=exit_outer&&step==4;
                unsigned expected_pc=exited?10:pcs[step], expected_sp=exited?2:inner?6:4;
                unsigned flags=c->registers[DSP_REG_SR]&((1u<<DSP_SR_LF)|(1u<<DSP_SR_FV));
                unsigned expected_flags=exited?0:inner?1u<<DSP_SR_LF:(1u<<DSP_SR_LF)|(1u<<DSP_SR_FV);
                if(c->pc!=expected_pc||c->registers[DSP_REG_SP]!=expected_sp||flags!=expected_flags) {
                    fprintf(stderr,"nested forever engine=%u exit=%u step=%u PC=%x/%x SP=%u/%u flags=%x/%x\n",engine,exit_outer,step,c->pc,expected_pc,c->registers[DSP_REG_SP],expected_sp,flags,expected_flags);exit(1);
                }
            }
            if (dspjit_test_fallback_count() || compare_machines(a,b)) exit(1);
            cases++;
        }
    }
    for(unsigned relative=0;relative<2;relative++)for(unsigned context=0;context<3;context++)for(unsigned seed=0;seed<4;seed++) {
        static const unsigned contexts[]={0x40000,0x80000,0x40000000};
        check(a,b,relative?0x202:0x203,0x40,0x400000|contexts[context]|(seed<<1),mode,seed&1);
    }
}
