/*
 *  i386 CPUID, CPU class, definitions, models: system-only code
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "qapi/error.h"
#include "qapi/qapi-visit-run-state.h"
#include "qobject/qdict.h"
#include "qapi/qobject-input-visitor.h"
#include "qom/qom-qobject.h"
#include "qapi/qapi-commands-machine.h"

#include "cpu-internal.h"

void cpu_clear_apic_feature(CPUX86State *env)
{
    env->features[FEAT_1_EDX] &= ~CPUID_APIC;
}

void cpu_set_apic_feature(CPUX86State *env)
{
    env->features[FEAT_1_EDX] |= CPUID_APIC;
}

bool cpu_has_x2apic_feature(CPUX86State *env)
{
    return env->features[FEAT_1_ECX] & CPUID_EXT_X2APIC;
}

bool cpu_is_bsp(X86CPU *cpu)
{
    return cpu_get_apic_base(cpu->apic_state) & MSR_IA32_APICBASE_BSP;
}

/* TODO: remove me, when reset over QOM tree is implemented */
void x86_cpu_machine_reset_cb(void *opaque)
{
    X86CPU *cpu = opaque;
    cpu_reset(CPU(cpu));
}

GuestPanicInformation *x86_cpu_get_crash_info(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    GuestPanicInformation *panic_info = NULL;

    if (hyperv_feat_enabled(cpu, HYPERV_FEAT_CRASH)) {
        panic_info = g_new0(GuestPanicInformation, 1);

        panic_info->type = GUEST_PANIC_INFORMATION_TYPE_HYPER_V;

        assert(HV_CRASH_PARAMS >= 5);
        panic_info->u.hyper_v.arg1 = env->msr_hv_crash_params[0];
        panic_info->u.hyper_v.arg2 = env->msr_hv_crash_params[1];
        panic_info->u.hyper_v.arg3 = env->msr_hv_crash_params[2];
        panic_info->u.hyper_v.arg4 = env->msr_hv_crash_params[3];
        panic_info->u.hyper_v.arg5 = env->msr_hv_crash_params[4];
    }

    return panic_info;
}
void x86_cpu_get_crash_info_qom(Object *obj, Visitor *v,
                                const char *name, void *opaque,
                                Error **errp)
{
    CPUState *cs = CPU(obj);
    GuestPanicInformation *panic_info;

    if (!cs->crash_occurred) {
        error_setg(errp, "No crash occurred");
        return;
    }

    panic_info = x86_cpu_get_crash_info(cs);
    if (panic_info == NULL) {
        error_setg(errp, "No crash information");
        return;
    }

    visit_type_GuestPanicInformation(v, "crash-information", &panic_info,
                                     errp);
    qapi_free_GuestPanicInformation(panic_info);
}

uint64_t cpu_x86_get_msr_core_thread_count(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    uint64_t val;

    val = x86_threads_per_pkg(&env->topo_info);  /* thread count, bits 15..0 */
    val |= x86_cores_per_pkg(&env->topo_info) << 16; /* core count, bits 31..16 */

    return val;
}
